#include "RuntimeCheck.h"

#include "../presentation/theme/ThemeManager.h"
#include "../presentation/workspaces/WorkspaceListModel.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace devhub::app
{
namespace
{
// The smoke controllers have the presentation contract but cannot authenticate,
// launch processes, or mutate user data. Populate delegates as well as roots.
class RuntimeAuthFlow final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool accountExists MEMBER exists NOTIFY accountModeChanged)
    Q_PROPERTY(QString username MEMBER user CONSTANT)
    Q_PROPERTY(bool keepSessionDefault MEMBER keep CONSTANT)
    Q_PROPERTY(bool busy MEMBER busyValue CONSTANT)
    Q_PROPERTY(int retryRemaining MEMBER retry CONSTANT)
    Q_PROPERTY(QString message MEMBER text CONSTANT)
  public:
    bool exists = true;
    QString user = QStringLiteral("Runtime check");
    bool keep = false;
    bool busyValue = false;
    int retry = 0;
    QString text;
    Q_INVOKABLE void clearMessage() {}
    Q_INVOKABLE void signIn(QString, QString, bool) {}
    Q_INVOKABLE void createAccount(QString, QString, QString) {}
    Q_INVOKABLE void resetProfile(QString) {}
    Q_INVOKABLE void quit() {}
  signals:
    void accountModeChanged();
};

class RuntimeShell final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString shellWorkspaceTitle MEMBER title CONSTANT)
    Q_PROPERTY(QString shellWorkspacePath MEMBER path CONSTANT)
    Q_PROPERTY(QString shellWorkspaceId MEMBER id CONSTANT)
    Q_PROPERTY(QVariantList overviewSections MEMBER sections CONSTANT)
    Q_PROPERTY(int quickModule MEMBER module CONSTANT)
    Q_PROPERTY(bool workspaceRemovalInProgress READ workspaceRemovalInProgress CONSTANT)
    Q_PROPERTY(QString workspaceActionMessage READ workspaceActionMessage CONSTANT)
    Q_PROPERTY(QString workspaceRemovalWorkspaceId READ workspaceRemovalWorkspaceId CONSTANT)
    Q_PROPERTY(QString workspaceActionTone READ workspaceActionTone CONSTANT)
  public:
    QString title = QStringLiteral("Runtime check");
    QString path = QStringLiteral("C:/RuntimeCheck");
    QString id = QStringLiteral("11111111-1111-4111-8111-111111111111");
    QVariantList sections;
    int module = 0;
    RuntimeShell()
    {
        const QStringList names{QStringLiteral("Git"),      QStringLiteral("Services"),
                                QStringLiteral("Ports"),    QStringLiteral("Logs"),
                                QStringLiteral("Database"), QStringLiteral("Notes")};
        for (qsizetype i = 0; i < names.size(); ++i)
        {
            sections.append(
                QVariantMap{{QStringLiteral("icon"), QStringLiteral("+")},
                            {QStringLiteral("title"), names.at(i)},
                            {QStringLiteral("summary"), QStringLiteral("Runtime available")},
                            {QStringLiteral("status"), QStringLiteral("Ready")},
                            {QStringLiteral("tone"), QStringLiteral("success")},
                            {QStringLiteral("details"), QStringList{QStringLiteral("Smoke test")}},
                            {QStringLiteral("error"), QString()},
                            {QStringLiteral("updated"), QStringLiteral("Current")},
                            {QStringLiteral("loading"), false},
                            {QStringLiteral("action"), QStringLiteral("Open")},
                            {QStringLiteral("module"), static_cast<int>(i + 1)}});
        }
    }
    Q_INVOKABLE void activateQuickWorkspace(QString) {}
    Q_INVOKABLE void selectQuickModule(int) {}
    Q_INVOKABLE void addQuickWorkspace() {}
    Q_INVOKABLE void lockQuickSession() {}
    Q_INVOKABLE void openQuickTerminal() {}
    Q_INVOKABLE void restartQuickServices() {}
    Q_INVOKABLE void openQuickFolder() {}
    Q_INVOKABLE void openQuickSettings() {}
    Q_INVOKABLE void showQuickWorkspaceMenu(QString) {}
    Q_INVOKABLE void manageArchivedWorkspaces() {}
    Q_INVOKABLE void cancelQuickWorkspaceRemoval(QString) {}
    bool workspaceRemovalInProgress() const { return false; }
    QString workspaceActionMessage() const { return {}; }
    QString workspaceRemovalWorkspaceId() const { return {}; }
    QString workspaceActionTone() const { return QStringLiteral("info"); }
};

void polishItems(QQuickItem *item)
{
    item->ensurePolished();
    const auto children = item->childItems();
    for (auto *child : children)
        polishItems(child);
}

void countDelegates(QQuickItem *item, int &workspaceCount, int &overviewCount)
{
    if (!item->property("workspaceId").toString().isEmpty())
        ++workspaceCount;
    if (item->property("modelData").toMap().contains(QStringLiteral("title")))
        ++overviewCount;
    for (auto *child : item->childItems())
        countDelegates(child, workspaceCount, overviewCount);
}
int countAssistantNavigation(QQuickItem* item)
{
    int count = item->property("moduleIndex").isValid() && item->property("moduleIndex").toInt() == 9
        && item->property("modelData").toString() == QStringLiteral("Assistant") ? 1 : 0;
    for (auto* child : item->childItems()) count += countAssistantNavigation(child);
    return count;
}
} // namespace

int checkRuntime(QApplication &application, const QString &reportPath, const QString &qmlRoot)
{
    QStringList diagnostics;
    bool success = true;
    const QString connection =
        QStringLiteral("devhub-runtime-check-%1").arg(QUuid::createUuid().toString());
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(QStringLiteral(":memory:"));
        const bool opened = database.open();
        QSqlQuery query(database);
        if (!opened || !query.exec(QStringLiteral("SELECT 1")) || !query.next() ||
            query.value(0).toInt() != 1)
        {
            diagnostics.append(QStringLiteral("SQLite runtime failed: %1 %2")
                                   .arg(database.lastError().text(), query.lastError().text()));
            success = false;
        }
        else
            diagnostics.append(QStringLiteral("PASS: SQLite in-memory connection and query"));
    }
    QSqlDatabase::removeDatabase(connection);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    presentation::theme::ThemeManager theme(application);
    RuntimeAuthFlow auth;
    RuntimeShell shell;
    presentation::workspaces::WorkspaceListModel workspaces;
    auto workspaceId = domain::workspace::WorkspaceId::fromString(shell.id);
    auto accountId = domain::account::AccountId::fromString(
        QStringLiteral("22222222-2222-4222-8222-222222222222"));
    if (!workspaceId || !accountId)
        return 2;
    workspaces.setWorkspaces({domain::workspace::Workspace(
        workspaceId.value(), accountId.value(), shell.title,
        domain::workspace::WorkspacePath(shell.path, shell.path),
        domain::workspace::WorkspaceStatus::Ready, true, 0, QDateTime::currentDateTimeUtc())});
    const auto source = [&qmlRoot](const QString &file)
    {
        return qmlRoot.isEmpty() ? QUrl(QStringLiteral("qrc:/qt/qml/DevHub/") + file)
                                 : QUrl::fromLocalFile(QDir(qmlRoot).absoluteFilePath(file));
    };
    bool qmlFailed = false;
    {
        QQuickWindow shellWindow;
        QQmlApplicationEngine engine;
        QObject::connect(&engine, &QQmlEngine::warnings, &engine,
                         [&](const QList<QQmlError> &errors)
                         {
                             qmlFailed = true;
                             for (const auto &error : errors)
                                 diagnostics.append(error.toString());
                         });
        auto *context = engine.rootContext();
        context->setContextProperty(QStringLiteral("themeController"), &theme);
        context->setContextProperty(QStringLiteral("authFlow"), &auth);
        context->setContextProperty(QStringLiteral("shellController"), &shell);
        context->setContextProperty(QStringLiteral("workspaceModel"), &workspaces);
        engine.load(source(QStringLiteral("Authentication.qml")));
        if (engine.rootObjects().size() != 1 ||
            !qobject_cast<QQuickWindow *>(engine.rootObjects().value(0)))
        {
            diagnostics.append(QStringLiteral("FAIL: Authentication.qml did not create a window"));
            qmlFailed = true;
        }
        else
        {
            auth.exists = false;
            emit auth.accountModeChanged();
            polishItems(qobject_cast<QQuickWindow *>(engine.rootObjects().first())->contentItem());
            application.processEvents();
            diagnostics.append(
                QStringLiteral("PASS: Authentication.qml sign-in and account-creation views"));
        }
        const qsizetype before = engine.rootObjects().size();
        engine.load(source(QStringLiteral("MainShell.qml")));
        auto *item = engine.rootObjects().size() == before + 1
                         ? qobject_cast<QQuickItem *>(engine.rootObjects().constLast())
                         : nullptr;
        if (!item)
        {
            diagnostics.append(QStringLiteral("FAIL: MainShell.qml did not create an item"));
            qmlFailed = true;
        }
        else
        {
            shellWindow.resize(1160, 820);
            item->setParentItem(shellWindow.contentItem());
            item->setWidth(1160);
            item->setHeight(820);
            polishItems(item);
            application.processEvents();
            polishItems(item);
            int workspaceCount = 0;
            int overviewCount = 0;
            countDelegates(item, workspaceCount, overviewCount);
            if (workspaceCount < 1 || overviewCount != shell.sections.size())
            {
                diagnostics.append(
                    QStringLiteral(
                        "FAIL: MainShell.qml created %1 workspace and %2 overview delegates")
                        .arg(workspaceCount)
                        .arg(overviewCount));
                qmlFailed = true;
            }
            else
                diagnostics.append(
                    QStringLiteral("PASS: MainShell.qml with workspace and overview delegates"));
            if (countAssistantNavigation(item) != 1) {
                diagnostics.append(QStringLiteral("FAIL: Assistant navigation must occupy module index 9 exactly once"));
                qmlFailed = true;
            } else diagnostics.append(QStringLiteral("PASS: Assistant navigation at module index 9"));
        }
    }
    success = success && !qmlFailed;
    diagnostics.append(success ? QStringLiteral("PASS: packaged runtime")
                               : QStringLiteral("FAIL: packaged runtime"));
    const QByteArray output = diagnostics.join(QLatin1Char('\n')).toUtf8() + '\n';
    if (!reportPath.isEmpty())
    {
        QFile report(reportPath);
        if (!report.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
            report.write(output) != output.size())
            return 3;
    }
    if (!success)
        qCritical().noquote() << QString::fromUtf8(output);
    return success ? 0 : 2;
}
} // namespace devhub::app

#include "RuntimeCheck.moc"
