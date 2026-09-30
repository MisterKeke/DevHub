#pragma once

#include <QMetaType>

namespace devhub::application::lifecycle {

enum class ApplicationLifecycleState
{
    Starting,
    Ready,
    Recovery,
    ShutdownRequested,
    Stopping,
    Stopped
};

} // namespace devhub::application::lifecycle

Q_DECLARE_METATYPE(devhub::application::lifecycle::ApplicationLifecycleState)
