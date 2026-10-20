#include "AssistantSettingsPanel.h"
#include "application/assistant/CodexLoginPolicy.h"
#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <atomic>
namespace devhub::presentation::settings {
using namespace application::assistant;
namespace { std::atomic<quint64> nextProbeGeneration{0}; }
AssistantSettingsPanel::AssistantSettingsPanel(AssistantController& controller, ICodexExecutableProbe& probe,
    AssistantConfiguration configuration, QWidget* parent, std::function<bool(const QUrl&)> openUrl)
    : QWidget(parent), m_controller(controller), m_probe(probe), m_openUrl(std::move(openUrl))
{
    if (!m_openUrl) m_openUrl = [](const QUrl& url) { return QDesktopServices::openUrl(url); };
    auto* root = new QVBoxLayout(this);
    auto* explanation = new QLabel(tr("Assistant uses the native Codex executable and its shared ChatGPT sign-in. No API key is required. Completing sign-in may change the account used by other Codex apps. Chat stays inside DevHub; only explicit authorization opens a browser."), this);
    explanation->setTextFormat(Qt::PlainText); explanation->setWordWrap(true); root->addWidget(explanation);
    auto* form = new QFormLayout; root->addLayout(form);
    auto* executableRow = new QWidget(this); auto* executableLayout = new QHBoxLayout(executableRow); executableLayout->setContentsMargins(0, 0, 0, 0);
    m_executable = new QLineEdit(configuration.executablePath, executableRow); m_executable->setObjectName("assistantExecutable"); m_executable->setMaxLength(2048);
    m_executable->setPlaceholderText(tr("Empty uses native codex.exe on PATH")); m_executable->setAccessibleName(tr("Native Codex executable override"));
    auto* browse = new QPushButton(tr("Browse…"), executableRow); browse->setObjectName("assistantBrowse");
    m_check = new QPushButton(tr("Recheck executable"), executableRow); m_check->setObjectName("assistantRecheck");
    executableLayout->addWidget(m_executable, 1); executableLayout->addWidget(browse); executableLayout->addWidget(m_check); form->addRow(tr("Native executable"), executableRow);
    m_probeStatus = new QLabel(this); m_probeStatus->setObjectName("assistantProbeStatus"); m_probeStatus->setTextFormat(Qt::PlainText); m_probeStatus->setWordWrap(true); root->addWidget(m_probeStatus);
    m_model = new QComboBox(this); m_model->setObjectName("assistantModel"); m_model->setAccessibleName(tr("Preferred text model")); m_model->addItem(tr("Automatic (Codex default text model)"), QString{});
    if (!configuration.preferredModel.isEmpty()) { m_model->addItem(configuration.preferredModel, configuration.preferredModel); m_model->setCurrentIndex(1); }
    form->addRow(tr("Preferred model"), m_model);
    auto* catalog = new QLabel(tr("Models shown here may require a different subscription. An unavailable saved choice disables Send; Automatic uses an available default. Save applies preferences after the current operation finishes."), this);
    catalog->setTextFormat(Qt::PlainText); catalog->setWordWrap(true); root->addWidget(catalog);
    m_runtime = new QLabel(this); m_runtime->setObjectName("assistantRuntimeStatus"); m_runtime->setTextFormat(Qt::PlainText); m_runtime->setWordWrap(true); root->addWidget(m_runtime);
    m_status = new QLabel(this); m_status->setObjectName("assistantSetupStatus"); m_status->setTextFormat(Qt::PlainText); m_status->setWordWrap(true); root->addWidget(m_status);
    m_verify = new QPushButton(tr("Verify runtime restrictions"), this); m_verify->setObjectName("assistantVerifyCompatibility");
    m_verify->setToolTip(tr("Runs offline checks with synthetic local files and disposable Codex homes. Requires Python 3. No sign-in or real inference."));
    root->addWidget(m_verify);
    connect(m_verify, &QPushButton::clicked, this, [this] {
        if (m_controller.compatibilityVerificationActive()) m_controller.cancelCompatibilityVerification();
        else m_controller.verifyCompatibility();
    });
    auto* actions = new QHBoxLayout;
    m_reconnect = new QPushButton(tr("Check connection / Reconnect"), this); m_reconnect->setObjectName("assistantSetupReconnect");
    m_browser = new QPushButton(tr("Connect ChatGPT"), this); m_browser->setObjectName("assistantConnectBrowser");
    m_device = new QPushButton(tr("Connect with device code"), this); m_device->setObjectName("assistantConnectDevice");
    actions->addWidget(m_reconnect); actions->addWidget(m_browser); actions->addWidget(m_device); root->addLayout(actions);
    m_loginStatus = new QLabel(this); m_loginStatus->setObjectName("assistantLoginStatus"); m_loginStatus->setTextFormat(Qt::PlainText); m_loginStatus->setWordWrap(true); root->addWidget(m_loginStatus);
    m_challengeForm = new QFormLayout; root->addLayout(m_challengeForm);
    m_url = new QLineEdit(this); m_url->setObjectName("assistantAuthorizationUrl"); m_url->setReadOnly(true); m_url->setAccessibleName(tr("Transient sign-in authorization URL"));
    m_code = new QLineEdit(this); m_code->setObjectName("assistantDeviceCode"); m_code->setReadOnly(true); m_code->setAccessibleName(tr("Transient device code"));
    m_challengeForm->addRow(tr("Authorization page"), m_url); m_challengeForm->addRow(tr("Device code"), m_code);
    auto* challengeActions = new QHBoxLayout;
    m_open = new QPushButton(tr("Open authorization page"), this); m_open->setObjectName("assistantOpenAuthorization");
    m_cancel = new QPushButton(tr("Cancel sign-in"), this); m_cancel->setObjectName("assistantCancelLogin");
    challengeActions->addWidget(m_open); challengeActions->addWidget(m_cancel); challengeActions->addStretch(); root->addLayout(challengeActions);
    for (auto* button : {browse, m_check, m_reconnect, m_browser, m_device, m_open, m_cancel}) button->setAccessibleName(button->text());
    connect(browse, &QPushButton::clicked, this, [this] {
        const auto selected = QFileDialog::getOpenFileName(this, tr("Select native Codex executable"), m_executable->text(), tr("Native executable (*.exe)"));
        if (!selected.isEmpty()) m_executable->setText(selected);
    });
    connect(m_executable, &QLineEdit::textChanged, this, [this] {
        ++m_generation; m_checking = false; m_probe.cancel(); m_probeStatus->clear();
        if (m_controller.compatibilityVerificationActive()) m_controller.cancelCompatibilityVerification();
        refresh();
    });
    connect(m_check, &QPushButton::clicked, this, &AssistantSettingsPanel::recheck);
    connect(&probe, &ICodexExecutableProbe::executableFound, this, [this](const CodexExecutable& executable, quint64 generation) {
        if (generation != m_generation || !m_checking) return;
        m_checking = false;
        m_probeStatus->setText(tr("Codex %1 executable check passed. Connection status is shown below.").arg(executable.version));
        refresh();
    });
    connect(&probe, &ICodexExecutableProbe::probeFailed, this, [this](const domain::errors::AppError& error, quint64 generation) {
        if (generation != m_generation || !m_checking) return;
        m_checking = false; m_probeStatus->setText(error.userMessage); refresh();
    });
    connect(m_reconnect, &QPushButton::clicked, &controller, &AssistantController::reconnect);
    connect(m_browser, &QPushButton::clicked, this, [this] { startLogin(CodexLoginMode::Browser); });
    connect(m_device, &QPushButton::clicked, this, [this] { startLogin(CodexLoginMode::DeviceCode); });
    connect(m_cancel, &QPushButton::clicked, &controller, &AssistantController::cancelAccountLogin);
    connect(m_open, &QPushButton::clicked, this, [this] {
        const auto value = m_controller.accountChallenge();
        if (value && m_controller.setupAllowed() && acceptsCodexLoginUrl(value->url, value->mode == CodexLoginMode::DeviceCode)
            && !m_openUrl(value->url)) m_loginStatus->setText(tr("The system could not open the authorization page."));
    });
    connect(&controller, &AssistantController::stateChanged, this, &AssistantSettingsPanel::refresh);
    root->addStretch(); refresh();
}
AssistantSettingsPanel::~AssistantSettingsPanel()
{
    ++m_generation; m_checking = false; m_probe.cancel();
    if (m_ownsLogin && m_controller.loginActive()) m_controller.cancelAccountLogin();
}
void AssistantSettingsPanel::recheck()
{
    if (!m_controller.setupAllowed() || m_checking) return;
    m_generation = ++nextProbeGeneration; m_checking = true; m_probeStatus->setText(tr("Inspecting the executable and its protocol schema…")); refresh();
    m_probe.locate(m_executable->text(), m_generation);
}
void AssistantSettingsPanel::startLogin(CodexLoginMode mode)
{
    m_ownsLogin = true; m_controller.connectAccount(mode); if (!m_controller.loginActive()) m_ownsLogin = false;
}
void AssistantSettingsPanel::refreshModels()
{
    QStringList signature;
    for (const auto& model : m_controller.models()) {
        if (model.hidden || !model.supportsText) continue;
        signature << model.model << model.displayName << model.availabilityMessage;
    }
    if (m_modelCatalogInitialized && signature == m_modelCatalogSignature) return;
    m_modelCatalogInitialized = true; m_modelCatalogSignature = std::move(signature);
    const auto selected = m_model->currentData().toString(); const QSignalBlocker blocker(m_model);
    m_model->clear(); m_model->addItem(tr("Automatic (Codex default text model)"), QString{});
    for (const auto& model : m_controller.models()) {
        if (model.hidden || !model.supportsText || m_model->findData(model.model) >= 0) continue;
        const bool available = model.availabilityMessage.isEmpty();
        m_model->addItem((model.displayName.isEmpty() ? model.model : model.displayName).left(100)
            + (available ? QString{} : tr(" (unavailable)")), model.model);
        if (!available) static_cast<QStandardItemModel*>(m_model->model())->item(m_model->count() - 1)->setEnabled(false);
    }
    int index = m_model->findData(selected);
    if (index < 0 && !selected.isEmpty()) { m_model->addItem(tr("Unavailable saved choice: %1").arg(selected.left(100)), selected); index = m_model->count() - 1; static_cast<QStandardItemModel*>(m_model->model())->item(index)->setEnabled(false); }
    m_model->setCurrentIndex(qMax(0, index));
}
void AssistantSettingsPanel::refresh()
{
    if (!m_controller.setupAllowed() && m_checking) { ++m_generation; m_checking = false; m_probe.cancel(); m_probeStatus->clear(); }
    const bool savedPath = m_executable->text() == m_controller.configuration().executablePath;
    m_verify->setText(m_controller.compatibilityVerificationActive() ? tr("Cancel verification") : tr("Verify runtime restrictions"));
    m_verify->setEnabled(savedPath && (m_controller.canVerifyCompatibility() || m_controller.compatibilityVerificationActive()));
    m_check->setEnabled(m_controller.setupAllowed() && !m_checking); m_reconnect->setEnabled(m_controller.canReconnect() && savedPath);
    m_browser->setEnabled(m_controller.canConnectAccount() && savedPath); m_device->setEnabled(m_controller.canConnectAccount() && savedPath); m_cancel->setEnabled(m_controller.loginActive());
    m_runtime->setText(m_controller.runtimeText()); m_status->setText(m_controller.errorText().isEmpty() ? m_controller.statusText() : m_controller.errorText());
    m_loginStatus->setText(m_controller.loginStatusText());
    m_loginStatus->setVisible(!m_controller.loginStatusText().isEmpty());
    m_probeStatus->setVisible(!m_probeStatus->text().isEmpty());
    const auto challenge = m_controller.accountChallenge();
    m_url->setText(challenge ? challenge->url.toString(QUrl::FullyEncoded) : QString{}); m_code->setText(challenge ? challenge->userCode : QString{});
    m_challengeForm->setRowVisible(m_url, challenge.has_value()); m_challengeForm->setRowVisible(m_code, challenge && challenge->mode == CodexLoginMode::DeviceCode);
    m_open->setVisible(challenge.has_value()); m_cancel->setVisible(m_controller.loginActive());
    m_open->setEnabled(challenge.has_value() && m_controller.setupAllowed());
    refreshModels();
}
}
