#pragma once

#include "DatabaseTableModel.h"
#include "../../application/databases/IDatabaseReferenceRepository.h"
#include "../../application/databases/ISqliteConnectionFactory.h"

#include <QWidget>
#include <QStringList>
#include <optional>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QStandardItemModel;
class QTableView;
class QTabWidget;
class QTreeView;

namespace devhub::application::databases { class IDatabaseSession; }

namespace devhub::presentation::databases {

class DatabasePage final : public QWidget
{
    Q_OBJECT
public:
    DatabasePage(application::databases::IDatabaseReferenceRepository& references,
                 application::databases::ISqliteConnectionFactory& factory,
                 QWidget* parent = nullptr, int pageSize = 250,
                 bool queryHistoryEnabled = false, int queryHistoryLimit = 20);
    void setWorkspace(QString workspaceId, QString workspacePath);
    void setQueryHistoryPreferences(bool enabled, int limit);
    [[nodiscard]] int referenceCount() const { return static_cast<int>(m_databaseReferences.size()); }
    [[nodiscard]] QStringList referenceNames() const;

signals:
    void databaseOperationFailed(QString message);

private:
    void reloadReferences();
    void addReference();
    void removeReference();
    void openSelected();
    void closeSession();
    void connectSession();
    void loadSchema(QVector<application::databases::DatabaseSchemaObject> objects);
    void selectTable(const QString& tableName);
    void requestPage(int offset);
    void runQuery(bool script);
    void enableEditMode();
    void leaveEditMode();
    void updateModeUi();
    [[nodiscard]] QString selectedReferenceId() const;

    application::databases::IDatabaseReferenceRepository& m_references;
    application::databases::ISqliteConnectionFactory& m_factory;
    application::databases::IDatabaseSession* m_session = nullptr;
    QString m_workspaceId;
    QString m_workspacePath;
    QVector<application::databases::DatabaseReference> m_databaseReferences;
    std::optional<application::databases::DatabaseReference> m_currentReference;
    QVector<application::databases::DatabaseColumn> m_currentColumns;
    QComboBox* m_databaseCombo = nullptr;
    QLabel* m_modeBadge = nullptr;
    QLabel* m_status = nullptr;
    QTabWidget* m_tabs = nullptr;
    QTreeView* m_schemaTree = nullptr;
    QStandardItemModel* m_schemaModel = nullptr;
    QTableView* m_table = nullptr;
    DatabaseTableModel* m_tableModel = nullptr;
    QPlainTextEdit* m_sqlEditor = nullptr;
    QTableView* m_queryResults = nullptr;
    QStandardItemModel* m_queryModel = nullptr;
    QPushButton* m_editModeButton = nullptr;
    QPushButton* m_addRowButton = nullptr;
    QPushButton* m_deleteRowsButton = nullptr;
    QPushButton* m_commitButton = nullptr;
    QPushButton* m_rollbackButton = nullptr;
    QPushButton* m_previousPage = nullptr;
    QPushButton* m_nextPage = nullptr;
    QComboBox* m_pageSize = nullptr;
    QComboBox* m_queryHistory = nullptr;
    QStringList m_queryHistoryEntries;
    QString m_selectedTable;
    QString m_sortColumn;
    bool m_sortAscending = true;
    bool m_sessionOpen = false;
    bool m_readWrite = false;
    bool m_withoutRowId = false;
    bool m_queryHistoryEnabled = false;
    int m_queryHistoryLimit = 20;
};

} // namespace devhub::presentation::databases
