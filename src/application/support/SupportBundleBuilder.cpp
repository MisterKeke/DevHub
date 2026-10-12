#include "SupportBundleBuilder.h"
#include "../../domain/errors/AppError.h"

#include <QDataStream>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QRegularExpression>
#include <QSysInfo>

namespace devhub::application::support {
namespace {
struct ZipEntry final { QString name; QByteArray data; quint32 crc = 0; quint32 offset = 0; };
quint32 crc32(const QByteArray& bytes)
{
    quint32 crc = 0xffffffffU;
    for (const char raw : bytes) {
        const auto byte = static_cast<unsigned char>(raw);
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    }
    return crc ^ 0xffffffffU;
}
QByteArray zipBytes(QVector<ZipEntry> entries)
{
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly); out.setByteOrder(QDataStream::LittleEndian);
    for (auto& entry : entries) {
        const QByteArray name = entry.name.toUtf8(); entry.crc = crc32(entry.data); entry.offset = static_cast<quint32>(bytes.size());
        out << quint32(0x04034b50) << quint16(20) << quint16(0x0800) << quint16(0) << quint16(0) << quint16(0x21)
            << entry.crc << quint32(entry.data.size()) << quint32(entry.data.size())
            << quint16(name.size()) << quint16(0);
        out.writeRawData(name.constData(), name.size()); out.writeRawData(entry.data.constData(), entry.data.size());
    }
    const quint32 centralOffset = static_cast<quint32>(bytes.size());
    for (const auto& entry : entries) {
        const QByteArray name = entry.name.toUtf8();
        out << quint32(0x02014b50) << quint16(20) << quint16(20) << quint16(0x0800) << quint16(0) << quint16(0) << quint16(0x21)
            << entry.crc << quint32(entry.data.size()) << quint32(entry.data.size())
            << quint16(name.size()) << quint16(0) << quint16(0) << quint16(0) << quint16(0) << quint32(0) << entry.offset;
        out.writeRawData(name.constData(), name.size());
    }
    const quint32 centralSize = static_cast<quint32>(bytes.size()) - centralOffset;
    out << quint32(0x06054b50) << quint16(0) << quint16(0) << quint16(entries.size()) << quint16(entries.size())
        << centralSize << centralOffset << quint16(0);
    return bytes;
}
shared::common::Result<void> failure(const QString& detail)
{
    return shared::common::Result<void>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::IoFailure, domain::errors::ErrorCategory::FileSystem,
        QStringLiteral("DevHub could not create the support bundle."), detail, QStringLiteral("write support bundle")));
}
QString safeSupportLog(const QString& source, const shared::security::RedactionService& redactor)
{
    QStringList kept;
    const auto lines = source.split(QLatin1Char('\n'));
    for (const auto& line : lines) {
        // Assistant/Codex categories are deliberately omitted from exports. Their
        // live diagnostics are a separate allowlisted summary, never log payloads.
        if (line.contains(QStringLiteral("[devhub.assistant]"), Qt::CaseInsensitive)
            || line.contains(QStringLiteral("[devhub.codex."), Qt::CaseInsensitive)) continue;
        kept.append(redactor.redact(line));
    }
    return kept.join(QLatin1Char('\n'));
}
QStringList safeModuleStatus(const QStringList& source, const shared::security::RedactionService& redactor)
{
    static const QRegularExpression excluded(QStringLiteral("(?i)(transcript|prompt|response body|message body|codex state|thread id|turn id|payload|credential|authorization)"));
    QStringList kept;
    for (const auto& line : source) if (!excluded.match(line).hasMatch()) kept.append(redactor.redact(line));
    return kept;
}
}

SupportBundleBuilder::SupportBundleBuilder(QString logDirectory, settings::SettingsService& settings,
    const shared::security::RedactionService& redactor)
    : m_logDirectory(std::move(logDirectory)), m_settings(settings), m_redactor(redactor) {}

QStringList SupportBundleBuilder::previewCategories(const SupportBundleOptions& options) const
{
    QStringList items{QStringLiteral("Application and operating-system versions")};
    if (options.settings) items << QStringLiteral("Sanitized non-secret settings");
    if (options.logs) items << QStringLiteral("Sanitized rotating application logs");
    if (options.moduleStatus) items << QStringLiteral("Workspace and module status summaries");
    if (options.crashInformation) items << QStringLiteral("Crash information (only when a local crash file exists)");
    items << QStringLiteral("Excluded: source files, databases, query values, notes, workspace file contents, Assistant transcripts, and Codex state");
    return items;
}

shared::common::Result<void> SupportBundleBuilder::write(const QString& outputPath,
    const SupportBundleOptions& options, const QStringList& moduleStatus) const
{
    QVector<ZipEntry> entries;
    const QString about = QStringLiteral("DevHub %1\nOS: %2\nArchitecture: %3\nGenerated UTC: %4\n")
        .arg(QCoreApplication::applicationVersion(), QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture(),
             QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    entries.push_back({QStringLiteral("about.txt"), about.toUtf8()});
    if (options.settings) {
        QJsonObject object;
        const auto values = m_settings.sanitizedValues();
        for (auto i = values.cbegin(); i != values.cend(); ++i) {
            const QVariant safeValue = i.value().metaType().id() == QMetaType::QString
                ? QVariant(m_redactor.redact(i.value().toString())) : i.value();
            object.insert(i.key(), QJsonValue::fromVariant(safeValue));
        }
        entries.push_back({QStringLiteral("settings.json"), QJsonDocument(object).toJson(QJsonDocument::Indented)});
    }
    if (options.logs) {
        QDir directory(m_logDirectory);
        const auto files = directory.entryInfoList({QStringLiteral("devhub*.log")}, QDir::Files, QDir::Name);
        for (const auto& info : files) {
            if (info.size() > 5 * 1024 * 1024) continue;
            QFile file(info.absoluteFilePath()); if (!file.open(QIODevice::ReadOnly)) continue;
            entries.push_back({QStringLiteral("logs/%1").arg(info.fileName()), safeSupportLog(QString::fromUtf8(file.readAll()), m_redactor).toUtf8()});
        }
    }
    if (options.moduleStatus) entries.push_back({QStringLiteral("module-status.txt"),
        safeModuleStatus(moduleStatus, m_redactor).join(QLatin1Char('\n')).toUtf8()});
    if (options.crashInformation) {
        QFile crash(QDir(m_logDirectory).filePath(QStringLiteral("crash-info.txt")));
        if (crash.exists() && crash.size() <= 1024 * 1024 && crash.open(QIODevice::ReadOnly))
            entries.push_back({QStringLiteral("crash-info.txt"), safeSupportLog(QString::fromUtf8(crash.readAll()), m_redactor).toUtf8()});
    }
    QSaveFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly)) return failure(file.errorString());
    const QByteArray archive = zipBytes(std::move(entries));
    if (file.write(archive) != archive.size() || !file.commit()) return failure(file.errorString());
    return shared::common::Result<void>::success();
}
} // namespace devhub::application::support
