#pragma once

#include <chrono>

namespace devhub::application::authentication {

struct AuthenticationProductPolicy final
{
    static constexpr bool KeepSessionDefault = false;
    static constexpr std::chrono::minutes InactivityTimeout{15};
    static constexpr int FailedAttemptsBeforeDelay = 3;
    static constexpr int InitialFailedAttemptDelayMs = 500;
    static constexpr int MaximumFailedAttemptDelayMs = 8000;
};

} // namespace devhub::application::authentication
