#pragma once

#include "BuildTypes.h"

namespace devhub::application::buildtest {

class BuildResultParser final
{
public:
    [[nodiscard]] static QVector<TestResult> parseGoJson(const QByteArray& output);
    [[nodiscard]] static QVector<TestResult> parseJUnit(const QByteArray& xml);
    [[nodiscard]] static QVector<Diagnostic> parseDiagnostics(const QString& output, const QString& stage);
    static void summarizeTests(BuildRun& run);
};

} // namespace devhub::application::buildtest
