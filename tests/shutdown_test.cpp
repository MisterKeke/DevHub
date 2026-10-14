#include <QProcess>
#include <QtTest>

class ShutdownTest final : public QObject
{
    Q_OBJECT
  private slots:
    void deferredWindowCloseExitsProcess_data()
    {
        QTest::addColumn<QString>("mode");
        for (const auto *mode : {"empty", "immediate", "delayed", "deadline", "cancel"})
            QTest::newRow(mode) << QString::fromLatin1(mode);
    }
    void deferredWindowCloseExitsProcess()
    {
        QFETCH(QString, mode);
        QProcess process;
        process.start(QString::fromUtf8(DEVHUB_WINDOW_SHUTDOWN_DRIVER), {mode});
        QVERIFY2(process.waitForStarted(), qPrintable(process.errorString()));
        QVERIFY2(process.waitForFinished(5000), qPrintable(process.errorString()));
        const QByteArray diagnostics =
            process.readAllStandardOutput() + process.readAllStandardError();
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QVERIFY2(process.exitCode() == 0, diagnostics.constData());
        QVERIFY2(diagnostics.contains("shutdown=1 aboutToQuit=1 requests=1"),
                 diagnostics.constData());
    }
};

QTEST_GUILESS_MAIN(ShutdownTest)
#include "shutdown_test.moc"
