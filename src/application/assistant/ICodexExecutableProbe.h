#pragma once
#include "AssistantTypes.h"
#include <QObject>
namespace devhub::application::assistant {
class ICodexExecutableProbe : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void locate(const QString& configuredPath, quint64 generation) = 0;
    virtual void cancel() = 0;
    virtual bool isProbing() const = 0;
signals:
    void compatibilityChecked(CodexCompatibilityReport report, quint64 generation);
    void executableFound(CodexExecutable executable, quint64 generation);
    void probeFailed(domain::errors::AppError error, quint64 generation);
};
}
