#pragma once

class QObject;

namespace devhub::application::databases {
class IDatabaseSession;

class ISqliteConnectionFactory
{
public:
    virtual ~ISqliteConnectionFactory() = default;
    [[nodiscard]] virtual IDatabaseSession* createSession(QObject* parent) = 0;
};
}
