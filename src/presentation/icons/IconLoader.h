#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace devhub::presentation::icons {

enum class IconName { Terminal, Folder, Settings, More, Workspace, Dashboard, Project, Ports, Logs, Logo, Lock,
                      Git, Services, Database, Notes, Assistant };

class IconLoader final
{
public:
    [[nodiscard]] static QIcon icon(IconName name, const QColor& tint, int logicalSize = 20, qreal dpr = 1.0);
    [[nodiscard]] static QString licenseNotice();
};

} // namespace devhub::presentation::icons
