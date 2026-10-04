#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

class RuntimeCheckTest final : public QObject
{
    Q_OBJECT
  private slots:
    void embeddedViewsLoad()
    {
        QTemporaryDir temp;
        const QString report = temp.filePath(QStringLiteral("runtime report.txt"));
        QProcess process;
        process.start(QString::fromUtf8(DEVHUB_TEST_EXECUTABLE),
                      {QStringLiteral("--check-runtime"),
                       QStringLiteral("--check-runtime-report=") + report});
        QVERIFY(process.waitForStarted());
        QVERIFY(process.waitForFinished(20000));
        QFile file(report);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto output = file.readAll();
        QVERIFY2(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
                 output.constData());
        QVERIFY(output.contains("PASS: SQLite"));
        QVERIFY(output.contains("PASS: Authentication.qml"));
        QVERIFY(output.contains("PASS: MainShell.qml"));
        QVERIFY(output.contains("PASS: packaged runtime"));
    }

    void invalidQmlFailsWithReport_data()
    {
        QTest::addColumn<QByteArray>("qml");
        QTest::addColumn<QByteArray>("expected");
        QTest::newRow("missing-import")
            << QByteArray("import QtQuick\nimport Missing.Runtime.Dependency 1.0\nWindow {}\n")
            << QByteArray("Missing.Runtime.Dependency");
        QTest::newRow("binding-error")
            << QByteArray("import QtQuick\nWindow { title: unavailableRuntimeProperty }\n")
            << QByteArray("unavailableRuntimeProperty");
        QTest::newRow("wrong-root") << QByteArray("import QtQuick\nItem {}\n")
                                    << QByteArray("Authentication.qml did not create a window");
    }
    void invalidQmlFailsWithReport()
    {
        QFETCH(QByteArray, qml);
        QFETCH(QByteArray, expected);
        QTemporaryDir temp;
        const QDir source(QString::fromUtf8(DEVHUB_TEST_QML_SOURCE));
        for (const auto &name : source.entryList({QStringLiteral("*.qml")}, QDir::Files))
            QVERIFY(QFile::copy(source.filePath(name), temp.filePath(name)));
        QFile module(temp.filePath(QStringLiteral("qmldir")));
        QVERIFY(module.open(QIODevice::WriteOnly));
        module.write("module DevHub\nsingleton Theme 1.0 Theme.qml\n");
        module.close();
        QFile fixture(temp.filePath(QStringLiteral("Authentication.qml")));
        QVERIFY(fixture.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(fixture.write(qml), qml.size());
        fixture.close();
        const QString report = temp.filePath(QStringLiteral("failed report.txt"));
        QProcess process;
        process.start(QString::fromUtf8(DEVHUB_TEST_EXECUTABLE),
                      {QStringLiteral("--check-runtime"),
                       QStringLiteral("--check-runtime-report=") + report,
                       QStringLiteral("--check-runtime-qml-root=") + temp.path()});
        QVERIFY(process.waitForStarted());
        QVERIFY(process.waitForFinished(20000));
        QVERIFY(process.exitStatus() == QProcess::NormalExit && process.exitCode() != 0);
        QFile file(report);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto output = file.readAll();
        QVERIFY2(output.contains(expected), output.constData());
        QVERIFY2(output.contains("PASS: MainShell.qml"), output.constData());
        QVERIFY(output.contains("FAIL: packaged runtime"));
    }
};

QTEST_GUILESS_MAIN(RuntimeCheckTest)
#include "runtime_check_test.moc"
