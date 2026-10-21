#include "support/AssistantHarness.h"
#include "application/settings/SettingsService.h"
#include "application/assistant/CodexLoginPolicy.h"
#include "presentation/settings/AssistantSettingsPanel.h"
#include "presentation/settings/SettingsWindow.h"
#include "presentation/theme/ThemeManager.h"
#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFontDatabase>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QListWidget>
#include <QStackedWidget>
#include <QTest>
using namespace devhub;
using namespace application::assistant;
using namespace application::settings;
using namespace tests::assistant;
using namespace tests::codex;
using presentation::settings::AssistantSettingsPanel;
using presentation::settings::SettingsWindow;
class SettingsRepository final : public ISettingsRepository {
public:
    SettingValues values; SettingUpdates lastUpdates; bool failSave = false; int failBatchAfterWrites = -1; int lastBatchWrites = 0;
    shared::common::Result<SettingValues> load(SettingScope, const QString&) override { return shared::common::Result<SettingValues>::success(values); }
    shared::common::Result<void> save(SettingScope, const QString&, const QString& id, const QVariant& value) override {
        if (failSave) return shared::common::Result<void>::failure(domain::errors::AppError::failure(domain::errors::ErrorCode::IoFailure,
            domain::errors::ErrorCategory::Persistence, "Synthetic save failure", {}));
        values.insert(id, value); return shared::common::Result<void>::success();
    }
    shared::common::Result<void> saveBatch(const SettingUpdates& updates) override {
        lastUpdates = updates;
        auto staged = values; lastBatchWrites = 0;
        for (const auto& update : updates) {
            if (update.scope == SettingScope::Session) continue;
            ++lastBatchWrites;
            if (failSave || (failBatchAfterWrites >= 0 && lastBatchWrites > failBatchAfterWrites))
                return shared::common::Result<void>::failure(domain::errors::AppError::failure(
                    domain::errors::ErrorCode::IoFailure, domain::errors::ErrorCategory::Persistence,
                    "Synthetic save failure", "Failure injected after an earlier staged write."));
            staged.insert(update.id, update.value);
        }
        values = std::move(staged);
        return shared::common::Result<void>::success();
    }
};
class SetupProbe final : public ICodexExecutableProbe {
public:
    QString path; quint64 generation = 0; bool active = false;
    void locate(const QString& value, quint64 id) override { path = value; generation = id; active = true; }
    void cancel() override { active = false; }
    bool isProbing() const override { return active; }
    void complete(quint64 id) { active = false; emit executableFound({"C:/fixture/codex.exe", "0.160.1"}, id); }
};
class AssistantSettingsTest final : public QObject {
    Q_OBJECT
    QTemporaryDir m_settingsRoot;
    std::unique_ptr<presentation::theme::ThemeManager> m_theme;
    static QJsonObject model(QString availability = {}) {
        QJsonObject value{{"id", "fixture"}, {"model", "fixture-model"}, {"displayName", "Fixture"}, {"description", "Synthetic model"},
            {"defaultReasoningEffort", "medium"}, {"supportedReasoningEfforts", QJsonArray{}}, {"hidden", false}, {"isDefault", true}, {"inputModalities", QJsonArray{"text"}}};
        if (!availability.isEmpty()) value.insert("availabilityNux", QJsonObject{{"message", availability}});
        return value;
    }
private slots:
    void initTestCase() {
        const int font = QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf"); QVERIFY(font >= 0);
        QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 10));
        QCoreApplication::setOrganizationName("DevHubAssistantSettingsTests"); QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsRoot.path());
        m_theme = std::make_unique<presentation::theme::ThemeManager>(*qApp);
    }
    void descriptorsAndSensitiveValues() {
        SettingsRepository repository; SettingsService settings(repository);
        QCOMPARE(settings.value("assistant/executable").toString(), QString{}); QCOMPARE(settings.value("assistant/model").toString(), QString{});
        const auto executable = settings.descriptor("assistant/executable"); const auto preferred = settings.descriptor("assistant/model");
        QCOMPARE(executable.section, QStringLiteral("Assistant")); QCOMPARE(executable.sensitivity, SettingSensitivity::Sensitive); QVERIFY(!executable.requiresRestart);
        QVERIFY(settings.setValue("assistant/executable", QString::fromUtf8("C:/Native İ path/codex.exe")));
        QVERIFY(settings.setValue("assistant/model", "fixture-model")); QVERIFY(!settings.descriptor("assistant/apiKey").validate);
        QVERIFY(!settings.sanitizedValues().contains(executable.label)); QVERIFY(!settings.sanitizedValues().contains(preferred.label));
        for (const auto& path : {"relative.exe", "C:/codex.cmd", "C:/codex.bat", "C:/codex.ps1", "\"C:/codex.exe\"", "C:/codex\n.exe"}) QVERIFY(!settings.setValue("assistant/executable", QString::fromLatin1(path)));
        QVERIFY(!settings.setValue("assistant/model", QString(513, QChar('x')))); QVERIFY(!settings.setValue("assistant/model", "bad\nmodel"));
        QVERIFY(!settings.setValue("assistant/executable", 123));
    }
    void descriptorDefaultsAreValid() {
        SettingsRepository repository; SettingsService settings(repository);
        for (const auto& descriptor : settings.descriptors()) {
            QVERIFY2(descriptor.validate, qPrintable(descriptor.id));
            QVERIFY2(descriptor.validate(descriptor.defaultValue), qPrintable(descriptor.id));
        }
    }
    void somethingQtProfileUsesGlobalSensitiveScopeAndTagsAreWorkspaceScoped() {
        SettingsRepository repository; SettingsService settings(repository);
        const QString profilePath = QDir(QDir::rootPath()).filePath(QStringLiteral("Something/profiles/default/something.sqlite"));
        const auto profile = settings.descriptor(QStringLiteral("assistant/somethingQtProfilePath"));
        const auto tag = settings.descriptor(QStringLiteral("assistant/somethingQtWorkspaceTag"));
        const auto restriction = settings.descriptor(QStringLiteral("assistant/somethingQtRestrictWorkspaceTag"));
        QCOMPARE(profile.scope, SettingScope::Global);
        QCOMPARE(profile.sensitivity, SettingSensitivity::Sensitive);
        QVERIFY(profile.defaultValue.toString().isEmpty());
        QVERIFY(profile.validate(profilePath));
        QVERIFY(!profile.validate(QStringLiteral("relative/something.sqlite")));
        QCOMPARE(tag.scope, SettingScope::Workspace);
        QCOMPARE(restriction.scope, SettingScope::Workspace);
        QVERIFY(settings.setValue(profile.id, profilePath));
        QCOMPARE(repository.lastUpdates.size(), 1);
        QCOMPARE(repository.lastUpdates.first().scope, SettingScope::Global);
        QVERIFY(!settings.sanitizedValues().contains(profile.label));
        QVERIFY(settings.setValue(tag.id, QStringLiteral("DevHub"), QStringLiteral("workspace-1")));
        QCOMPARE(repository.lastUpdates.first().scope, SettingScope::Workspace);
        QCOMPARE(repository.lastUpdates.first().workspaceId, QStringLiteral("workspace-1"));
        QVERIFY(settings.setValue(restriction.id, true, QStringLiteral("workspace-1")));
        QCOMPARE(repository.lastUpdates.first().scope, SettingScope::Workspace);
    }
    void restoreConversationPreferenceUsesBooleanValidation() {
        SettingsRepository repository; SettingsService settings(repository);
        const auto id = QStringLiteral("assistant/restoreConversation");
        QCOMPARE(settings.value(id).metaType().id(), QMetaType::Bool);
        QCOMPARE(settings.value(id).toBool(), true);
        for (const bool enabled : {false, true}) {
            QVERIFY(settings.setValue(id, enabled));
            QCOMPARE(repository.values.value(id).metaType().id(), QMetaType::Bool);
            SettingsService reloaded(repository);
            QCOMPARE(reloaded.value(id).metaType().id(), QMetaType::Bool);
            QCOMPARE(reloaded.value(id).toBool(), enabled);
        }
        QVERIFY(!settings.setValue(id, QStringLiteral("false")));
        QVERIFY(!settings.setValue(id, QString{}));
        QVERIFY(!settings.setValue(id, 0));
        QVERIFY(!settings.setValue(id, QVariant{}));
        QCOMPARE(repository.values.value(id).toBool(), true);
    }
    void asyncRecheckUsesDraftAndIgnoresStaleResult() {
        Harness h; SetupProbe probe; AssistantSettingsPanel panel(h.controller, probe, {});
        auto* executable = panel.executableControl(); executable->setText(QString::fromUtf8("C:/Native İ path/codex.exe"));
        panel.findChild<QPushButton*>("assistantRecheck")->click(); QCOMPARE(probe.path, executable->text()); const auto old = probe.generation;
        QVERIFY(!panel.findChild<QPushButton*>("assistantRecheck")->isEnabled());
        executable->setText("C:/Second path/codex.exe"); probe.complete(old); QVERIFY(panel.findChild<QLabel*>("assistantProbeStatus")->text().isEmpty());
        panel.findChild<QPushButton*>("assistantRecheck")->click(); probe.complete(probe.generation);
        QVERIFY(panel.findChild<QLabel*>("assistantProbeStatus")->text().contains("0.160.1"));
        QCOMPARE(h.transport.sent.size(), 0); QCOMPARE(h.controller.configuration().executablePath, QString{});
    }
    void statusRefreshPreservesModelCatalogAndDraftSelection() {
        Harness h; QVERIFY(h.readyController()); SetupProbe probe;
        AssistantSettingsPanel panel(h.controller, probe, {});
        auto* model = panel.modelControl();
        const auto index = model->findData("fixture-model"); QVERIFY(index >= 0);
        model->setCurrentIndex(index);
        QSignalSpy removed(model->model(), &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(model->model(), &QAbstractItemModel::rowsInserted);
        for (int i = 0; i < 20; ++i) emit h.controller.stateChanged();
        QCOMPARE(removed.size(), 0); QCOMPARE(inserted.size(), 0);
        QCOMPARE(model->currentData().toString(), QString("fixture-model"));
    }
    void invalidStoredOverrideDoesNotBecomePathDiscovery() {
        SettingsRepository repository; repository.values.insert("assistant/executable", "C:/codex.cmd");
        SettingsService settings(repository); QVERIFY(!settings.value("assistant/executable").toString().isEmpty());
        Harness h; h.controller.applyConfiguration({settings.value("assistant/executable").toString(), {}});
        QSignalSpy probes(&h.controller, &AssistantController::executableProbeRequested); h.controller.open();
        QVERIFY(!probes.front().at(0).toString().isEmpty());
    }
    void savedConfigurationUsesOverrideAndDefersActiveTurn() {
        Harness h; QSignalSpy probes(&h.controller, &AssistantController::executableProbeRequested);
        const AssistantConfiguration initial{QString::fromUtf8("C:/Native İ path/codex.exe"), "fixture-model"};
        h.controller.applyConfiguration(initial); QCOMPARE(probes.size(), 0); QVERIFY(h.readyController()); QCOMPARE(probes.front().at(0).toString(), initial.executablePath);
        QVERIFY(h.sendAndAccept()); const auto generation = h.transport.generation; const auto count = probes.size();
        h.controller.applyConfiguration({"C:/New native/codex.exe", "fixture-model"}); QCOMPARE(probes.size(), count); QCOMPARE(h.transport.generation, generation);
        QVERIFY(!h.controller.canSend()); QVERIFY(h.controller.statusText().contains("active operation"));
        h.finish(); QTRY_VERIFY(probes.size() > count); QCOMPARE(probes.back().at(0).toString(), QStringLiteral("C:/New native/codex.exe"));
        QCOMPARE(h.controller.messages().size(), 1); QVERIFY(h.transport.generation > generation);
    }
    void settingsSaveHookOnlyAfterSuccess() {
        Harness h; SetupProbe probe; SettingsRepository repository; SettingsService settings(repository);
        SettingsWindow dialog(settings, *m_theme, {}, nullptr, &h.controller, &probe); QSignalSpy applied(&dialog, &SettingsWindow::settingsApplied);
        connect(&dialog, &SettingsWindow::settingsApplied, &h.controller, [&] { h.controller.applyConfiguration({settings.value("assistant/executable").toString(), settings.value("assistant/model").toString()}); });
        auto* edit = dialog.findChild<QLineEdit*>("assistantExecutable"); QVERIFY(edit); edit->setText("C:/Native path/codex.exe");
        auto* save = dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save);
        repository.failBatchAfterWrites = 1; save->click();
        QTRY_VERIFY_WITH_TIMEOUT(QApplication::activeModalWidget() != nullptr, 1000);
        auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); QVERIFY(message); message->accept();
        QCOMPARE(applied.size(), 0); QCOMPARE(h.controller.configuration().executablePath, QString{});
        QVERIFY(repository.lastBatchWrites > 1); QVERIFY(repository.values.isEmpty());
        repository.failBatchAfterWrites = -1; save->click(); QCOMPARE(applied.size(), 1); QCOMPARE(h.controller.configuration().executablePath, edit->text());
        for (auto i = repository.values.cbegin(); i != repository.values.cend(); ++i) QVERIFY(!i.key().contains("login") && !i.key().contains("token") && !i.key().contains("account") && !i.key().contains("runtime"));
    }
    void cancelSettingsDoesNotApplyDraft() {
        Harness h; SetupProbe probe; SettingsRepository repository; SettingsService settings(repository);
        SettingsWindow dialog(settings, *m_theme, {}, nullptr, &h.controller, &probe); QSignalSpy applied(&dialog, &SettingsWindow::settingsApplied);
        dialog.findChild<QLineEdit*>("assistantExecutable")->setText("C:/Unsaved/codex.exe"); dialog.reject(); QCOMPARE(applied.size(), 0); QVERIFY(repository.values.isEmpty());
    }
    void browserAndDeviceChallenge_data() { QTest::addColumn<bool>("device"); QTest::newRow("browser") << false; QTest::newRow("device") << true; }
    void browserAndDeviceChallenge() {
        QFETCH(bool, device); Harness h; QVERIFY(h.readyController()); SetupProbe probe; QList<QUrl> opened;
        AssistantSettingsPanel panel(h.controller, probe, {}, nullptr, [&](const QUrl& url) { opened.append(url); return true; });
        panel.findChild<QPushButton*>(device ? "assistantConnectDevice" : "assistantConnectBrowser")->click(); QVERIFY(h.controller.loginActive()); QVERIFY(!h.controller.canSend());
        h.reply("account/login/start", device ? QJsonObject{{"type", "chatgptDeviceCode"}, {"loginId", "login-1"}, {"verificationUrl", "https://auth.openai.com/codex/device"}, {"userCode", "FAKE-1234"}}
            : QJsonObject{{"type", "chatgpt"}, {"loginId", "login-1"}, {"authUrl", "https://auth.openai.com/oauth/authorize?state=fixture"}});
        QCOMPARE(opened.size(), 0); QVERIFY(!panel.findChild<QLineEdit*>("assistantAuthorizationUrl")->text().isEmpty());
        QCOMPARE(panel.findChild<QLineEdit*>("assistantDeviceCode")->text(), device ? QStringLiteral("FAKE-1234") : QString{});
        panel.findChild<QPushButton*>("assistantOpenAuthorization")->click(); QCOMPARE(opened.size(), 1);
        panel.findChild<QPushButton*>("assistantCancelLogin")->click(); QVERIFY(!h.controller.loginActive());
        QVERIFY(panel.findChild<QLineEdit*>("assistantAuthorizationUrl")->text().isEmpty()); QVERIFY(panel.findChild<QLineEdit*>("assistantDeviceCode")->text().isEmpty());
        QCOMPARE(h.transport.lastRequest("account/login/cancel").value("params").toObject().value("loginId").toString(), QStringLiteral("login-1"));
        h.transport.event("account/login/completed", {{"loginId", "login-1"}, {"success", true}}); QVERIFY(!h.controller.accountChallenge()); QCOMPARE(opened.size(), 1);
    }
    void invalidAuthorizationNeverOpens() {
        Harness h; QVERIFY(h.readyController()); SetupProbe probe; int opened = 0;
        AssistantSettingsPanel panel(h.controller, probe, {}, nullptr, [&](const QUrl&) { ++opened; return true; });
        panel.findChild<QPushButton*>("assistantConnectBrowser")->click();
        h.reply("account/login/start", {{"type", "chatgpt"}, {"loginId", "bad"}, {"authUrl", "https://evil.test/oauth/authorize?secret=fixture"}});
        panel.findChild<QPushButton*>("assistantOpenAuthorization")->click(); QCOMPARE(opened, 0); QVERIFY(!h.controller.accountChallenge());
        QVERIFY(!panel.findChild<QLabel*>("assistantSetupStatus")->text().contains("secret"));
    }
    void closePanelCancelsPendingLogin() {
        Harness h; QVERIFY(h.readyController()); SetupProbe probe;
        auto panel = std::make_unique<AssistantSettingsPanel>(h.controller, probe, AssistantConfiguration{});
        panel->findChild<QPushButton*>("assistantConnectBrowser")->click(); const int stopped = h.transport.stops;
        panel.reset(); QVERIFY(!h.controller.loginActive()); QVERIFY(h.transport.stops > stopped);
    }
    void expiredChallengeClearsTransientFields() {
        CodexClientLimits limits; limits.loginLifetimeMilliseconds = 30; Harness h({}, limits); QVERIFY(h.readyController()); SetupProbe probe;
        AssistantSettingsPanel panel(h.controller, probe, {}); h.controller.connectAccount(CodexLoginMode::DeviceCode);
        h.reply("account/login/start", {{"type", "chatgptDeviceCode"}, {"loginId", "expired"}, {"verificationUrl", "https://auth.openai.com/codex/device"}, {"userCode", "FAKE"}});
        QTRY_VERIFY_WITH_TIMEOUT(!h.controller.loginActive(), 1000); QVERIFY(panel.findChild<QLineEdit*>("assistantDeviceCode")->text().isEmpty());
        QVERIFY(panel.findChild<QLineEdit*>("assistantAuthorizationUrl")->text().isEmpty());
        QVERIFY(!acceptsCodexLoginUrl(QUrl("https://auth.openai.com/oauth/authorize?state=" + QString(8192, QChar('x'))), false));
    }
    void unsavedExecutableCannotStartAuthorization() {
        Harness h; QVERIFY(h.readyController()); SetupProbe probe; AssistantSettingsPanel panel(h.controller, probe, {});
        panel.executableControl()->setText("C:/Unsaved/codex.exe");
        QVERIFY(!panel.findChild<QPushButton*>("assistantConnectBrowser")->isEnabled()); QVERIFY(!panel.findChild<QPushButton*>("assistantSetupReconnect")->isEnabled());
        panel.findChild<QPushButton*>("assistantConnectBrowser")->click(); QVERIFY(h.transport.lastRequest("account/login/start").isEmpty());
    }
    void completedLoginRefreshesAccountAndLockClearsChallenge() {
        Harness h; QVERIFY(h.readyController()); h.controller.connectAccount(CodexLoginMode::Browser);
        h.reply("account/login/start", {{"type", "chatgpt"}, {"loginId", "ok"}, {"authUrl", "https://auth.openai.com/oauth/authorize"}});
        h.transport.event("account/login/completed", {{"loginId", "ok"}, {"success", true}}); QVERIFY(!h.controller.loginActive());
        h.reply("account/read", chatGptAccount()); QVERIFY(h.controller.runtimeText().contains("ChatGPT"));
        h.controller.connectAccount(CodexLoginMode::DeviceCode); h.reply("account/login/start", {{"type", "chatgptDeviceCode"}, {"loginId", "lock"}, {"verificationUrl", "https://auth.openai.com/codex/device"}, {"userCode", "FAKE"}});
        h.controller.setSessionAllowed(false); QVERIFY(!h.controller.accountChallenge()); QVERIFY(!h.controller.canConnectAccount());
    }
    void authAndRuntimeStatesAreDistinct() {
        Harness h; QVERIFY(h.readyController()); QVERIFY(h.controller.runtimeText().contains("0.160.1"));
        QVERIFY(h.client.readAccount()); h.reply("account/read", {{"account", QJsonValue(QJsonValue::Null)}, {"requiresOpenaiAuth", true}}); QVERIFY(h.controller.runtimeText().contains("Signed out"));
        QVERIFY(h.client.readAccount()); h.reply("account/read", {{"account", QJsonObject{{"type", "apiKey"}}}, {"requiresOpenaiAuth", false}}); QVERIFY(h.controller.runtimeText().contains("API key"));
        QVERIFY(h.client.readAccount()); h.transport.deliver({{"id", h.requestId("account/read")}, {"error", QJsonObject{{"code", 401}, {"message", "synthetic expiry"}}}});
        QVERIFY(h.controller.runtimeText().contains("Expired")); QVERIFY(h.controller.canConnectAccount());
    }
    void emptyAndUnavailableModelCatalog() {
        Harness h; QVERIFY(h.readyController()); SetupProbe probe; AssistantSettingsPanel panel(h.controller, probe, {});
        QVERIFY(panel.modelControl()->findData("fixture-model") >= 0); QVERIFY(!h.controller.models().front().entitlementVerified);
        QVERIFY(h.client.listModels()); h.reply("model/list", {{"data", QJsonArray{}}}); QVERIFY(!h.controller.canSend()); QVERIFY(h.controller.statusText().contains("No text model")); QCOMPARE(panel.modelControl()->count(), 1);
        QVERIFY(h.client.listModels()); h.reply("model/list", {{"data", QJsonArray{model("Unavailable")}}}); QVERIFY(!h.controller.canSend());
        const auto index = panel.modelControl()->findData("fixture-model"); QVERIFY(index >= 0); QVERIFY(!(panel.modelControl()->model()->flags(panel.modelControl()->model()->index(index, 0)) & Qt::ItemIsEnabled));
    }
    void unavailablePreferenceHasNoSilentFallback() {
        Harness h; h.controller.applyConfiguration({{}, "missing-model"}); QVERIFY(h.readyController()); QVERIFY(!h.controller.canSend());
        QVERIFY(h.controller.statusText().contains("selected model is unavailable")); QVERIFY(!h.controller.send("no fallback")); QVERIFY(h.transport.lastRequest("thread/start").isEmpty());
    }
    void settingsVisualSample() {
        Harness h; QVERIFY(h.readyController()); SetupProbe probe; SettingsRepository repository; SettingsService settings(repository);
        SettingsWindow dialog(settings, *m_theme, {}, nullptr, &h.controller, &probe); dialog.resize(1000, 820);
        auto* navigation = dialog.findChild<QListWidget*>("settingsNavigation"); QVERIFY(navigation);
        auto* stack = dialog.findChild<QStackedWidget*>("settingsPageStack"); QVERIFY(stack);
        int index = -1; for (int i = 0; i < navigation->count(); ++i) if (navigation->item(i)->text() == "Assistant") index = i;
        QVERIFY(index >= 0); navigation->setCurrentRow(index); QCOMPARE(stack->currentIndex(), index);
        dialog.show(); QTest::qWait(40); QVERIFY(navigation->isVisible());
        auto* panel = dialog.findChild<AssistantSettingsPanel*>(); QVERIFY(panel); QVERIFY(panel->isVisible());
        auto* connectDevice = panel->findChild<QPushButton*>("assistantConnectDevice"); QVERIFY(connectDevice);
        auto* deviceCode = panel->findChild<QLineEdit*>("assistantDeviceCode"); QVERIFY(deviceCode);
        navigation->setFocus(); navigation->setCurrentRow(index - 1); QTest::keyClick(navigation, Qt::Key_Down);
        QCOMPARE(navigation->currentRow(), index); QCOMPARE(stack->currentIndex(), index);
        const auto snapshot = qEnvironmentVariable("DEVHUB_ASSISTANT_SNAPSHOT");
        if (!snapshot.isEmpty()) QVERIFY(dialog.grab().save(snapshot.chopped(4) + "-idle.png"));
        connectDevice->click();
        h.reply("account/login/start", {{"type", "chatgptDeviceCode"}, {"loginId", "visual-fixture"}, {"verificationUrl", "https://auth.openai.com/codex/device"}, {"userCode", "FAKE-1234"}});
        QTest::qWait(20);
        for (auto* button : panel->findChildren<QPushButton*>()) QVERIFY(dialog.rect().contains(QRect(button->mapTo(&dialog, QPoint{}), button->size())));
        QVERIFY(deviceCode->isVisible());
        if (!snapshot.isEmpty()) QVERIFY(dialog.grab().save(snapshot));
    }
};
QTEST_MAIN(AssistantSettingsTest)
#include "assistant_settings_test.moc"
