#pragma once

#include "LogTypes.h"

namespace devhub::application::logs {

class LogParser final
{
public:
    [[nodiscard]] static LogLine parse(quint64 sequence, QString text);
};

} // namespace devhub::application::logs
