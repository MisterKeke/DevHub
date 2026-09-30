#include "ApplicationClock.h"

namespace devhub::shared::time {

QDateTime SystemApplicationClock::utcNow() const
{
    return QDateTime::currentDateTimeUtc();
}

} // namespace devhub::shared::time
