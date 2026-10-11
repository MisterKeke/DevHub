#include "DatabasePage.h"

#include "../../application/databases/IDatabaseSession.h"

#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPersistentModelIndex>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QStandardItemModel>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTableView>
#include <QTreeView>
#include <QVBoxLayout>

namespace devhub::presentation::databases {

QStringList DatabasePage::referenceNames() const
{
    QStringList names;
    for (const auto& reference : m_databaseReferences) names.append(reference.displayName);
    names.sort(Qt::CaseInsensitive);
    return names;
}
using namespace application::databases;
namespace {
constexpr int ReferenceIdRole = Qt::UserRole + 1;
constexpr int ObjectNameRole = Qt::UserRole + 2;
constexpr int ObjectKindRole = Qt::UserRole + 3;

bool appearsReadOnly(const QString& sql)
{
    QString statement = sql.trimmed();
    while (statement.startsWith(QLatin1Char('-')) || statement.startsWith(QLatin1Char('/'))) {
        if (statement.startsWith(QStringLiteral("--"))) {
            const qsizetype end = statement.indexOf(QLatin1Char('\n'));
            if (end < 0) return true;
            statement.remove(0, end + 1);
        } else if (statement.startsWith(QStringLiteral("/*"))) {
            const qsizetype end = statement.indexOf(QStringLiteral("*/"), 2);
            if (end < 0) return false;
            statement.remove(0, end + 2);
        } else break;
        statement = statement.trimmed();
    }
    const QString first = statement.section(QRegularExpression(QStringLiteral("\\s+|;")), 0, 0).toUpper();
    return first == QStringLiteral("SELECT") || first == QStringLiteral("EXPLAIN");
}
}

DatabasePage::DatabasePage(IDatabaseReferenceRepository& references, ISqliteConnectionFactory& factory,
                           QWidget* parent, int pageSize, bool queryHistoryEnabled, int queryHistoryLimit)
    : QWidget(parent), m_references(references), m_factory(factory),
      m_queryHistoryEnabled(queryHistoryEnabled), m_queryHistoryLimit(qMax(1, queryHistoryLimit))
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 20);
    auto* toolbar = new QHBoxLayout();
    m_databaseCombo = new QComboBox(this);
    m_databaseCombo->setObjectName(QStringLiteral("databaseReferenceCombo"));
    m_databaseCombo->setAccessibleName(tr("Registered databases"));
    m_databaseCombo->setMinimumWidth(240);
    auto* add = new QPushButton(tr("Add Database…"), this);
    auto* remove = new QPushButton(tr("Remove Reference"), this);
    auto* open = new QPushButton(tr("Open Read-Only"), this);
    auto* close = new QPushButton(tr("Close"), this);
    m_modeBadge = new QLabel(tr("Closed"), this);
    m_modeBadge->setObjectName(QStringLiteral("databaseModeBadge"));
    toolbar->addWidget(m_databaseCombo, 1);
    toolbar->addWidget(add);
    toolbar->addWidget(remove);
    toolbar->addWidget(open);
    toolbar->addWidget(close);
    toolbar->addWidget(m_modeBadge);
    root->addLayout(toolbar);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("databaseTabs"));
    auto* schemaTab = new QWidget(m_tabs);
    auto* schemaLayout = new QVBoxLayout(schemaTab);
    auto* schemaSplit = new QSplitter(Qt::Horizontal, schemaTab);
    m_schemaModel = new QStandardItemModel(schemaSplit);
    m_schemaTree = new QTreeView(schemaSplit);
    m_schemaTree->setModel(m_schemaModel);
    m_schemaTree->setHeaderHidden(true);
    m_schemaTree->setAccessibleName(tr("Database schema objects"));
    m_schemaTree->setMinimumWidth(200);
    auto* dataPane = new QWidget(schemaSplit);
    auto* dataLayout = new QVBoxLayout(dataPane);
    dataLayout->setContentsMargins(0, 0, 0, 0);
    auto* dataToolbar = new QHBoxLayout();
    m_pageSize = new QComboBox(dataPane);
    for (const int size : {25, 50, 100, 250, 500}) m_pageSize->addItem(QString::number(size), size);
    const int pageSizeIndex = m_pageSize->findData(pageSize);
    m_pageSize->setCurrentIndex(pageSizeIndex >= 0 ? pageSizeIndex : 1);
    m_pageSizeIndex = m_pageSize->currentIndex();
    m_pageSize->setAccessibleName(tr("Rows per page"));
    m_previousPage = new QPushButton(tr("Previous"), dataPane);
    m_nextPage = new QPushButton(tr("Next"), dataPane);
    auto* count = new QPushButton(tr("Count Rows"), dataPane);
    m_addRowButton = new QPushButton(tr("Add Row"), dataPane);
    m_deleteRowsButton = new QPushButton(tr("Delete Selected"), dataPane);
    m_editModeButton = new QPushButton(tr("Enable Edit Mode…"), dataPane);
    m_commitButton = new QPushButton(tr("Commit Changes"), dataPane);
    m_rollbackButton = new QPushButton(tr("Roll Back Pending"), dataPane);
    dataToolbar->addWidget(new QLabel(tr("Page size"), dataPane));
    dataToolbar->addWidget(m_pageSize);
    dataToolbar->addWidget(m_previousPage);
    dataToolbar->addWidget(m_nextPage);
    dataToolbar->addWidget(count);
    dataToolbar->addStretch();
    dataToolbar->addWidget(m_addRowButton);
    dataToolbar->addWidget(m_deleteRowsButton);
    dataToolbar->addWidget(m_editModeButton);
    dataToolbar->addWidget(m_commitButton);
    dataToolbar->addWidget(m_rollbackButton);
    dataLayout->addLayout(dataToolbar);
    m_tableModel = new DatabaseTableModel(this);
    m_table = new QTableView(dataPane);
    m_table->setObjectName(QStringLiteral("databaseDataTable"));
    m_table->setModel(m_tableModel);
    m_table->setAccessibleName(tr("Database table rows"));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSortingEnabled(false);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        const QPersistentModelIndex cell(m_table->indexAt(position));
        if (!cell.isValid()) return;
        QMenu menu(m_table);
        auto* setNull = menu.addAction(tr("Set Cell to SQL NULL"));
        setNull->setEnabled(!m_commitPending && (m_tableModel->flags(cell) & Qt::ItemIsEditable));
        if (menu.exec(m_table->viewport()->mapToGlobal(position)) == setNull && !m_commitPending)
            m_tableModel->setData(cell, QVariant(QMetaType(QMetaType::QString)), Qt::EditRole);
    });
    dataLayout->addWidget(m_table, 1);
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { updateModeUi(); });
    connect(m_tableModel, &QAbstractItemModel::dataChanged, this, [this] { updateModeUi(); });
    schemaSplit->addWidget(m_schemaTree);
    schemaSplit->addWidget(dataPane);
    schemaSplit->setStretchFactor(1, 1);
    schemaLayout->addWidget(schemaSplit);
    m_tabs->addTab(schemaTab, tr("Schema & Data"));

    auto* queryTab = new QWidget(m_tabs);
    auto* queryLayout = new QVBoxLayout(queryTab);
    auto* queryActions = new QHBoxLayout();
    auto* run = new QPushButton(tr("Run Statement"), queryTab);
    auto* runScript = new QPushButton(tr("Run Script…"), queryTab);
    auto* cancelQuery = new QPushButton(tr("Cancel SQL"), queryTab);
    m_queryHistory = new QComboBox(queryTab);
    m_queryHistory->setObjectName(QStringLiteral("databaseQueryHistory"));
    m_queryHistory->setAccessibleName(tr("Session query history"));
    m_queryHistory->setMinimumWidth(220);
    m_queryHistory->setPlaceholderText(tr("Recent SQL (this session only)"));
    m_queryHistory->setVisible(m_queryHistoryEnabled);
    auto* clearHistory = new QPushButton(tr("Clear History"), queryTab);
    queryActions->addWidget(run);
    queryActions->addWidget(runScript);
    queryActions->addWidget(cancelQuery);
    queryActions->addWidget(m_queryHistory, 1);
    queryActions->addWidget(clearHistory);
    queryActions->addStretch();
    queryLayout->addLayout(queryActions);
    m_sqlEditor = new QPlainTextEdit(queryTab);
    m_sqlEditor->setObjectName(QStringLiteral("databaseSqlEditor"));
    m_sqlEditor->setAccessibleName(tr("SQL statement editor"));
    m_sqlEditor->setPlaceholderText(tr("Enter a SQL statement. Results are capped to keep the UI responsive."));
    m_sqlEditor->setMaximumBlockCount(10000);
    queryLayout->addWidget(m_sqlEditor, 1);
    m_queryModel = new QStandardItemModel(this);
    m_queryResults = new QTableView(queryTab);
    m_queryResults->setModel(m_queryModel);
    m_queryResults->setAccessibleName(tr("SQL query results"));
    m_queryResults->horizontalHeader()->setStretchLastSection(true);
    m_queryResults->setMinimumHeight(180);
    queryLayout->addWidget(m_queryResults, 1);
    m_tabs->addTab(queryTab, tr("SQL Query"));
    root->addWidget(m_tabs, 1);
    m_status = new QLabel(tr("Open a registered SQLite database to inspect its schema."), this);
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    connect(add, &QPushButton::clicked, this, &DatabasePage::addReference);
    connect(remove, &QPushButton::clicked, this, &DatabasePage::removeReference);
    connect(open, &QPushButton::clicked, this, &DatabasePage::openSelected);
    connect(close, &QPushButton::clicked, this, [this] { if (confirmDiscardPendingChanges()) closeSession(); });
    connect(m_databaseCombo, &QComboBox::currentIndexChanged, this, [this] { updateModeUi(); });
    connect(m_schemaTree, &QTreeView::clicked, this, [this](const QModelIndex& index) {
        if (index.data(ObjectKindRole).toInt() == static_cast<int>(DatabaseObjectKind::Table))
            selectTable(index.data(ObjectNameRole).toString());
    });
    connect(m_previousPage, &QPushButton::clicked, this, [this] {
        requestPage(qMax(0, m_tableModel->pageOffset() - m_pageSize->currentData().toInt()));
    });
    connect(m_nextPage, &QPushButton::clicked, this, [this] {
        requestPage(m_tableModel->nextPageOffset());
    });
    connect(m_pageSize, &QComboBox::currentIndexChanged, this, [this] {
        if (!confirmDiscardPendingChanges()) {
            const QSignalBlocker blocker(m_pageSize);
            m_pageSize->setCurrentIndex(m_pageSizeIndex);
            return;
        }
        m_pageSizeIndex = m_pageSize->currentIndex();
        if (!m_selectedTable.isEmpty()) requestPage(0, true);
    });
    connect(count, &QPushButton::clicked, this, [this] { if (m_session && m_sessionOpen && !m_selectedTable.isEmpty()) m_session->requestCountRows(m_selectedTable); });
    connect(m_addRowButton, &QPushButton::clicked, this, [this] { m_tableModel->appendBlankRow(); updateModeUi(); });
    connect(m_deleteRowsButton, &QPushButton::clicked, this, [this] {
        const QModelIndexList selected = m_table->selectionModel()->selectedRows();
        if (selected.isEmpty()) return;
        QString preview;
        for (const auto& index : selected.mid(0, 5)) {
            QStringList identity;
            for (int column = 0; column < m_tableModel->columnCount(); ++column)
                if (m_tableModel->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString().contains(QStringLiteral("🔑")))
                    identity.append(m_tableModel->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString()
                        + QStringLiteral("=") + m_tableModel->data(m_tableModel->index(index.row(), column), Qt::DisplayRole).toString());
            preview += tr("Row %1: %2\n").arg(index.row() + m_tableModel->pageOffset() + 1)
                .arg(identity.isEmpty() ? tr("primary key unavailable") : identity.join(QStringLiteral(", ")));
        }
        if (selected.size() > 5) preview += tr("… and %1 more row(s)\n").arg(selected.size() - 5);
        if (QMessageBox::warning(this, tr("Delete selected rows?"),
                tr("These rows will be deleted on Commit Changes.\n\n%1").arg(preview),
                QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes) return;
        m_tableModel->deleteRows(selected);
        updateModeUi();
    });
    connect(m_editModeButton, &QPushButton::clicked, this, [this] {
        if (m_tableModel->hasEditMode()) {
            if (!confirmDiscardPendingChanges()) return;
            m_tableModel->setEditMode(false);
            if (!m_selectedTable.isEmpty()) requestPage(m_tableModel->pageOffset(), true);
            updateModeUi();
        } else enableEditMode();
    });
    connect(m_commitButton, &QPushButton::clicked, this, [this] {
        if (m_session && !m_commitPending && m_tableModel->hasPendingChanges()) {
            m_commitPending = true;
            updateModeUi();
            m_session->requestCommit(m_tableModel->pendingChanges());
        }
    });
    connect(m_rollbackButton, &QPushButton::clicked, this, [this] { leaveEditMode(); });
    connect(run, &QPushButton::clicked, this, [this] { runQuery(false); });
    connect(runScript, &QPushButton::clicked, this, [this] { runQuery(true); });
    connect(cancelQuery, &QPushButton::clicked, this, [this] {
        if (m_session) {
            m_session->cancelQuery();
            m_status->setText(tr("Cancelling SQL operation…"));
        }
    });
    connect(m_queryHistory, &QComboBox::currentTextChanged, this, [this](const QString& sql) {
        if (!sql.isEmpty()) m_sqlEditor->setPlainText(sql);
    });
    connect(clearHistory, &QPushButton::clicked, this, [this] {
        m_queryHistoryEntries.clear(); m_queryHistory->clear();
    });
    updateModeUi();
}

void DatabasePage::setWorkspace(QString workspaceId, QString workspacePath)
{
    if (m_workspaceId != workspaceId) closeSession();
    m_workspaceId = std::move(workspaceId);
    m_workspacePath = std::move(workspacePath);
    reloadReferences();
}

void DatabasePage::setQueryHistoryPreferences(bool enabled, int limit)
{
    m_queryHistoryEnabled = enabled;
    m_queryHistoryLimit = qBound(1, limit, 200);
    m_queryHistory->setVisible(enabled);
    if (!enabled) {
        m_queryHistoryEntries.clear();
        m_queryHistory->clear();
    } else {
        while (m_queryHistoryEntries.size() > m_queryHistoryLimit) m_queryHistoryEntries.removeLast();
    }
}

void DatabasePage::reloadReferences()
{
    m_databaseCombo->clear();
    m_databaseReferences.clear();
    if (m_workspaceId.isEmpty()) { updateModeUi(); return; }
    const auto result = m_references.list(m_workspaceId);
    if (!result) { m_status->setText(result.error().userMessage); updateModeUi(); return; }
    m_databaseReferences = result.value();
    for (const auto& reference : m_databaseReferences) {
        m_databaseCombo->addItem(reference.displayName + (reference.headerVerified ? QString() : tr("  (unverified)")), reference.id);
        m_databaseCombo->setItemData(m_databaseCombo->count() - 1, reference.id, ReferenceIdRole);
    }
    updateModeUi();
}

QString DatabasePage::selectedReferenceId() const { return m_databaseCombo->currentData().toString(); }

void DatabasePage::addReference()
{
    if (m_workspaceId.isEmpty()) { QMessageBox::information(this, tr("Select a workspace"), tr("Choose a workspace before adding a database reference.")); return; }
    const QString path = QFileDialog::getOpenFileName(this, tr("Register SQLite database"), m_workspacePath,
        tr("SQLite databases (*.db *.sqlite *.sqlite3);;All files (*)"));
    if (path.isEmpty()) return;
    DatabaseReference reference;
    reference.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    reference.workspaceId = m_workspaceId;
    reference.displayName = QFileInfo(path).completeBaseName();
    reference.filePath = QFileInfo(path).absoluteFilePath();
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) reference.headerVerified = file.read(16) == QByteArray("SQLite format 3\0", 16);
    if (!reference.headerVerified && QMessageBox::question(this, tr("Unverified SQLite file"),
            tr("This file does not have a standard SQLite header. Register it anyway?")) != QMessageBox::Yes) return;
    const auto saved = m_references.add(reference);
    if (!saved) { QMessageBox::warning(this, tr("Could not register database"), saved.error().userMessage); return; }
    reloadReferences();
    m_databaseCombo->setCurrentIndex(m_databaseCombo->findData(reference.id));
}

void DatabasePage::removeReference()
{
    const QString id = selectedReferenceId();
    if (id.isEmpty()) return;
    if (m_currentReference && m_currentReference->id == id) {
        if (!confirmDiscardPendingChanges()) return;
        closeSession();
    }
    const auto result = m_references.remove(id, m_workspaceId);
    if (!result) { QMessageBox::warning(this, tr("Could not remove reference"), result.error().userMessage); return; }
    reloadReferences();
}

void DatabasePage::openSelected()
{
    const auto it = std::find_if(m_databaseReferences.cbegin(), m_databaseReferences.cend(), [this](const auto& reference) { return reference.id == selectedReferenceId(); });
    if (it == m_databaseReferences.cend()) return;
    if (!confirmDiscardPendingChanges()) return;
    closeSession();
    m_currentReference = *it;
    m_session = m_factory.createSession(this);
    connectSession();
    m_status->setText(tr("Opening %1 read-only…").arg(it->displayName));
    m_session->open(it->filePath, DatabaseOpenMode::ReadOnly);
    updateModeUi();
}

void DatabasePage::connectSession()
{
    connect(m_session, &IDatabaseSession::operationFailed, this, [this](const QString& message) {
        if (m_loadingTable) {
            m_selectedTable = m_tableModel->tableName();
            m_currentColumns = m_tableModel->columnMetadata();
        }
        m_loadingTable = false;
        updateModeUi();
        m_status->setText(message);
        emit databaseOperationFailed(message);
    });
    connect(m_session, &IDatabaseSession::openFinished, this, [this](bool success, const QString& message, DatabaseOpenMode mode) {
        m_sessionOpen = success;
        m_readWrite = success && mode == DatabaseOpenMode::ReadWrite;
        if (m_readWrite) m_tableModel->setEditMode(true);
        m_status->setText(message);
        if (success) m_session->requestSchema();
        updateModeUi();
    });
    connect(m_session, &IDatabaseSession::schemaReady, this, &DatabasePage::loadSchema);
    connect(m_session, &IDatabaseSession::columnsReady, this, [this](const QString& table, const QVector<DatabaseColumn>& columns, bool withoutRowId) {
        if (table != m_selectedTable) return;
        m_currentColumns = columns; m_withoutRowId = withoutRowId; requestPage(0, true);
    });
    connect(m_session, &IDatabaseSession::pageReady, this, [this](const QString& table, const DatabasePageResult& page) {
        if (table != m_selectedTable) return;
        m_tableModel->setPage(table, page, m_currentColumns);
        m_loadingTable = false;
        if (page.primaryKeyColumns.isEmpty())
            m_status->setText(page.withoutRowId
                ? tr("Page loaded. This table has no stable row key; paging can shift when another application changes it.")
                : tr("Page loaded using SQLite rowid. External writes can shift offset-based pages."));
        else m_status->setText(tr("Loaded %1 rows from offset %2.").arg(page.rows.size()).arg(page.offset));
        updateModeUi();
    });
    connect(m_session, &IDatabaseSession::rowCountReady, this, [this](const QString&, qint64 count) { m_status->setText(tr("Row count: %1").arg(count)); });
    connect(m_session, &IDatabaseSession::queryReady, this, [this](const DatabaseQueryResult& result) {
        m_queryModel->clear();
        m_queryModel->setHorizontalHeaderLabels(result.columns);
        for (const auto& row : result.rows) {
            QList<QStandardItem*> items;
            for (const auto& value : row) items.append(new QStandardItem(!value.isValid() || value.isNull() ? QStringLiteral("NULL")
                : value.metaType().id() == QMetaType::QByteArray ? tr("[BLOB, %1 bytes]").arg(value.toByteArray().size()) : value.toString()));
            m_queryModel->appendRow(items);
        }
        m_status->setText(result.message.isEmpty() ? tr("Query completed in %1 ms; %2 rows returned%3.").arg(result.durationMs).arg(result.rows.size()).arg(result.truncated ? tr(" (result capped)") : QString()) : result.message);
    });
    connect(m_session, &IDatabaseSession::backupFinished, this, [this](bool success, const QString& message, const QString&) {
        if (!success) { m_status->setText(message); QMessageBox::warning(this, tr("Backup failed"), message); return; }
        m_status->setText(message);
        connect(m_session, &IDatabaseSession::closed, this, [this] {
            m_sessionOpen = false;
            m_session->open(m_currentReference->filePath, DatabaseOpenMode::ReadWrite);
        }, Qt::SingleShotConnection);
        m_session->close();
    });
    connect(m_session, &IDatabaseSession::commitFinished, this, [this](bool success, const QString& message) {
        m_commitPending = false;
        m_status->setText(message);
        if (success && !m_selectedTable.isEmpty()) {
            m_loadingTable = true;
            m_session->requestSchema(); m_session->requestColumns(m_selectedTable);
        }
        updateModeUi();
    });
}

void DatabasePage::closeSession()
{
    if (m_session) {
        disconnect(m_session, nullptr, this, nullptr);
        m_session->close();
        m_session->deleteLater();
        m_session = nullptr;
    }
    m_sessionOpen = false; m_readWrite = false; m_currentReference.reset(); m_selectedTable.clear();
    m_loadingTable = false; m_commitPending = false;
    m_tableModel->setEditMode(false);
    m_schemaModel->clear(); m_tableModel->setPage({}, {}, {}); m_queryModel->clear();
    m_status->setText(tr("Database connection closed."));
    updateModeUi();
}

void DatabasePage::loadSchema(QVector<DatabaseSchemaObject> objects)
{
    m_schemaModel->clear(); m_schemaModel->setHorizontalHeaderLabels({tr("Database schema")});
    QHash<int, QStandardItem*> groups;
    for (const auto kind : {DatabaseObjectKind::Table, DatabaseObjectKind::View, DatabaseObjectKind::Index, DatabaseObjectKind::Trigger}) {
        auto* group = new QStandardItem(databaseObjectKindName(kind) + QStringLiteral("s"));
        group->setEditable(false); m_schemaModel->appendRow(group); groups.insert(static_cast<int>(kind), group);
    }
    for (const auto& object : objects) {
        auto* item = new QStandardItem(object.name); item->setEditable(false);
        item->setData(object.name, ObjectNameRole); item->setData(static_cast<int>(object.kind), ObjectKindRole);
        item->setToolTip(object.sql);
        if (groups.contains(static_cast<int>(object.kind))) groups.value(static_cast<int>(object.kind))->appendRow(item);
    }
    m_schemaTree->expandToDepth(0);
}

void DatabasePage::selectTable(const QString& tableName)
{
    if (!m_session || !m_sessionOpen || m_loadingTable || !confirmDiscardPendingChanges()) return;
    m_loadingTable = true;
    m_selectedTable = tableName; m_session->requestColumns(tableName);
    updateModeUi();
    m_status->setText(tr("Loading table %1…").arg(tableName));
}

void DatabasePage::requestPage(int offset, bool discardConfirmed)
{
    if (m_session && m_sessionOpen && !m_selectedTable.isEmpty()) {
        if (!discardConfirmed && !confirmDiscardPendingChanges()) return;
        m_loadingTable = true;
        updateModeUi();
        m_session->requestPage(m_selectedTable, offset, m_pageSize->currentData().toInt(), m_sortColumn, m_sortAscending);
    }
}

void DatabasePage::runQuery(bool script)
{
    if (!m_session || !m_sessionOpen) return;
    const QString sql = m_sqlEditor->textCursor().hasSelection() ? m_sqlEditor->textCursor().selectedText() : m_sqlEditor->toPlainText();
    if (sql.trimmed().isEmpty()) return;
    const bool readOnly = appearsReadOnly(sql);
    if (script && QMessageBox::warning(this, tr("Run SQL script"),
            tr("A script can change or delete data and schema. Continue?"), QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes) return;
    if (!script && !readOnly && QMessageBox::warning(this, tr("Potentially mutating SQL"),
            tr("This statement may change data or schema. Continue?"), QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes) return;
    if (m_queryHistoryEnabled) {
        m_queryHistoryEntries.removeAll(sql);
        m_queryHistoryEntries.prepend(sql);
        while (m_queryHistoryEntries.size() > m_queryHistoryLimit) m_queryHistoryEntries.removeLast();
        const QSignalBlocker blocker(m_queryHistory);
        m_queryHistory->clear(); m_queryHistory->addItems(m_queryHistoryEntries); m_queryHistory->setCurrentIndex(0);
    }
    m_session->requestQuery(sql, script);
}

void DatabasePage::enableEditMode()
{
    if (!m_sessionOpen || !m_currentReference || m_selectedTable.isEmpty()) return;
    if (m_withoutRowId && m_tableModel->hasStableIdentity() == false) {
        QMessageBox::information(this, tr("Stable identity required"), tr("This table has no declared primary key. Editing is disabled to prevent ambiguous updates.")); return;
    }
    if (!m_tableModel->hasStableIdentity()) {
        QMessageBox::information(this, tr("Stable identity required"), tr("This table has no declared primary key. Editing is disabled to prevent ambiguous updates.")); return;
    }
    if (m_readWrite) { m_tableModel->setEditMode(true); updateModeUi(); return; }
    const QFileInfo file(m_currentReference->filePath);
    const QString suggestion = file.dir().filePath(file.completeBaseName() + QStringLiteral("-pre-edit-")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")) + QStringLiteral(".sqlite"));
    const QString backup = QFileDialog::getSaveFileName(this, tr("Create pre-edit backup"), suggestion, tr("SQLite database (*.sqlite *.db)"));
    if (backup.isEmpty()) return;
    if (QFileInfo::exists(backup)) { QMessageBox::warning(this, tr("Backup already exists"), tr("Choose a new backup file path; existing files are not overwritten.")); return; }
    m_status->setText(tr("Creating backup before enabling edits…"));
    m_session->requestBackup(backup);
}

void DatabasePage::leaveEditMode()
{
    m_tableModel->setEditMode(false);
    if (m_tableModel->hasPendingChanges()) requestPage(m_tableModel->pageOffset(), true);
    updateModeUi();
}

void DatabasePage::updateModeUi()
{
    const bool busy = m_loadingTable || m_commitPending;
    m_table->setEnabled(!busy);
    const bool canEdit = m_sessionOpen && !busy && !m_selectedTable.isEmpty() && m_tableModel->hasStableIdentity();
    m_modeBadge->setText(!m_sessionOpen ? tr("Closed") : m_readWrite ? tr("Read/Write") : tr("Read-Only"));
    m_editModeButton->setEnabled(canEdit);
    m_editModeButton->setText(m_tableModel->hasEditMode() ? tr("Disable Edit Mode") : tr("Enable Edit Mode…"));
    m_addRowButton->setEnabled(m_tableModel->hasEditMode() && canEdit);
    m_deleteRowsButton->setEnabled(m_tableModel->hasEditMode() && canEdit && !m_table->selectionModel()->selectedRows().isEmpty());
    m_commitButton->setEnabled(m_sessionOpen && !busy && m_readWrite && m_tableModel->hasPendingChanges());
    m_rollbackButton->setEnabled(!busy && m_tableModel->hasPendingChanges());
    m_previousPage->setEnabled(m_sessionOpen && !busy && m_tableModel->pageOffset() > 0);
    m_nextPage->setEnabled(m_sessionOpen && !busy && m_tableModel->hasMore());
    m_pageSize->setEnabled(m_sessionOpen && !busy);
    m_databaseCombo->setEnabled(!m_sessionOpen || !m_currentReference);
}

bool DatabasePage::confirmDiscardPendingChanges()
{
    if (m_commitPending) {
        m_status->setText(tr("Wait for the current commit to finish before leaving this table."));
        return false;
    }
    if (!m_tableModel->hasPendingChanges()) return true;
    return QMessageBox::warning(this, tr("Discard pending database changes?"),
        tr("This action will discard your pending edits, inserts, and deletes. Commit them first to keep them."),
        QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Discard;
}

} // namespace devhub::presentation::databases
