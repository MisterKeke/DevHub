#include "ApplicationBootstrapper.h"

#include <QApplication>
#include <QSqlDatabase>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    if (application.arguments().contains(QStringLiteral("--check-runtime"))) {
        return QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")) ? 0 : 2;
    }
    devhub::shared::time::SystemApplicationClock clock;
    devhub::shared::identifiers::UuidIdGenerator idGenerator;
    devhub::app::ApplicationBootstrapper bootstrapper(application, clock, idGenerator);
    return bootstrapper.run();
}
