#include "IconLoader.h"

#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace devhub::presentation::icons {
namespace {

QString svgSource(IconName name)
{
    QString shape;
    switch (name) {
    case IconName::Terminal:
        shape = QStringLiteral("<path d='m5 7 5 5-5 5'/><path d='M12 17h7'/>");
        break;
    case IconName::Folder:
        shape = QStringLiteral("<path d='M3 6h7l2 2h9v11H3z'/>");
        break;
    case IconName::Settings:
        shape = QStringLiteral("<circle cx='12' cy='12' r='7'/><circle cx='12' cy='12' r='2.5'/>");
        break;
    case IconName::More:
        shape = QStringLiteral("<circle cx='5' cy='12' r='1'/><circle cx='12' cy='12' r='1'/><circle cx='19' cy='12' r='1'/>");
        break;
    case IconName::Workspace:
        shape = QStringLiteral("<rect x='3' y='4' width='18' height='16' rx='2'/><path d='M8 8h8M8 12h8M8 16h5'/>");
        break;
    case IconName::Dashboard:
        shape = QStringLiteral("<rect x='3' y='3' width='8' height='8' rx='1'/><rect x='13' y='3' width='8' height='5' rx='1'/><rect x='13' y='10' width='8' height='11' rx='1'/><rect x='3' y='13' width='8' height='8' rx='1'/>");
        break;
    case IconName::Project:
        shape = QStringLiteral("<path d='M4 7h6l2 2h8v10H4z'/><path d='M4 7V5h6l2 2'/>");
        break;
    case IconName::Ports:
        shape = QStringLiteral("<circle cx='6' cy='6' r='2'/><circle cx='18' cy='18' r='2'/><path d='M8 6h5a3 3 0 0 1 3 3v3a3 3 0 0 0 3 3h1M6 8v8'/>");
        break;
    case IconName::Logs:
        shape = QStringLiteral("<path d='M5 4h14v16H5z'/><path d='M8 8h8M8 12h8M8 16h5'/>");
        break;
    case IconName::Logo:
        shape = QStringLiteral("<path d='M5 5h14v14H5z'/><path d='m8 15 4-7 4 7M9.5 12.5h5'/>");
        break;
    case IconName::Lock:
        shape = QStringLiteral("<rect x='4' y='10' width='16' height='11' rx='2'/><path d='M8 10V7a4 4 0 0 1 8 0v3'/><circle cx='12' cy='15' r='1'/><path d='M12 16v2'/>");
        break;
    case IconName::Git:
        shape = QStringLiteral("<circle cx='6' cy='5' r='2'/><circle cx='18' cy='19' r='2'/><circle cx='18' cy='5' r='2'/><path d='M6 7v10a2 2 0 0 0 2 2h8M16 5H9a3 3 0 0 0-3 3'/>");
        break;
    case IconName::Services:
        shape = QStringLiteral("<path d='M5 4h14v16H5z'/><path d='m10 8 6 4-6 4z'/>");
        break;
    case IconName::Database:
        shape = QStringLiteral("<ellipse cx='12' cy='5' rx='8' ry='3'/><path d='M4 5v14c0 1.7 3.6 3 8 3s8-1.3 8-3V5M4 12c0 1.7 3.6 3 8 3s8-1.3 8-3'/>");
        break;
    case IconName::Notes:
        shape = QStringLiteral("<path d='M6 3h9l4 4v14H6z'/><path d='M14 3v5h5M9 12h7M9 16h7'/>");
        break;
    }
    return QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='#ffffff' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'>%1</svg>")
        .arg(shape);
}

} // namespace

QIcon IconLoader::icon(IconName name, const QColor& tint, int logicalSize, qreal dpr)
{
    static QHash<QString, QIcon> cache;
    const QString key = QStringLiteral("%1:%2:%3:%4")
                            .arg(static_cast<int>(name))
                            .arg(tint.name(QColor::HexArgb))
                            .arg(logicalSize)
                            .arg(dpr, 0, 'f', 2);
    const auto found = cache.constFind(key);
    if (found != cache.cend()) {
        return *found;
    }

    QSvgRenderer renderer(svgSource(name).toUtf8());
    const int pixelSize = qMax(1, qRound(static_cast<qreal>(logicalSize) * dpr));
    QPixmap pixmap(pixelSize, pixelSize);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    renderer.render(&painter, QRectF(1, 1, logicalSize - 2, logicalSize - 2));
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(QRect(QPoint(0, 0), pixmap.size()), tint);
    const QIcon result(pixmap);
    cache.insert(key, result);
    return result;
}

QString IconLoader::licenseNotice()
{
    return QStringLiteral("DevHub authored outline icons; CC0 1.0 Universal.");
}

} // namespace devhub::presentation::icons
