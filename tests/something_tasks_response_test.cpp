#include "application/assistant/SomethingTasksResponse.h"

#include <QJsonArray>
#include <QTest>

using namespace devhub::application::assistant;

namespace {
ToolResult response()
{
    ToolResult result;
    result.success = true;
    result.data = {{QStringLiteral("source"), QStringLiteral("SomethingQt")},
        {QStringLiteral("sourceProfile"), QStringLiteral("default/something.sqlite")},
        {QStringLiteral("retrievedAtUtc"), QStringLiteral("2026-10-21T12:00:00Z")},
        {QStringLiteral("requestedTags"), QJsonArray{QStringLiteral("DevHub")}},
        {QStringLiteral("truncated"), false}};
    return result;
}

QJsonObject summary(qint64 id)
{
    QJsonArray tags{QStringLiteral("DevHub")};
    for (int i = 1; i < 16; ++i)
        tags.append(QString(62, QChar(0x754c)) + QString::number(i));
    return {{QStringLiteral("taskId"), QString::number(id)},
        {QStringLiteral("title"), QString(512, QChar(0x754c))},
        {QStringLiteral("titleTruncated"), false},
        {QStringLiteral("completed"), false},
        {QStringLiteral("priority"), QStringLiteral("high")},
        {QStringLiteral("difficulty"), QStringLiteral("hard")},
        {QStringLiteral("dueDate"), QStringLiteral("2026-10-23")},
        {QStringLiteral("tags"), tags}, {QStringLiteral("tagsTruncated"), false},
        {QStringLiteral("revision"), QStringLiteral("9007199254740993")},
        {QStringLiteral("updatedAt"), QStringLiteral("2026-10-21T11:00:00Z")}};
}
}

class SomethingTasksResponseTest final : public QObject {
    Q_OBJECT
private slots:
    void oversizedFindRetainsEveryTaskAndSourceThroughRegistry()
    {
        auto input = response();
        QJsonArray tasks;
        for (int i = 0; i < 20; ++i) tasks.append(summary(9007199254740993LL + i));
        input.data.insert(QStringLiteral("tasks"), tasks);
        input.data.insert(QStringLiteral("hasMore"), true);
        input.data.insert(QStringLiteral("offset"), 0);
        input.data.insert(QStringLiteral("limit"), 20);
        QVERIFY(input.compactJson().size() > kSomethingTasksFindOutputBytes);

        ToolResult output;
        int replies = 0;
        AssistantToolRegistry registry([&](const QString&, const ToolResult& result) { output = result; ++replies; });
        ToolDefinition definition;
        definition.name = QStringLiteral("devhub_something_tasks_find");
        definition.description = QStringLiteral("Synthetic oversized task response");
        definition.maximumOutputBytes = kSomethingTasksFindOutputBytes;
        QVERIFY(registry.registerTool(definition, [input](const ToolCall&, const ToolCancellation&, auto done) {
            done(boundSomethingTasksResponse(input, kSomethingTasksFindOutputBytes));
        }));
        ToolCall call;
        call.routeId = QStringLiteral("route"); call.callId = QStringLiteral("call"); call.name = definition.name;
        call.scope = ToolScope(QStringLiteral("scope"), QStringLiteral("account"), {}, {}, 1, 1, true);
        QVERIFY(registry.invoke(call));
        QCOMPARE(replies, 1);
        QVERIFY(output.success); QVERIFY(output.truncated);
        QVERIFY(output.compactJson().size() <= kSomethingTasksFindOutputBytes);
        QCOMPARE(output.data.value(QStringLiteral("sourceProfile")), input.data.value(QStringLiteral("sourceProfile")));
        QCOMPARE(output.data.value(QStringLiteral("retrievedAtUtc")), input.data.value(QStringLiteral("retrievedAtUtc")));
        QCOMPARE(output.data.value(QStringLiteral("requestedTags")), input.data.value(QStringLiteral("requestedTags")));
        const auto rows = output.data.value(QStringLiteral("tasks")).toArray();
        QCOMPARE(rows.size(), tasks.size());
        QVERIFY(output.data.value(QStringLiteral("hasMore")).toBool());
        for (qsizetype i = 0; i < rows.size(); ++i) {
            const auto row = rows.at(i).toObject();
            QCOMPARE(row.value(QStringLiteral("taskId")), tasks.at(i).toObject().value(QStringLiteral("taskId")));
            QCOMPARE(row.value(QStringLiteral("dueDate")).toString(), QStringLiteral("2026-10-23"));
            QVERIFY(!row.value(QStringLiteral("title")).toString().isEmpty());
            QVERIFY(row.value(QStringLiteral("titleTruncated")).toBool());
            const auto remainingTags = row.value(QStringLiteral("tags")).toArray();
            const auto originalTags = tasks.at(i).toObject().value(QStringLiteral("tags")).toArray();
            for (qsizetype j = 0; j < remainingTags.size(); ++j) QCOMPARE(remainingTags.at(j), originalTags.at(j));
        }
    }

    void oversizedDetailPreservesUnicodeAndSubtaskMetadata()
    {
        auto input = response();
        QJsonObject task = summary(9007199254740993LL);
        const QString text = QString::fromUcs4(U"\U0001f600\U0001f600\U0001f600") + QChar(0x0001);
        task.insert(QStringLiteral("description"), text.repeated(1100));
        task.insert(QStringLiteral("descriptionTruncated"), false);
        QJsonArray subtasks;
        for (int i = 0; i < 50; ++i)
            subtasks.append(QJsonObject{{QStringLiteral("subtaskId"), QString::number(9007199254741093LL + i)},
                {QStringLiteral("title"), text.repeated(36)}, {QStringLiteral("titleTruncated"), false},
                {QStringLiteral("completed"), i % 2 == 0}, {QStringLiteral("position"), i}});
        task.insert(QStringLiteral("subtasks"), subtasks);
        task.insert(QStringLiteral("subtasksTruncated"), false);
        input.data.insert(QStringLiteral("task"), task);
        QVERIFY(input.compactJson().size() > kSomethingTaskReadOutputBytes);
        const auto output = boundSomethingTasksResponse(input, kSomethingTaskReadOutputBytes);
        QVERIFY(output.success); QVERIFY(output.truncated);
        QVERIFY(output.compactJson().size() <= kSomethingTaskReadOutputBytes);
        QCOMPARE(output.data.value(QStringLiteral("sourceProfile")), input.data.value(QStringLiteral("sourceProfile")));
        const auto bounded = output.data.value(QStringLiteral("task")).toObject();
        QCOMPARE(bounded.value(QStringLiteral("taskId")), task.value(QStringLiteral("taskId")));
        QCOMPARE(bounded.value(QStringLiteral("revision")), task.value(QStringLiteral("revision")));
        QCOMPARE(bounded.value(QStringLiteral("dueDate")), task.value(QStringLiteral("dueDate")));
        const QString description = bounded.value(QStringLiteral("description")).toString();
        QVERIFY(!description.isEmpty()); QVERIFY(description.isValidUtf16());
        QVERIFY(task.value(QStringLiteral("description")).toString().startsWith(description));
        QVERIFY(bounded.value(QStringLiteral("descriptionTruncated")).toBool());
        const auto remaining = bounded.value(QStringLiteral("subtasks")).toArray();
        QCOMPARE(remaining.size(), subtasks.size());
        for (qsizetype i = 0; i < remaining.size(); ++i) {
            const auto row = remaining.at(i).toObject();
            const auto original = subtasks.at(i).toObject();
            QCOMPARE(row.value(QStringLiteral("subtaskId")), original.value(QStringLiteral("subtaskId")));
            QCOMPARE(row.value(QStringLiteral("completed")), original.value(QStringLiteral("completed")));
            const QString title = row.value(QStringLiteral("title")).toString();
            QVERIFY(title.isValidUtf16()); QVERIFY(!title.isEmpty());
            QVERIFY(original.value(QStringLiteral("title")).toString().startsWith(title));
            QVERIFY(row.value(QStringLiteral("titleTruncated")).toBool());
        }
    }

    void smallResponseRemainsIntact()
    {
        auto input = response();
        auto task = summary(1);
        task.insert(QStringLiteral("title"), QStringLiteral("Small task"));
        task.insert(QStringLiteral("tags"), QJsonArray{QStringLiteral("DevHub")});
        input.data.insert(QStringLiteral("tasks"), QJsonArray{task});
        QCOMPARE(boundSomethingTasksResponse(input, kSomethingTasksFindOutputBytes).compactJson(), input.compactJson());
    }

    void subtaskTitleTruncationIsReportedAtTopLevel()
    {
        auto input = response();
        input.data.insert(QStringLiteral("task"), QJsonObject{{QStringLiteral("taskId"), QStringLiteral("1")},
            {QStringLiteral("subtasks"), QJsonArray{QJsonObject{{QStringLiteral("title"), QStringLiteral("Shortened")},
                {QStringLiteral("titleTruncated"), true}}}}});
        const auto output = boundSomethingTasksResponse(input, kSomethingTaskReadOutputBytes);
        QVERIFY(output.truncated); QVERIFY(output.data.value(QStringLiteral("truncated")).toBool());
    }

    void shortenedPagesResumeAfterReturnedRows()
    {
        auto input = response();
        QJsonArray tasks;
        for (int i = 0; i < 20; ++i) tasks.append(summary(i + 1));
        input.data.insert(QStringLiteral("tasks"), tasks);
        input.data.insert(QStringLiteral("offset"), 7);
        input.data.insert(QStringLiteral("hasMore"), false);
        constexpr qsizetype smallBudget = 2048;
        const auto output = boundSomethingTasksResponse(input, smallBudget);
        QVERIFY(output.success); QVERIFY(output.truncated);
        QVERIFY(output.compactJson().size() <= smallBudget);
        const auto rows = output.data.value(QStringLiteral("tasks")).toArray();
        QVERIFY(!rows.isEmpty()); QVERIFY(rows.size() < tasks.size());
        QVERIFY(output.data.value(QStringLiteral("hasMore")).toBool());
        QCOMPARE(output.data.value(QStringLiteral("nextOffset")).toInt(), 7 + rows.size());
    }
};

QTEST_GUILESS_MAIN(SomethingTasksResponseTest)
#include "something_tasks_response_test.moc"
