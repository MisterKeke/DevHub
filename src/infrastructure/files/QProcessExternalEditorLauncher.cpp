#include "QProcessExternalEditorLauncher.h"

#include <QFileInfo>
#include <QProcess>

namespace devhub::infrastructure::files {

shared::common::Result<void> QProcessExternalEditorLauncher::launch(const QString& program, const QString& path)
{
    if (!QFileInfo(program).isFile() || !QFileInfo(path).isFile())
        return shared::common::Result<void>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::IoFailure, domain::errors::ErrorCategory::Process,
            QStringLiteral("The editor or selected file could not be found."), QStringLiteral("program=%1 path=%2").arg(program, path),
            QStringLiteral("launch external editor")));
    if (!QProcess::startDetached(program, {QFileInfo(path).absoluteFilePath()}))
        return shared::common::Result<void>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::IoFailure, domain::errors::ErrorCategory::Process,
            QStringLiteral("DevHub could not start the configured editor."), program, QStringLiteral("launch external editor")));
    return shared::common::Result<void>::success();
}

} // namespace devhub::infrastructure::files
