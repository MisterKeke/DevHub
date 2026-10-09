#include "infrastructure/codex/CodexProtocolCodec.h"
#include "infrastructure/codex/CodexWriteQueue.h"
#include "application/assistant/ICodexTransport.h"
#include <QSignalSpy>
#include <QTest>
#include <type_traits>

using namespace devhub::application::assistant;
using devhub::infrastructure::codex::CodexProtocolCodec;
using devhub::infrastructure::codex::CodexWriteQueue;

static_assert(std::is_const_v<decltype(CodexExecutable::path)>);
static_assert(std::is_const_v<decltype(CodexExecutable::version)>);
static_assert(std::is_const_v<decltype(CodexLaunchSpec::executable)>);
static_assert(std::is_const_v<decltype(CodexLaunchSpec::arguments)>);
static_assert(std::is_const_v<decltype(CodexLaunchSpec::scratchDirectory)>);
static_assert(std::is_const_v<decltype(CodexLaunchSpec::environment)>);
static_assert(std::is_const_v<decltype(CodexLaunchSpec::processGeneration)>);
static_assert(std::is_copy_constructible_v<CodexExecutable> && std::is_copy_constructible_v<CodexLaunchSpec>);
static_assert(!std::is_copy_assignable_v<CodexExecutable> && !std::is_copy_assignable_v<CodexLaunchSpec>);

// Replaceable QObject seam for the client milestone; preserves JSON ID types.
class MemoryTransport final : public ICodexTransport
{
public:
    using ICodexTransport::ICodexTransport;
    quint64 generation = 0;
    QList<QJsonObject> sent;
    void launch(const CodexLaunchSpec& spec) override { generation = spec.processGeneration; emit started(generation); }
    devhub::shared::common::Result<void> sendObject(const QJsonObject& object) override
    { sent.append(object); return devhub::shared::common::Result<void>::success(); }
    void stop() override { emit stopped(0, false, generation); }
    void deliver(QJsonObject object) { emit objectReceived(object, generation); }
};
class AssistantProtocolTest final : public QObject
{
    Q_OBJECT
private slots:
    void fragmentedUtf8()
    {
        CodexProtocolCodec codec;
        const auto frame = QByteArray("\n\r\n{\"id\":\"123\",\"result\":\"")
            + QString::fromUtf8("İ 🐈").toUtf8() + "\"}\r\n";
        QList<QJsonObject> objects;
        for (const char byte : frame) {
            QVERIFY(codec.feed(QByteArray(1, byte)));
            while (codec.hasRecord()) {
                auto value = codec.nextEnvelope(); QVERIFY(value);
                if (value.value()) objects.append(*value.value());
            }
        }
        QCOMPARE(objects.size(), 1);
        QCOMPARE(objects.front().value("result").toString(), QString::fromUtf8("İ 🐈"));
        QVERIFY(objects.front().value("id").isString());
        QVERIFY(codec.finish());
    }
    void envelopes_data()
    {
        QTest::addColumn<QByteArray>("bytes"); QTest::addColumn<bool>("accepted");
        QTest::newRow("request") << QByteArray("{\"method\":\"read\",\"id\":1,\"params\":{}}\n") << true;
        QTest::newRow("server request string") << QByteArray("{\"method\":\"approve\",\"id\":\"1\"}\n") << true;
        QTest::newRow("error") << QByteArray("{\"id\":1,\"error\":{\"code\":-32000,\"message\":\"failed\"}}\n") << true;
        QTest::newRow("unknown notification") << QByteArray("{\"method\":\"future/event\"}\n") << true;
        QTest::newRow("array") << QByteArray("[]\n") << false;
        QTest::newRow("malformed") << QByteArray("{broken}\n") << false;
        QTest::newRow("both response kinds") << QByteArray("{\"id\":1,\"result\":{},\"error\":{}}\n") << false;
        QTest::newRow("mixed request response") << QByteArray("{\"id\":1,\"method\":\"read\",\"result\":{}}\n") << false;
        QTest::newRow("fractional id") << QByteArray("{\"id\":1.5,\"result\":null}\n") << false;
        QTest::newRow("null id") << QByteArray("{\"id\":null,\"result\":null}\n") << false;
        QTest::newRow("unsafe integer") << QByteArray("{\"id\":9007199254740992,\"result\":null}\n") << false;
        QTest::newRow("invalid utf8") << QByteArray("{\"method\":\"") + char(0xff) + "\"}\n" << false;
    }
    void envelopes()
    {
        QFETCH(QByteArray, bytes); QFETCH(bool, accepted);
        CodexProtocolCodec codec; QVERIFY(codec.feed(bytes));
        QCOMPARE(codec.nextEnvelope().hasValue(), accepted);
    }
    void capsAndEof()
    {
        CodexTransportLimits limits; limits.recordBytes = 32; limits.framingBytes = 64;
        CodexProtocolCodec codec(limits);
        QVERIFY(!codec.feed(QByteArray(65, 'x')));
        codec.reset(); QVERIFY(!codec.feed(QByteArray(34, 'x')));
        codec.reset(); QVERIFY(codec.feed(QByteArray(33, 'x') + '\n')); QVERIFY(!codec.nextEnvelope());
        codec.reset(); QVERIFY(codec.feed("{\"method\":\"event\"}")); QVERIFY(!codec.finish());
        codec.reset(); QVERIFY(codec.feed("{\"method\":\"event\"}\n")); QVERIFY(codec.nextEnvelope()); QVERIFY(codec.finish());
    }
    void compactEncodingAndFake()
    {
        CodexProtocolCodec codec;
        const QJsonObject object{{"id", 42}, {"method", "read"}, {"params", QJsonObject{{"text", "a\nb"}}}};
        const auto bytes = codec.encode(object); QVERIFY(bytes);
        QCOMPARE(bytes.value().count('\n'), 1); QVERIFY(bytes.value().endsWith('\n'));
        QVERIFY(codec.feed(bytes.value())); const auto parsed = codec.nextEnvelope(); QVERIFY(parsed);
        QCOMPARE(*parsed.value(), object); QVERIFY(parsed.value()->value("id").isDouble());
        MemoryTransport fake; QSignalSpy received(&fake, &ICodexTransport::objectReceived);
        const CodexLaunchSpec spec{{}, {}, {}, {}, 99}; fake.launch(spec);
        QVERIFY(fake.sendObject(object)); fake.deliver(object);
        QCOMPARE(received.front().at(1).toULongLong(), quint64(99)); QCOMPARE(fake.sent.front(), object);
    }
    void environmentAllowlist()
    {
        QProcessEnvironment source; source.insert("PATH", "tools"); source.insert("OPENAI_API_KEY", "secret");
        source.insert("CODEX_HOME", "unchecked"); source.insert("UNRELATED", "value");
        const auto filtered = codexBaseEnvironment(source);
        QVERIFY(filtered.contains("PATH")); QCOMPARE(filtered.keys().size(), 1); QVERIFY(!filtered.inheritsFromParent());
    }
    void immutableSnapshots()
    {
        QString path = "native.exe", version = "0.160.1", scratch = "scratch";
        QStringList arguments{"app-server"};
        QProcessEnvironment environment; environment.insert("PATH", "safe-path");
        const CodexExecutable executable{path, version};
        const CodexLaunchSpec launch{executable, arguments, scratch, environment, 77};
        path = "changed.exe"; version = "other"; scratch = "other";
        arguments.append("changed"); environment.insert("PATH", "changed");
        QCOMPARE(launch.executable.path, QStringLiteral("native.exe"));
        QCOMPARE(launch.executable.version, QStringLiteral("0.160.1"));
        QCOMPARE(launch.arguments, QStringList{"app-server"});
        QCOMPARE(launch.scratchDirectory, QStringLiteral("scratch"));
        QCOMPARE(launch.environment.value("PATH"), QStringLiteral("safe-path"));
        QCOMPARE(launch.processGeneration, quint64(77));
        const auto copy = launch;
        QCOMPARE(copy.executable.path, launch.executable.path);
        const auto variant = QVariant::fromValue(executable);
        QCOMPARE(qvariant_cast<CodexExecutable>(variant).version, executable.version);
    }
    void deterministicShortWrites()
    {
        CodexProtocolCodec codec;
        const QJsonObject first{{"id", 1}, {"method", "echo"},
            {"params", QJsonObject{{"text", QString::fromUtf8("İ 🐈\ntext")}}}};
        const QJsonObject second{{"id", "two"}, {"method", "echo"}, {"params", QJsonObject{{"ok", true}}}};
        const auto firstBytes = codec.encode(first), secondBytes = codec.encode(second);
        QVERIFY(firstBytes); QVERIFY(secondBytes);
        const QByteArray expected = firstBytes.value() + secondBytes.value();
        CodexWriteQueue queue(expected.size());
        QVERIFY(queue.enqueue(firstBytes.value(), 0)); QVERIFY(queue.enqueue(secondBytes.value(), 0));
        QByteArray delivered;
        int calls = 0, shortWrites = 0, zeroWrites = 0;
        const auto writer = [&](const char* bytes, qint64 size) -> qint64 {
            if (QByteArray(bytes, static_cast<qsizetype>(size)) != expected.mid(delivered.size())) {
                QTest::qFail("Writer received a corrupted/reordered suffix", __FILE__, __LINE__);
                return -1;
            }
            // Include zero writes and split every multibyte UTF-8 character.
            const qint64 accepted = calls++ % 4 == 0 ? 0 : 1;
            if (accepted == 0) ++zeroWrites;
            else if (accepted < size) ++shortWrites;
            delivered.append(bytes, static_cast<qsizetype>(accepted));
            return accepted;
        };
        while (queue.size() && calls < 4 * expected.size()) {
            const auto written = queue.flush(writer); QVERIFY(written);
            QCOMPARE(queue.size() + delivered.size(), expected.size());
        }
        QVERIFY(shortWrites > 0); QVERIFY(zeroWrites > 0);
        QCOMPARE(queue.size(), qint64(0)); QCOMPARE(delivered, expected);
        QVERIFY(codec.feed(delivered));
        const auto decodedFirst = codec.nextEnvelope(), decodedSecond = codec.nextEnvelope();
        QVERIFY(decodedFirst); QVERIFY(decodedSecond);
        QVERIFY(decodedFirst.value()); QVERIFY(decodedSecond.value());
        QCOMPARE(*decodedFirst.value(), first); QCOMPARE(*decodedSecond.value(), second); QVERIFY(codec.finish());
        QVERIFY(queue.flush([](const char*, qint64) -> qint64 { QTest::qFail("Empty queue invoked writer", __FILE__, __LINE__); return -1; }));
    }
    void shortWriteCapsAndErrors()
    {
        CodexWriteQueue queue(12);
        QVERIFY(queue.enqueue("abcd", 8)); QVERIFY(!queue.enqueue("x", 8));
        QVERIFY(!queue.enqueue("x", -1)); QVERIFY(!queue.enqueue("x", 13));
        auto written = queue.flush([](const char*, qint64) -> qint64 { return 0; });
        QVERIFY(written); QCOMPARE(written.value(), qint64(0)); QCOMPARE(queue.size(), qint64(4));
        written = queue.flush([](const char*, qint64) -> qint64 { return 2; });
        QVERIFY(written); QCOMPARE(queue.size(), qint64(2));
        QVERIFY(!queue.enqueue("x", 10)); // Accepted bytes still count while in Qt's pipe buffer.
        QVERIFY(queue.enqueue("ef", 0)); // After acknowledgement, another frame can be accepted.
        QVERIFY(!queue.flush([](const char*, qint64) -> qint64 { return -1; })); QCOMPARE(queue.size(), qint64(4));
        QVERIFY(!queue.flush([](const char*, qint64 size) -> qint64 { return size + 1; })); QCOMPARE(queue.size(), qint64(4));
        QByteArray suffix;
        written = queue.flush([&](const char* bytes, qint64 size) -> qint64 {
            suffix.append(bytes, static_cast<qsizetype>(size)); return size;
        });
        QVERIFY(written); QCOMPARE(suffix, QByteArray("cdef")); QCOMPARE(queue.size(), qint64(0));
        QVERIFY(queue.enqueue("relaunch", 0)); queue.clear(); QCOMPARE(queue.size(), qint64(0));
    }
};
QTEST_GUILESS_MAIN(AssistantProtocolTest)
#include "assistant_protocol_test.moc"
