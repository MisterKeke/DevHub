#include "infrastructure/codex/CodexProcess.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QLibraryInfo>
#include <QDir>
#include <QTimer>
#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir scratch;
    devhub::application::assistant::CodexTransportLimits limits;
    limits.drainMilliseconds = 50; limits.terminateMilliseconds = 50;
    devhub::infrastructure::codex::CodexProcess process(nullptr, limits);
    QObject::connect(&process, &devhub::application::assistant::ICodexTransport::started, &app, [&] {
        std::printf("%lld\n", static_cast<long long>(process.processId())); std::fflush(stdout);
        if (app.arguments().contains(QStringLiteral("emergency"))) app.quit();
        else process.stop();
    });
    QObject::connect(&process, &devhub::application::assistant::ICodexTransport::stopped, &app, &QCoreApplication::quit);
    QTimer::singleShot(0, &app, [&] {
        auto environment = devhub::application::assistant::codexBaseEnvironment(QProcessEnvironment::systemEnvironment());
        environment.insert("PATH", QDir::toNativeSeparators(QLibraryInfo::path(QLibraryInfo::BinariesPath))
            + ';' + environment.value("PATH"));
        process.launch({{QStringLiteral(DEVHUB_FAKE_CODEX), QStringLiteral("0.160.1")},
            {QStringLiteral("hang")}, scratch.path(),
            environment, 1});
    });
    QTimer::singleShot(5000, &app, [&] { app.exit(99); });
    return app.exec();
}
