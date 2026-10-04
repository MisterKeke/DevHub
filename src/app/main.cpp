#include "ApplicationBootstrapper.h"
#include "RuntimeCheck.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    if (application.arguments().contains(QStringLiteral("--check-runtime"))) {
        QString reportPath;
        QString qmlRoot;
        for (const auto& argument : application.arguments()) {
            const QString reportPrefix = QStringLiteral("--check-runtime-report=");
            const QString rootPrefix = QStringLiteral("--check-runtime-qml-root=");
            if (argument.startsWith(reportPrefix)) reportPath = argument.mid(reportPrefix.size());
            if (argument.startsWith(rootPrefix)) qmlRoot = argument.mid(rootPrefix.size());
        }
        return devhub::app::checkRuntime(application, reportPath, qmlRoot);
    }
    devhub::shared::time::SystemApplicationClock clock;
    devhub::shared::identifiers::UuidIdGenerator idGenerator;
    devhub::app::ApplicationBootstrapper bootstrapper(application, clock, idGenerator);
    return bootstrapper.run();
}
