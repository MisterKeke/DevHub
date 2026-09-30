#include "WorkspaceListDelegate.h"
#include "WorkspaceListModel.h"
#include "../theme/ThemePalette.h"

#include <QPainter>

namespace devhub::presentation::workspaces {
void WorkspaceListDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    painter->save();
    const QRect r = option.rect.adjusted(3, 2, -3, -2);
    const QColor bg = (option.state & QStyle::State_Selected)
        ? devhub::presentation::theme::ThemePalette::color(devhub::presentation::theme::ColorToken::Accent).darker(250)
        : ((option.state & QStyle::State_MouseOver) ? option.palette.alternateBase().color() : option.palette.base().color());
    painter->setPen(Qt::NoPen); painter->setBrush(bg); painter->drawRoundedRect(r, 6, 6);
    const QColor fg = option.palette.text().color();
    painter->setPen(fg);
    QString title = index.data(WorkspaceListModel::FavoriteRole).toBool() ? QStringLiteral("★  ") : QStringLiteral("   ");
    title += index.data(WorkspaceListModel::NameRole).toString();
    painter->drawText(r.adjusted(12, 6, -8, -24), Qt::AlignVCenter | Qt::AlignLeft, title);
    painter->setPen(option.palette.mid().color());
    painter->drawText(r.adjusted(12, 27, -8, -3), Qt::AlignVCenter | Qt::AlignLeft,
                      QFontMetrics(option.font).elidedText(index.data(WorkspaceListModel::PathRole).toString(), Qt::ElideMiddle, r.width() - 20));
    const auto status = static_cast<domain::workspace::WorkspaceStatus>(index.data(WorkspaceListModel::StatusRole).toInt());
    if (status != domain::workspace::WorkspaceStatus::Ready) {
        painter->setPen(QColor(210, 145, 60));
        painter->drawText(r.adjusted(8, 2, -8, -2), Qt::AlignRight | Qt::AlignVCenter,
                          status == domain::workspace::WorkspaceStatus::Missing ? QObject::tr("Missing") : QObject::tr("Inaccessible"));
    }
    painter->restore();
}
QSize WorkspaceListDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const { return {180, 58}; }
}
