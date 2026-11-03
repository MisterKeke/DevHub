#include "BuildResultParser.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <QUuid>

namespace devhub::application::buildtest {
namespace {

QString testId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

qint64 secondsToMilliseconds(const QString& value)
{
    bool ok = false;
    const double seconds = value.toDouble(&ok);
    return ok ? qMax<qint64>(0, static_cast<qint64>(seconds * 1000.0)) : 0;
}

} // namespace

QVector<TestResult> BuildResultParser::parseGoJson(const QByteArray& output)
{
    QVector<TestResult> tests;
    QHash<QString, int> indexes;
    const QList<QByteArray> lines = output.split('\n');
    for (const QByteArray& line : lines) {
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) continue;
        const QJsonObject event = document.object();
        const QString packageName = event.value(QStringLiteral("Package")).toString();
        const QString name = event.value(QStringLiteral("Test")).toString();
        if (name.isEmpty() && event.value(QStringLiteral("Action")).toString() != QStringLiteral("fail")) continue;
        const QString key = packageName + QLatin1Char('\n') + (name.isEmpty() ? QStringLiteral("(package)") : name);
        int index = indexes.value(key, -1);
        if (index < 0) {
            TestResult result;
            result.id = testId(); result.packageName = packageName;
            result.name = name.isEmpty() ? QStringLiteral("(package)") : name;
            result.status = QStringLiteral("running");
            index = static_cast<int>(tests.size());
            indexes.insert(key, index); tests.append(std::move(result));
        }
        TestResult& test = tests[index];
        const QString action = event.value(QStringLiteral("Action")).toString();
        if (action == QStringLiteral("output")) {
            test.details += event.value(QStringLiteral("Output")).toString();
            if (test.details.size() > 8000) test.details = test.details.right(8000);
        } else if (action == QStringLiteral("pass") || action == QStringLiteral("fail") || action == QStringLiteral("skip")) {
            test.status = action == QStringLiteral("pass") ? QStringLiteral("passed")
                : action == QStringLiteral("skip") ? QStringLiteral("skipped") : QStringLiteral("failed");
            test.durationMilliseconds = secondsToMilliseconds(event.value(QStringLiteral("Elapsed")).toVariant().toString());
        }
    }
    tests.erase(std::remove_if(tests.begin(), tests.end(), [](const TestResult& result) {
        return result.status == QStringLiteral("running");
    }), tests.end());
    return tests;
}

QVector<TestResult> BuildResultParser::parseJUnit(const QByteArray& xml)
{
    QVector<TestResult> tests;
    QXmlStreamReader reader(xml);
    TestResult current;
    bool inTestCase = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QStringLiteral("testcase")) {
            inTestCase = true; current = {};
            current.id = testId(); current.name = reader.attributes().value(QStringLiteral("name")).toString();
            current.packageName = reader.attributes().value(QStringLiteral("classname")).toString();
            current.status = QStringLiteral("passed");
            current.durationMilliseconds = secondsToMilliseconds(reader.attributes().value(QStringLiteral("time")).toString());
        } else if (inTestCase && reader.isStartElement()
                   && (reader.name() == QStringLiteral("failure") || reader.name() == QStringLiteral("error"))) {
            current.status = QStringLiteral("failed");
            current.details = reader.attributes().value(QStringLiteral("message")).toString();
            const QString text = reader.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
            if (!text.isEmpty()) current.details += (current.details.isEmpty() ? QString{} : QStringLiteral("\n")) + text;
        } else if (inTestCase && reader.isStartElement() && reader.name() == QStringLiteral("skipped")) {
            current.status = QStringLiteral("skipped");
            current.details = reader.attributes().value(QStringLiteral("message")).toString();
            const QString text = reader.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
            if (!text.isEmpty()) current.details += (current.details.isEmpty() ? QString{} : QStringLiteral("\n")) + text;
        } else if (reader.isEndElement() && reader.name() == QStringLiteral("testcase") && inTestCase) {
            tests.append(std::move(current)); inTestCase = false;
        }
    }
    if (reader.hasError()) return {};
    return tests;
}

QVector<Diagnostic> BuildResultParser::parseDiagnostics(const QString& output, const QString& stage)
{
    static const QRegularExpression gcc(QStringLiteral(R"(^(.+?):(\d+):(\d+):\s*(fatal error|error|warning|note):\s*(.*)$)"));
    static const QRegularExpression msvc(QStringLiteral(R"(^(.+?)\((\d+)(?:,(\d+))?\):\s*(fatal error|error|warning)\s*[^:]*:\s*(.*)$)"),
                                        QRegularExpression::CaseInsensitiveOption);
    QVector<Diagnostic> results;
    for (const QString& line : output.split(QLatin1Char('\n'))) {
        auto match = gcc.match(line.trimmed());
        if (!match.hasMatch()) match = msvc.match(line.trimmed());
        if (!match.hasMatch()) continue;
        Diagnostic diagnostic;
        diagnostic.filePath = match.captured(1).trimmed();
        diagnostic.line = match.captured(2).toInt();
        diagnostic.column = match.captured(3).toInt();
        diagnostic.severity = match.captured(4).toLower();
        diagnostic.message = match.captured(5).trimmed();
        diagnostic.stage = stage;
        results.append(std::move(diagnostic));
        if (results.size() >= 1000) break;
    }
    return results;
}

void BuildResultParser::summarizeTests(BuildRun& run)
{
    run.totalTests = static_cast<int>(run.tests.size()); run.passedTests = 0; run.failedTests = 0; run.skippedTests = 0;
    for (const TestResult& result : run.tests) {
        if (result.status == QStringLiteral("passed")) ++run.passedTests;
        else if (result.status == QStringLiteral("failed")) ++run.failedTests;
        else if (result.status == QStringLiteral("skipped")) ++run.skippedTests;
    }
}

} // namespace devhub::application::buildtest
