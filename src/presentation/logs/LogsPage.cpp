#include "LogsPage.h"

#include "../../application/services/ServiceCoordinator.h"
#include "../../application/logs/LogParser.h"
#include "../../infrastructure/logs/FileLogReader.h"

#include <QCheckBox>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace devhub::presentation::logs {
using namespace application::logs;
namespace {
QString serviceSourceId(const QString& serviceId, bool standardError)
{
    return QStringLiteral("service:%1:%2").arg(serviceId, standardError ? QStringLiteral("stderr") : QStringLiteral("stdout"));
}
}

LogsPage::LogsPage(ILogSourceRepository& repository, application::services::ServiceCoordinator& services,
                   QWidget* parent, qsizetype lineLimit, qsizetype byteLimit)
    : QWidget(parent), m_repository(repository), m_services(services),
      m_lineLimit(qMax(qsizetype{100}, lineLimit)), m_byteLimit(qMax(qsizetype{65536}, byteLimit))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    auto* title = new QLabel(tr("Logs"), this);
    QFont titleFont = title->font(); titleFont.setPointSize(titleFont.pointSize() + 4); titleFont.setBold(true); title->setFont(titleFont);
    layout->addWidget(title);
    auto* toolbar = new QHBoxLayout();
    m_sources = new QComboBox(this); m_sources->setAccessibleName(tr("Log source")); m_sources->setMinimumWidth(220);
    auto* add = new QPushButton(tr("Add File…"), this);
    auto* remove = new QPushButton(tr("Remove Source"), this);
    m_filter = new QLineEdit(this); m_filter->setAccessibleName(tr("Filter log lines")); m_filter->setPlaceholderText(tr("Filter log lines")); m_filter->setClearButtonEnabled(true);
    m_regex = new QCheckBox(tr("Regex"), this);
    m_severity = new QComboBox(this);
    m_severity->addItem(tr("All severities"), static_cast<int>(LogSeverity::Unknown));
    m_severity->addItem(tr("Warnings"), static_cast<int>(LogSeverity::Warning));
    m_severity->addItem(tr("Errors"), static_cast<int>(LogSeverity::Error));
    m_severity->addItem(tr("Critical"), static_cast<int>(LogSeverity::Critical));
    toolbar->addWidget(m_sources); toolbar->addWidget(add); toolbar->addWidget(remove); toolbar->addWidget(m_filter, 1);
    toolbar->addWidget(m_regex); toolbar->addWidget(m_severity);
    layout->addLayout(toolbar);
    auto* actions = new QHBoxLayout();
    m_pause = new QCheckBox(tr("Pause display"), this);
    auto* clear = new QPushButton(tr("Clear View"), this);
    auto* copy = new QPushButton(tr("Copy Selected Line"), this);
    auto* exportButton = new QPushButton(tr("Export Visible…"), this);
    auto* findNext = new QPushButton(tr("Find Next"), this);
    auto* findPrevious = new QPushButton(tr("Find Previous"), this);
    actions->addWidget(m_pause); actions->addWidget(clear); actions->addWidget(copy); actions->addWidget(exportButton);
    actions->addStretch(); actions->addWidget(findPrevious); actions->addWidget(findNext);
    layout->addLayout(actions);
    m_model = new LogLineModel(this);
    m_proxy = new LogFilterProxyModel(this); m_proxy->setSourceModel(m_model);
    m_linesView = new QListView(this); m_linesView->setAccessibleName(tr("Filtered log entries")); m_linesView->setModel(m_proxy); m_linesView->setUniformItemSizes(true);
    m_linesView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_linesView->setFont(font());
    layout->addWidget(m_linesView, 1);
    m_status = new QLabel(this); layout->addWidget(m_status);

    m_fileReader = new infrastructure::logs::FileLogReader(this);
    m_filterTimer = new QTimer(this); m_filterTimer->setSingleShot(true); m_filterTimer->setInterval(180);
    connect(m_filterTimer, &QTimer::timeout, this, [this] {
        if (!m_proxy->setTextFilter(m_filter->text(), m_regex->isChecked()))
            m_status->setText(tr("The regular expression is invalid; the previous filter is still active."));
        else m_status->clear();
    });
    connect(m_filter, &QLineEdit::textChanged, m_filterTimer, qOverload<>(&QTimer::start));
    connect(m_regex, &QCheckBox::toggled, m_filterTimer, qOverload<>(&QTimer::start));
    connect(m_severity, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        m_proxy->setSeverityFilter(static_cast<LogSeverity>(m_severity->currentData().toInt()));
    });
    connect(m_sources, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { selectSource(selectedSourceId()); });
    connect(add, &QPushButton::clicked, this, &LogsPage::addFileSource);
    connect(remove, &QPushButton::clicked, this, &LogsPage::removeSelectedSource);
    connect(m_fileReader, &infrastructure::logs::FileLogReader::linesAvailable, this, [this](QVector<QString> lines) {
        const QString id = selectedSourceId();
        if (id.isEmpty()) return;
        QVector<LogLine> parsed; parsed.reserve(lines.size());
        for (auto& line : lines) parsed.append(LogParser::parse(0, std::move(line)));
        appendToSource(id, std::move(parsed));
    });
    connect(m_fileReader, &infrastructure::logs::FileLogReader::sourceMissing, this, [this](bool missing) {
        m_status->setText(missing ? tr("Log file is missing. DevHub will reconnect if it reappears.") : tr("Log file reconnected."));
    });
    connect(m_fileReader, &infrastructure::logs::FileLogReader::sourceRotated, this, [this] { m_status->setText(tr("Log file changed or was truncated; following the current file.")); });
    connect(m_fileReader, &infrastructure::logs::FileLogReader::readFailed, this, [this](const QString& message) { m_status->setText(message); });
    connect(&m_services, &application::services::ServiceCoordinator::outputAvailable,
        this, [this](const QString& id, const QByteArray& bytes, bool standardError) { appendServiceOutput(id, bytes, standardError); });
    connect(&m_services, &application::services::ServiceCoordinator::stateChanged,
        this, [this](const QString&, const application::services::ServiceRuntimeState&) { reloadSources(); });
    connect(clear, &QPushButton::clicked, this, [this] {
        m_buffers[selectedSourceId()].clear(); m_model->setLines({}); m_clearedView = true;
        m_status->setText(tr("View cleared. The source file was not changed."));
    });
    connect(m_pause, &QCheckBox::toggled, this, [this](bool paused) { if (!paused) { m_clearedView = false; refreshDisplay(); } });
    connect(copy, &QPushButton::clicked, this, [this] {
        const QModelIndex current = m_linesView->currentIndex();
        if (!current.isValid()) return;
        const QModelIndex source = m_proxy->mapToSource(current);
        if (const auto* line = m_model->lineAt(source.row())) QApplication::clipboard()->setText(line->text);
    });
    connect(exportButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Export visible log lines"), QString(), tr("Text files (*.txt);;All files (*)"));
        if (path.isEmpty()) return;
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) { QMessageBox::warning(this, tr("Export failed"), file.errorString()); return; }
        for (int row = 0; row < m_proxy->rowCount(); ++row) {
            const auto* line = m_model->lineAt(m_proxy->mapToSource(m_proxy->index(row, 0)).row());
            if (line) file.write((line->text + QLatin1Char('\n')).toUtf8());
        }
        if (!file.commit()) QMessageBox::warning(this, tr("Export failed"), file.errorString());
    });
    const auto find = [this](bool forward) {
        if (m_filter->text().isEmpty() || m_proxy->rowCount() == 0) return;
        int row = m_linesView->currentIndex().row();
        row = (row + (forward ? 1 : -1) + m_proxy->rowCount()) % m_proxy->rowCount();
        const QModelIndex index = m_proxy->index(row, 0); m_linesView->setCurrentIndex(index); m_linesView->scrollTo(index);
    };
    connect(findNext, &QPushButton::clicked, this, [find] { find(true); });
    connect(findPrevious, &QPushButton::clicked, this, [find] { find(false); });
}

void LogsPage::setWorkspace(QString workspaceId, QString workspacePath)
{
    m_workspaceId = std::move(workspaceId); m_workspacePath = std::move(workspacePath); reloadSources();
}

void LogsPage::focusService(const QString& serviceId)
{
    if (serviceId.isEmpty()) return;
    reloadSources();
    const int index = m_sources->findData(serviceSourceId(serviceId, false));
    if (index >= 0) {
        m_sources->setCurrentIndex(index);
        selectSource(selectedSourceId());
    } else {
        m_status->setText(tr("No managed-service log source is available yet. Start the linked service to collect output."));
    }
}

void LogsPage::reloadSources()
{
    const QString previous = selectedSourceId();
    m_sources->blockSignals(true); m_sources->clear();
    m_fileSources.clear(); m_serviceNames.clear();
    if (!m_workspaceId.isEmpty()) {
        auto loaded = m_repository.list(m_workspaceId);
        if (loaded) {
            m_fileSources = std::move(loaded).value();
            for (const auto& source : m_fileSources) m_sources->addItem(source.displayName, source.id);
        } else m_status->setText(loaded.error().userMessage);
        for (const auto& state : m_services.states()) {
            if (state.definition.workspaceId != m_workspaceId) continue;
            m_serviceNames.insert(state.definition.id, state.definition.name);
            m_sources->addItem(tr("%1 — stdout").arg(state.definition.name), serviceSourceId(state.definition.id, false));
            m_sources->addItem(tr("%1 — stderr").arg(state.definition.name), serviceSourceId(state.definition.id, true));
        }
    }
    int selected = m_sources->findData(previous);
    if (selected < 0 && m_sources->count() > 0) selected = 0;
    m_sources->setCurrentIndex(selected); m_sources->blockSignals(false);
    if (selected >= 0) selectSource(selectedSourceId());
    else { m_activeReaderSourceId.clear(); m_fileReader->stop(); m_model->setLines({}); }
}

void LogsPage::selectSource(const QString& sourceId)
{
    if (sourceId.isEmpty()) return;
    m_clearedView = false;
    if (m_activeReaderSourceId == sourceId) { refreshDisplay(); return; }
    m_activeReaderSourceId = sourceId;
    const auto file = std::find_if(m_fileSources.cbegin(), m_fileSources.cend(), [&sourceId](const LogSourceConfig& row) { return row.id == sourceId; });
    if (file != m_fileSources.cend()) m_fileReader->start(file->filePath);
    else m_fileReader->stop();
    refreshDisplay();
}

void LogsPage::addFileSource()
{
    if (m_workspaceId.isEmpty()) { m_status->setText(tr("Select a workspace before adding a log file.")); return; }
    const QString path = QFileDialog::getOpenFileName(this, tr("Select log file"), m_workspacePath, tr("Log files (*.log *.txt);;All files (*)"));
    if (path.isEmpty()) return;
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("Add log source"), tr("Display name"), QLineEdit::Normal, QFileInfo(path).fileName(), &accepted);
    if (!accepted) return;
    LogSourceConfig source; source.workspaceId = m_workspaceId; source.displayName = name; source.filePath = path;
    const auto result = m_repository.add(source);
    if (!result) { QMessageBox::warning(this, tr("Could not add log source"), result.error().userMessage); return; }
    reloadSources();
    m_sources->setCurrentIndex(m_sources->findText(name));
    selectSource(selectedSourceId());
}

void LogsPage::removeSelectedSource()
{
    const QString id = selectedSourceId();
    const auto file = std::find_if(m_fileSources.cbegin(), m_fileSources.cend(), [&id](const LogSourceConfig& row) { return row.id == id; });
    if (file == m_fileSources.cend()) { m_status->setText(tr("Select a saved file source to remove.")); return; }
    if (QMessageBox::question(this, tr("Remove log source"), tr("Remove this source from DevHub? The log file will not be deleted.")) != QMessageBox::Yes) return;
    const auto result = m_repository.remove(id, m_workspaceId);
    if (!result) { QMessageBox::warning(this, tr("Could not remove log source"), result.error().userMessage); return; }
    m_fileReader->stop(); m_buffers.remove(id); reloadSources();
}

void LogsPage::appendToSource(const QString& sourceId, QVector<LogLine> lines)
{
    if (lines.isEmpty()) return;
    auto& buffer = m_buffers[sourceId];
    buffer.setLimits(m_lineLimit, m_byteLimit);
    const qsizetype dropped = buffer.append(lines);
    if (m_pause->isChecked() || m_clearedView || sourceId != selectedSourceId()) return;
    if (dropped > 0) refreshDisplay();
    else { m_model->appendLines(std::move(lines)); m_linesView->scrollToBottom(); }
    if (m_buffers[sourceId].droppedLineCount() > 0) m_status->setText(tr("Older lines were removed to keep memory bounded (%1 lines retained).")
        .arg(m_buffers[sourceId].lines().size()));
}

void LogsPage::appendServiceOutput(const QString& serviceId, const QByteArray& bytes, bool standardError)
{
    const QString sourceId = serviceSourceId(serviceId, standardError);
    auto& state = m_streams[sourceId];
    if (!state) state = std::make_shared<StreamState>();
    state->partial.append(state->decoder.decode(bytes));
    if (state->partial.size() > 1024 * 1024) state->partial = state->partial.right(1024 * 1024);
    QVector<LogLine> lines;
    qsizetype consumed = 0;
    for (qsizetype i = 0; i < state->partial.size(); ++i) {
        if (state->partial.at(i) != QLatin1Char('\n')) continue;
        QString text = state->partial.mid(consumed, i - consumed);
        if (text.endsWith(QLatin1Char('\r'))) text.chop(1);
        lines.append(LogParser::parse(0, std::move(text))); consumed = i + 1;
    }
    if (consumed > 0) state->partial.remove(0, consumed);
    appendToSource(sourceId, std::move(lines));
}

void LogsPage::refreshDisplay()
{
    if (m_pause->isChecked() || m_clearedView) return;
    m_model->setLines(m_buffers.value(selectedSourceId()).lines());
    if (m_model->rowCount() > 0) m_linesView->scrollToBottom();
}

QString LogsPage::selectedSourceId() const { return m_sources->currentData().toString(); }

} // namespace devhub::presentation::logs
