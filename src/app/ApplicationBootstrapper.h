#pragma once

#include "../shared/identifiers/IdGenerator.h"
#include "../shared/time/ApplicationClock.h"
#include "../domain/errors/AppError.h"

class QApplication;
class QElapsedTimer;

namespace devhub::infrastructure::database {
class InternalDatabaseManager;
}
namespace devhub::infrastructure::filesystem {
struct ApplicationPaths;
}
namespace devhub::infrastructure::logging { class ApplicationLogManager; }

namespace devhub::app {

class ApplicationBootstrapper final
{
public:
    ApplicationBootstrapper(QApplication& application,
                            shared::time::IApplicationClock& clock,
                            shared::identifiers::IIdGenerator& idGenerator);
    [[nodiscard]] int run();

private:
    [[nodiscard]] int runApplication(const infrastructure::filesystem::ApplicationPaths& paths,
                                     infrastructure::database::InternalDatabaseManager& database,
                                     infrastructure::logging::ApplicationLogManager& logManager,
                                     QElapsedTimer& startupTimer,
                                     bool recoveredFromUncleanExit);
    void logFatalStartupError(const domain::errors::AppError& error) const;

    QApplication& m_application;
    shared::time::IApplicationClock& m_clock;
    shared::identifiers::IIdGenerator& m_idGenerator;
};

} // namespace devhub::app
