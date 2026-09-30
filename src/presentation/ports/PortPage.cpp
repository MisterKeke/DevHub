#include "PortPage.h"

#include "../../application/services/ServiceCoordinator.h"
#include "../../application/workspaces/WorkspaceManager.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

namespace devhub::presentation::ports {
using namespace application::ports;

PortPage::PortPage(PortMonitorService& monitor, application::services::ServiceCoordinator& services,
                   application::workspaces::WorkspaceManager& workspaces, QWidget* parent)
    : QWidget(parent), m_monitor(monitor), m_services(services), m_workspaces(workspaces)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    auto* title = new QLabel(tr("Local Ports"), this);
    QFont titleFont = title->font(); titleFont.setPointSize(titleFont.pointSize() + 4); titleFont.setBold(true); title->setFont(titleFont);
    layout->addWidget(title);
    auto* toolbar = new QHBoxLayout();
    m_search = new QLineEdit(this); m_search->setAccessibleName(tr("Search ports and processes")); m_search->setPlaceholderText(tr("Search port, process, PID, or address")); m_search->setClearButtonEnabled(true);
    m_endpointFilter = new QComboBox(this);
    m_endpointFilter->setAccessibleName(tr("Endpoint type filter"));
    m_endpointFilter->addItem(tr("All endpoints"), 0); m_endpointFilter->addItem(tr("TCP"), 1);
    m_endpointFilter->addItem(tr("UDP"), 2); m_endpointFilter->addItem(tr("Listening TCP"), 3);
    auto* refresh = new QPushButton(tr("Refresh"), this);
    auto* copyPort = new QPushButton(tr("Copy Port"), this);
    auto* copyPid = new QPushButton(tr("Copy PID"), this);
    auto* openWorkspace = new QPushButton(tr("Open Associated Workspace"), this);
    auto* terminate = new QPushButton(tr("Terminate Process…"), this);
    auto* interval = new QComboBox(this);
    interval->setAccessibleName(tr("Port refresh interval"));
    interval->addItem(tr("Manual refresh"), 0); interval->addItem(tr("Every 5 seconds"), 5000); interval->addItem(tr("Every 15 seconds"), 15000);
    toolbar->addWidget(m_search, 1); toolbar->addWidget(m_endpointFilter); toolbar->addWidget(interval); toolbar->addWidget(refresh); toolbar->addWidget(copyPort);
    toolbar->addWidget(copyPid); toolbar->addWidget(openWorkspace); toolbar->addWidget(terminate);
    layout->addLayout(toolbar);
    m_model = new PortTableModel(this);
    m_proxy = new PortFilterProxyModel(this); m_proxy->setSourceModel(m_model);
    m_table = new QTableView(this); m_table->setModel(m_proxy); m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAccessibleName(tr("Local port and process list"));
    m_table->setSelectionMode(QAbstractItemView::SingleSelection); m_table->setSortingEnabled(true); m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setStretchLastSection(true); m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    layout->addWidget(m_table, 1);
    m_status = new QLabel(this);
    m_status->setAccessibleName(tr("Port monitor status"));
    m_status->setStyleSheet(QStringLiteral("color: palette(mid);"));
    layout->addWidget(m_status);
    m_refreshTimer = new QTimer(this); m_refreshTimer->setInterval(5000);
    connect(m_refreshTimer, &QTimer::timeout, this, [this] { (void)m_monitor.refresh(); });
    connect(interval, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, interval](int) {
        const int ms = interval->currentData().toInt();
        if (ms <= 0) m_refreshTimer->stop(); else { m_refreshTimer->setInterval(ms); m_refreshTimer->start(); }
    });
    connect(m_search, &QLineEdit::textChanged, m_proxy, &PortFilterProxyModel::setSearchText);
    connect(m_endpointFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        m_proxy->setEndpointFilter(m_endpointFilter->currentData().toInt());
    });
    connect(refresh, &QPushButton::clicked, this, [this] { if (!m_monitor.refresh()) m_status->setText(tr("A refresh is already running.")); });
    connect(copyPort, &QPushButton::clicked, this, [this] { copySelected(true); });
    connect(copyPid, &QPushButton::clicked, this, [this] { copySelected(false); });
    connect(openWorkspace, &QPushButton::clicked, this, [this] {
        const auto* record = selectedRecord();
        if (record && !record->associatedWorkspaceId.isEmpty()) emit openWorkspaceRequested(record->associatedWorkspaceId);
        else m_status->setText(tr("No managed workspace is associated with this process."));
    });
    connect(terminate, &QPushButton::clicked, this, [this] {
        const auto* selected = selectedRecord();
        if (!selected) { m_status->setText(tr("Select a process first.")); return; }
        const PortRecord record = *selected;
        if (record.process.processId <= 4 || record.process.processId == static_cast<quint32>(QCoreApplication::applicationPid())) {
            QMessageBox::warning(this, tr("Protected process"), tr("DevHub will not terminate a protected or current process.")); return;
        }
        QMessageBox confirmation(this); confirmation.setIcon(QMessageBox::Warning); confirmation.setWindowTitle(tr("Terminate process?"));
        confirmation.setText(tr("Terminate %1 (PID %2), which owns local %3 port %4?")
            .arg(record.processName.isEmpty() ? tr("Unknown process") : record.processName)
            .arg(record.process.processId).arg(protocolName(record.protocol)).arg(record.localPort));
        confirmation.setInformativeText(tr("This can interrupt development tools or other applications. DevHub will recheck the process identity before terminating it."));
        confirmation.setStandardButtons(QMessageBox::Cancel | QMessageBox::Yes); confirmation.setDefaultButton(QMessageBox::Cancel);
        if (confirmation.exec() != QMessageBox::Yes) return;
        const auto result = m_monitor.terminate(record);
        if (!result) QMessageBox::warning(this, tr("Could not terminate process"), result.error().userMessage);
        else { m_status->setText(tr("Termination requested.")); QTimer::singleShot(500, this, [this] { (void)m_monitor.refresh(); }); }
    });
    connect(&m_monitor, &PortMonitorService::recordsChanged, this, [this](QVector<PortRecord> records) {
        refreshAssociations(records); m_model->setRecords(std::move(records));
    });
    connect(&m_monitor, &PortMonitorService::refreshStarted, this, [refresh] { refresh->setEnabled(false); refresh->setText(QObject::tr("Refreshing…")); });
    connect(&m_monitor, &PortMonitorService::refreshFinished, this, [this, refresh](int count) {
        refresh->setEnabled(true); refresh->setText(tr("Refresh")); m_status->setText(tr("Found %1 local endpoints.").arg(count));
    });
    connect(&m_monitor, &PortMonitorService::refreshFailed, this, [this, refresh](const QString& message) {
        refresh->setEnabled(true); refresh->setText(tr("Refresh")); m_status->setText(message);
    });
    (void)m_monitor.refresh();
}

const PortRecord* PortPage::selectedRecord() const
{
    const QModelIndex current = m_table->currentIndex();
    if (!current.isValid()) return nullptr;
    return m_model->recordAt(m_proxy->mapToSource(current).row());
}

void PortPage::copySelected(bool copyPort)
{
    const auto* record = selectedRecord();
    if (!record) { m_status->setText(tr("Select a port first.")); return; }
    QApplication::clipboard()->setText(copyPort ? QString::number(record->localPort) : QString::number(record->process.processId));
    m_status->setText(copyPort ? tr("Port copied.") : tr("PID copied."));
}

void PortPage::refreshAssociations(QVector<PortRecord>& records)
{
    QHash<quint32, QString> managed;
    for (const auto& state : m_services.states()) {
        if (state.processId > 0 && (state.lifecycle == application::services::ServiceLifecycleState::Running
            || state.lifecycle == application::services::ServiceLifecycleState::Starting))
            managed.insert(static_cast<quint32>(state.processId), state.definition.workspaceId);
    }
    const auto workspaces = m_workspaces.list();
    QHash<QString, QString> names;
    if (workspaces) for (const auto& workspace : workspaces.value()) names.insert(workspace.id().toString(), workspace.displayName());
    for (auto& record : records) {
        const QString workspaceId = managed.value(record.process.processId);
        if (!workspaceId.isEmpty()) {
            record.associatedWorkspaceId = workspaceId;
            record.associatedWorkspaceName = names.value(workspaceId, workspaceId);
        }
    }
}

} // namespace devhub::presentation::ports
