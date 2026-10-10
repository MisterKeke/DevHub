#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>
#include <cstdio>
#include <cstdlib>
#include <thread>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

namespace {
void output(const QByteArray& bytes, FILE* channel = stdout)
{
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), channel);
    std::fflush(channel);
}
void envelope(QJsonObject object)
{
    output(QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n');
}
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    const auto args = app.arguments();
    if (args.contains(QStringLiteral("--version"))) {
        const auto name = QFileInfo(app.applicationFilePath()).baseName();
        if (name.contains(QStringLiteral("hang"))) return app.exec();
        if (name.contains(QStringLiteral("oversize"))) output(QByteArray(8192, 'v'));
        else if (name.contains(QStringLiteral("unknown"))) output("codex-cli 9.9.9\n");
        else if (name.contains(QStringLiteral("badversion"))) output("not codex\n");
        else if (name.contains(QStringLiteral("alpha"))) output("codex-cli 0.162.0-alpha.17.2\r\n");
        else if (name.contains(QStringLiteral("unsupported"))) output("codex-cli 0.163.0\r\n");
        else output("codex-cli 0.160.1\r\n");
        return 0;
    }
    const QString scenario = args.value(1, QStringLiteral("echo"));
    QProcess ownedHelper;
    if (scenario == QStringLiteral("owned")) {
        ownedHelper.setProgram(app.applicationFilePath());
        ownedHelper.setArguments({QStringLiteral("echo")});
        QObject::connect(&ownedHelper, &QProcess::started, &app, [&] {
            envelope({{"method", "helper/started"}, {"params", QJsonObject{{"pid", ownedHelper.processId()}}}});
        });
        ownedHelper.start();
    }
    // This helper alone has a blocking stdin reader; the production transport is
    // fully event driven. Queue replies to its main thread to serialize stdout.
    if (scenario != QStringLiteral("hang") && scenario != QStringLiteral("pressure")) {
        std::thread([&app, &ownedHelper, scenario] {
            QByteArray line;
            int ch;
            while ((ch = std::fgetc(stdin)) != EOF) {
                if (ch != '\n') { if (line.size() < 2 * 1024 * 1024) line.append(static_cast<char>(ch)); continue; }
                const auto received = line; line.clear();
                QMetaObject::invokeMethod(&app, [received, scenario] {
                    if (scenario == QStringLiteral("echo")) {
                        const auto object = QJsonDocument::fromJson(received).object();
                        if (object.contains(QStringLiteral("id")))
                            envelope({{"id", object.value(QStringLiteral("id"))}, {"result", object.value(QStringLiteral("params"))}});
                    }
                }, Qt::QueuedConnection);
            }
            QMetaObject::invokeMethod(&app, [&app, &ownedHelper] {
                if (ownedHelper.state() != QProcess::NotRunning) {
                    QObject::connect(&ownedHelper, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &app, &QCoreApplication::quit);
                    ownedHelper.closeWriteChannel();
                } else app.quit();
            }, Qt::QueuedConnection);
        }).detach();
    }
    QTimer::singleShot(0, &app, [scenario, &app] {
        if (scenario == QStringLiteral("fragment")) {
            const auto frame = QByteArray("\r\n{\"method\":\"delta\",\"params\":{\"text\":\"")
                + QString::fromUtf8("İstanbul 🐈").toUtf8() + "\"}}\r\n";
            auto* timer = new QTimer(&app);
            QObject::connect(timer, &QTimer::timeout, &app, [timer, frame, index = qsizetype{0}]() mutable {
                output(frame.mid(index++, 1));
                if (index == frame.size()) { timer->stop(); timer->deleteLater(); }
            });
            timer->start(1);
        } else if (scenario == QStringLiteral("stderr")) {
            output(QByteArray(2 * 1024 * 1024, 'x'), stderr);
            output("\naccess_token=secret-value\nhealthy\n", stderr);
            envelope({{"method", "alive"}});
        } else if (scenario == QStringLiteral("burst")) {
            QByteArray frames;
            for (int i = 0; i < 5000; ++i) frames.append("{\"method\":\"event\"}\n");
            output(frames);
        } else if (scenario == QStringLiteral("large")) {
            envelope({{"method", "history"}, {"params", QJsonObject{{"text", QString(3 * 1024 * 1024, 'x')}}}});
        } else if (scenario == QStringLiteral("malformed")) output("{broken}\n");
        else if (scenario == QStringLiteral("oversize")) output(QByteArray(5 * 1024 * 1024, 'x'));
        else if (scenario == QStringLiteral("truncated")) { output("{\"method\":"); app.quit(); }
        else if (scenario == QStringLiteral("crash")) std::abort();
        else if (scenario == QStringLiteral("exitburst")) {
            QByteArray frames;
            for (int i = 0; i < 5000; ++i) frames.append("{\"method\":\"last\"}\n");
            output(frames); app.quit();
        }
    });
    return app.exec();
}
