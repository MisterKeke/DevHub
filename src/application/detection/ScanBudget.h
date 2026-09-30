#pragma once

#include <QSet>
#include <QStringList>

#include <chrono>

namespace devhub::application::detection {

struct ScanBudget final
{
    int maximumDepth = 12;
    qint64 maximumInspectedFiles = 100'000;
    std::chrono::milliseconds wallTime{5000};
    bool validateSQLiteHeader = false;
    QSet<QString> ignoredDirectoryNames{
        QStringLiteral(".git"), QStringLiteral("node_modules"), QStringLiteral("build"),
        QStringLiteral("dist"), QStringLiteral("out"), QStringLiteral("target"),
        QStringLiteral(".vs"), QStringLiteral(".idea"), QStringLiteral(".vscode"),
        QStringLiteral(".qtcreator"), QStringLiteral(".cache"), QStringLiteral(".gradle"),
        QStringLiteral(".venv"), QStringLiteral("venv"), QStringLiteral("__pycache__"),
        QStringLiteral(".next"), QStringLiteral(".nuxt"), QStringLiteral(".angular"),
        QStringLiteral("pods"), QStringLiteral("bin"), QStringLiteral("obj")};
    QStringList ignoredDirectoryPatterns{QStringLiteral("^cmake-build(?:-.*)?$"),
                                         QStringLiteral("^build[-_].*$")};
};

} // namespace devhub::application::detection
