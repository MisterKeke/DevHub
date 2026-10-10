#include "../src/presentation/files/FileBrowserPage.h"

#include <QDir>
#include <QFile>
#include <QFileSystemModel>
#include <QPlainTextEdit>
#include <QSortFilterProxyModel>
#include <QTemporaryDir>
#include <QTreeView>
#include <QtTest>

using namespace devhub;

namespace {
class Trash final : public application::files::ITrashService {
public:
    shared::common::Result<void> moveToTrash(const QString&) override { return shared::common::Result<void>::success(); }
};
class Editor final : public application::files::IExternalEditorLauncher {
public:
    shared::common::Result<void> launch(const QString&, const QString&) override { return shared::common::Result<void>::success(); }
};
}

class FileBrowserUiTest final : public QObject {
    Q_OBJECT
private slots:
    void previewUsesSharedUtf8Reader()
    {
        QTemporaryDir temp(QDir::current().filePath(QStringLiteral("phase7-files-ui-XXXXXX"))); QVERIFY(temp.isValid());
        const QString path = QDir(temp.path()).filePath(QStringLiteral("hello.txt"));
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray expected = QStringLiteral("Hello — Files").toUtf8();
        const QByteArray source = QByteArray::fromHex("efbbbf") + expected;
        QCOMPARE(file.write(source), static_cast<qint64>(source.size()));
        file.close();

        Trash trash; Editor editor;
        presentation::files::FileBrowserPage page(trash, editor);
        page.setWorkspace(QStringLiteral("workspace"), temp.path());
        auto* tree = page.findChild<QTreeView*>(QStringLiteral("workspaceFileTree"));
        auto* preview = page.findChild<QPlainTextEdit*>(QStringLiteral("filePreview"));
        QVERIFY(tree && preview);
        auto* proxy = qobject_cast<QSortFilterProxyModel*>(tree->model());
        QVERIFY(proxy);
        auto* model = qobject_cast<QFileSystemModel*>(proxy->sourceModel());
        QVERIFY(model);
        QTRY_VERIFY_WITH_TIMEOUT(model->index(path).isValid(), 3000);
        const QModelIndex proxyIndex = proxy->mapFromSource(model->index(path));
        QVERIFY(proxyIndex.isValid());
        QVERIFY(QMetaObject::invokeMethod(tree, "clicked", Qt::DirectConnection, Q_ARG(QModelIndex, proxyIndex)));
        QTRY_COMPARE_WITH_TIMEOUT(preview->toPlainText(), QStringLiteral("Hello — Files"), 3000);
    }
};

QTEST_MAIN(FileBrowserUiTest)
#include "file_browser_ui_test.moc"
