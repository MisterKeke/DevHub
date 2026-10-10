#include "../src/presentation/notes/NotesPage.h"

#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest>

using namespace devhub;

namespace {
template<typename T> shared::common::Result<T> failure()
{
    return shared::common::Result<T>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::IoFailure, domain::errors::ErrorCategory::FileSystem,
        QStringLiteral("Unavailable"), {}, QStringLiteral("test")));
}
class NotesRepositorySpy final : public application::notes::INotesRepository
{
public:
    explicit NotesRepositorySpy(QString path) : contentPathValue(std::move(path)) {}
    QString contentPathValue;
    QVector<application::notes::NoteNode> nodes;
    shared::common::Result<QVector<application::notes::NoteNode>> list(const QString&, bool) override { return shared::common::Result<QVector<application::notes::NoteNode>>::success(nodes); }
    shared::common::Result<application::notes::NoteNode> create(const QString&, QString, QString, application::notes::NoteNodeKind) override { return failure<application::notes::NoteNode>(); }
    shared::common::Result<void> rename(const QString&, const QString&, QString) override { return shared::common::Result<void>::success(); }
    shared::common::Result<void> move(const QString&, const QString&, QString) override { return shared::common::Result<void>::success(); }
    shared::common::Result<void> setPinned(const QString&, const QString&, bool) override { return shared::common::Result<void>::success(); }
    shared::common::Result<void> setTags(const QString&, const QString&, QStringList) override { return shared::common::Result<void>::success(); }
    shared::common::Result<QString> readContent(const QString&, const QString&) override { return shared::common::Result<QString>::success({}); }
    shared::common::Result<QString> readContentBounded(const QString&, const QString&, qint64) override { return shared::common::Result<QString>::success({}); }
    shared::common::Result<QString> saveContent(const QString&, const QString&, const QString&, const QString&) override { return failure<QString>(); }
    QString contentPath(const QString&, const QString&) const override { return contentPathValue; }
    shared::common::Result<void> setTrashed(const QString&, const QString&, bool) override { return shared::common::Result<void>::success(); }
    shared::common::Result<void> permanentlyRemove(const QString&, const QString&) override { return shared::common::Result<void>::success(); }
};
}

class NotesSearchUiTest final : public QObject
{
    Q_OBJECT
private slots:
    void contentSearchAndTitleFilteringStayStable()
    {
        QTemporaryDir temp(QDir::current().filePath(QStringLiteral("phase7-notes-ui-XXXXXX")));
        QVERIFY(temp.isValid());
        const QString filePath = QDir(temp.path()).filePath(QStringLiteral("note.md"));
        QFile file(filePath); QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("content-only-needle"), qint64(19)); file.close();

        NotesRepositorySpy repository(filePath);
        application::notes::NoteNode note;
        note.id = QStringLiteral("note"); note.workspaceId = QStringLiteral("workspace"); note.title = QStringLiteral("Unrelated title");
        note.kind = application::notes::NoteNodeKind::Note; repository.nodes.append(note);
        presentation::notes::NotesPage page(repository, QStringLiteral("account"), temp.path());
        QVERIFY(page.setWorkspace(QStringLiteral("workspace")));
        auto* tree = page.findChild<QTreeWidget*>(QStringLiteral("notesTree"));
        auto* search = page.findChild<QLineEdit*>(QStringLiteral("notesSearch"));
        QVERIFY(tree && search && tree->topLevelItemCount() == 1);
        auto* item = tree->topLevelItem(0)->child(0); QVERIFY(item);
        search->setText(QStringLiteral("content-only-needle"));
        QTRY_VERIFY_WITH_TIMEOUT(!item->isHidden(), 3000);
        search->setText(QStringLiteral("does-not-match"));
        QTRY_VERIFY_WITH_TIMEOUT(item->isHidden(), 3000);
        search->clear();
        QTRY_VERIFY_WITH_TIMEOUT(!item->isHidden(), 3000);
    }
};

QTEST_MAIN(NotesSearchUiTest)
#include "notes_search_ui_test.moc"
