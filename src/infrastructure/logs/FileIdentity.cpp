#include "FileIdentity.h"

#include <QFileInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace devhub::infrastructure::logs {

FileIdentity identifyFile(const QString& path)
{
#ifdef Q_OS_WIN
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};
    BY_HANDLE_FILE_INFORMATION info{};
    const bool ok = GetFileInformationByHandle(handle, &info) != 0;
    CloseHandle(handle);
    if (!ok) return {};
    return {QStringLiteral("%1:%2:%3")
        .arg(info.dwVolumeSerialNumber, 8, 16, QLatin1Char('0'))
        .arg(info.nFileIndexHigh, 8, 16, QLatin1Char('0'))
        .arg(info.nFileIndexLow, 8, 16, QLatin1Char('0'))};
#else
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) return {};
    return {info.canonicalFilePath()};
#endif
}

} // namespace devhub::infrastructure::logs
