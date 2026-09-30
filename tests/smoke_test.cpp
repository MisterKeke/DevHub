#include <QCoreApplication>
#include <QtTest/QtTest>

class SmokeTest final : public QObject
{
    Q_OBJECT

private slots:
    void coreApplicationStarts()
    {
        QVERIFY(QCoreApplication::instance() != nullptr);
    }
};

QTEST_GUILESS_MAIN(SmokeTest)
#include "smoke_test.moc"
