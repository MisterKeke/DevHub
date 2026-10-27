#include "OverviewPage.h"
#include "../../application/commands/CommandRegistry.h"
#include "../icons/IconLoader.h"
#include "../theme/ThemePalette.h"

#include <QDateTime>
#include <QAction>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QFont>
#include <QLayoutItem>

namespace devhub::presentation::overview {

OverviewPage::OverviewPage(application::commands::CommandRegistry& commands, QWidget* parent)
    : QWidget(parent), m_commands(commands)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(28, 22, 28, 28);
    outer->setSpacing(8);
    auto* pageHeading = new QLabel(tr("Workspace Overview"), this);
    pageHeading->setObjectName(QStringLiteral("workspaceOverviewTitle"));
    QFont font = pageHeading->font(); font.setPointSize(font.pointSize() + 5); font.setBold(true); pageHeading->setFont(font);
    outer->addWidget(pageHeading);
    auto* description = new QLabel(tr("Your development workspace at a glance."), this);
    description->setObjectName(QStringLiteral("workspaceOverviewDescription"));
    description->setStyleSheet(QStringLiteral("color: %1;").arg(
        devhub::presentation::theme::ThemePalette::color(
            devhub::presentation::theme::ColorToken::TextSecondary).name()));
    outer->addWidget(description);
    auto* topGap = new QWidget(this);
    topGap->setFixedHeight(6);
    outer->addWidget(topGap);
    m_gridHost = new QWidget(this);
    m_grid = new QGridLayout(m_gridHost);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(14);
    outer->addWidget(m_gridHost, 1);
    const QStringList ids{QStringLiteral("git"), QStringLiteral("services"), QStringLiteral("ports"),
                          QStringLiteral("logs"), QStringLiteral("database"), QStringLiteral("notes")};
    const QStringList titles{tr("Git Status"), tr("Services / Run"), tr("Open Ports"),
                             tr("Recent Logs"), tr("Database (SQLite)"), tr("Notes")};
    const QVector<devhub::presentation::icons::IconName> iconNames{
        devhub::presentation::icons::IconName::Git, devhub::presentation::icons::IconName::Services,
        devhub::presentation::icons::IconName::Ports, devhub::presentation::icons::IconName::Logs,
        devhub::presentation::icons::IconName::Database, devhub::presentation::icons::IconName::Notes};
    for (int i = 0; i < ids.size(); ++i) {
        auto* frame = new QFrame(m_gridHost);
        frame->setObjectName(QStringLiteral("overviewCard"));
        frame->setFrameShape(QFrame::StyledPanel);
        frame->setMinimumHeight(240);
        auto* layout = new QVBoxLayout(frame);
        layout->setContentsMargins(14, 12, 14, 12);
        layout->setSpacing(7);
        auto* heading = new QWidget(frame);
        auto* headingLayout = new QHBoxLayout(heading);
        headingLayout->setContentsMargins(0, 0, 0, 0);
        headingLayout->setSpacing(8);
        auto* icon = new QLabel(heading);
        icon->setPixmap(devhub::presentation::icons::IconLoader::icon(iconNames.at(i),
            devhub::presentation::theme::ThemePalette::color(
                devhub::presentation::theme::ColorToken::Accent), 19).pixmap(19, 19));
        auto* title = new QLabel(titles[i], heading);
        title->setObjectName(QStringLiteral("overviewCardTitle"));
        QFont titleFont = title->font(); titleFont.setBold(true); title->setFont(titleFont);
        auto* status = new QLabel(heading);
        status->setObjectName(QStringLiteral("overviewStatus"));
        status->setAlignment(Qt::AlignCenter);
        status->hide();
        headingLayout->addWidget(icon);
        headingLayout->addWidget(title);
        headingLayout->addStretch();
        headingLayout->addWidget(status);
        layout->addWidget(heading);
        auto* summary = new QLabel(tr("Select a workspace to see its current state."), frame);
        summary->setObjectName(QStringLiteral("overviewSummary"));
        summary->setWordWrap(true);
        summary->setMinimumHeight(38);
        auto* details = new QWidget(frame);
        details->setObjectName(QStringLiteral("overviewDetails"));
        auto* detailsLayout = new QVBoxLayout(details);
        detailsLayout->setContentsMargins(0, 2, 0, 2);
        detailsLayout->setSpacing(3);
        auto* freshness = new QLabel(frame);
        freshness->setObjectName(QStringLiteral("overviewFreshness"));
        auto* error = new QLabel(frame); error->setWordWrap(true);
        error->setStyleSheet(QStringLiteral("color: %1;").arg(
            devhub::presentation::theme::ThemePalette::color(
                devhub::presentation::theme::ColorToken::Error).name()));
        auto* actionRow = new QWidget(frame);
        auto* actionLayout = new QHBoxLayout(actionRow); actionLayout->setContentsMargins(0, 4, 0, 0); actionLayout->setSpacing(6);
        layout->addWidget(summary); layout->addWidget(details); layout->addWidget(error);
        layout->addStretch(); layout->addWidget(freshness); layout->addWidget(actionRow);
        m_cards.push_back({frame, icon, title, status, summary, details, freshness, error, actionRow});
        frame->setProperty("overviewSectionId", ids[i]);
    }
    rebuildGrid();
    updateCards();
    m_freshnessTimer = new QTimer(this);
    m_freshnessTimer->setInterval(60000);
    connect(m_freshnessTimer, &QTimer::timeout, this, &OverviewPage::updateFreshness);
    m_freshnessTimer->start();
}

void OverviewPage::setSnapshot(application::overview::WorkspaceOverviewSnapshot snapshot)
{
    m_snapshot = std::move(snapshot);
    updateCards();
}

int OverviewPage::columnsForWidth() const
{
    if (width() < 700) return 1;
    if (width() < 1120) return 2;
    return 3;
}

void OverviewPage::rebuildGrid()
{
    const int columns = columnsForWidth();
    if (columns == m_columns) return;
    m_columns = columns;
    while (QLayoutItem* item = m_grid->takeAt(0)) { if (item->widget()) item->widget()->setParent(m_gridHost); delete item; }
    for (int i = 0; i < m_cards.size(); ++i) m_grid->addWidget(m_cards[i].frame, i / m_columns, i % m_columns);
    for (int col = 0; col < m_columns; ++col) m_grid->setColumnStretch(col, 1);
}

void OverviewPage::updateCards()
{
    const QStringList fallbackSummaries{
        tr("Branch, sync state, and working-tree changes will appear here."),
        tr("Start and monitor the services you use for this workspace."),
        tr("See which local processes are listening on network ports."),
        tr("Follow recent output from your log sources."),
        tr("Open saved SQLite references and run read-only queries."),
        tr("Keep workspace notes alongside your development context.")};
    const QStringList navigationIds{QStringLiteral("nav.git"), QStringLiteral("nav.services"),
        QStringLiteral("nav.ports"), QStringLiteral("nav.logs"), QStringLiteral("nav.database"),
        QStringLiteral("nav.notes")};
    const QStringList navigationLabels{tr("Open Git"), tr("Manage services"), tr("View ports"),
        tr("Open logs"), tr("Browse databases"), tr("Open notes")};
    for (int cardIndex = 0; cardIndex < m_cards.size(); ++cardIndex) {
        auto& card = m_cards[cardIndex];
        const QString id = card.frame->property("overviewSectionId").toString();
        const auto it = m_snapshot.sections.constFind(id);
        while (QLayoutItem* item = card.actions->layout()->takeAt(0)) { if (item->widget()) item->widget()->deleteLater(); delete item; }
        while (QLayoutItem* item = card.details->layout()->takeAt(0)) { if (item->widget()) item->widget()->deleteLater(); delete item; }
        card.error->clear();
        card.status->clear();
        card.status->hide();
        if (it == m_snapshot.sections.cend()) {
            const int sectionIndex = cardIndex;
            card.summary->setText(m_snapshot.workspaceName.isEmpty()
                ? tr("Add a workspace to get started. %1").arg(fallbackSummaries.at(sectionIndex))
                : fallbackSummaries.at(sectionIndex));
            card.freshness->setText(m_snapshot.workspaceName.isEmpty()
                ? tr("Waiting for a workspace") : tr("Waiting for first update"));
            auto* detailsLayout = qobject_cast<QVBoxLayout*>(card.details->layout());
            auto* detail = new QLabel(m_snapshot.workspaceName.isEmpty()
                ? tr("Choose a workspace to see live details.") : tr("Refresh this module to load its latest data."), card.details);
            detail->setObjectName(QStringLiteral("overviewDetail"));
            detail->setWordWrap(true);
            detailsLayout->addWidget(detail);
            const auto command = m_commands.descriptor(navigationIds.at(sectionIndex));
            if (command.action) {
                auto* button = new QPushButton(navigationLabels.at(sectionIndex), card.actions);
                button->setObjectName(QStringLiteral("overviewAction"));
                button->setEnabled(command.availability.available);
                button->setToolTip(command.availability.available ? command.description : command.availability.reason);
                connect(button, &QPushButton::clicked, command.action, &QAction::trigger);
                qobject_cast<QHBoxLayout*>(card.actions->layout())->addWidget(button);
            }
            qobject_cast<QHBoxLayout*>(card.actions->layout())->addStretch();
            continue;
        }
        card.summary->setText(it->loading ? tr("Updating…") : it->summary);
        card.error->setText(it->error);
        if (!it->statusLabel.isEmpty()) {
            const auto tone = it->statusTone == QStringLiteral("success")
                ? devhub::presentation::theme::ColorToken::Success
                : it->statusTone == QStringLiteral("warning")
                ? devhub::presentation::theme::ColorToken::Warning
                : it->statusTone == QStringLiteral("error")
                ? devhub::presentation::theme::ColorToken::Error
                : devhub::presentation::theme::ColorToken::Info;
            card.status->setText(it->statusLabel);
            card.status->setStyleSheet(QStringLiteral(
                "QLabel#overviewStatus { color: %1; background: %2; border: 1px solid %1;"
                " border-radius: 0px; padding: 2px 8px; font-size: 9pt; }")
                .arg(devhub::presentation::theme::ThemePalette::color(tone).name(),
                     devhub::presentation::theme::ThemePalette::color(
                         devhub::presentation::theme::ColorToken::RaisedSurface).name()));
            card.status->show();
        }
        auto* detailsLayout = qobject_cast<QVBoxLayout*>(card.details->layout());
        for (const QString& detailText : it->details) {
            auto* detail = new QLabel(detailText, card.details);
            detail->setObjectName(QStringLiteral("overviewDetail"));
            detail->setWordWrap(false);
            detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
            detail->setToolTip(detailText);
            detailsLayout->addWidget(detail);
        }
        if (it->details.isEmpty()) {
            auto* detail = new QLabel(it->loading ? tr("Collecting workspace data…") : tr("No recent items."), card.details);
            detail->setObjectName(QStringLiteral("overviewDetailMuted"));
            detailsLayout->addWidget(detail);
        }
        auto* row = qobject_cast<QHBoxLayout*>(card.actions->layout());
        for (const QString& actionId : it->actions) {
            const auto command = m_commands.descriptor(actionId);
            if (!command.action) continue;
            auto* button = new QPushButton(command.title, card.actions);
            button->setObjectName(QStringLiteral("overviewAction"));
            button->setEnabled(command.availability.available);
            button->setToolTip(command.availability.available ? command.description : command.availability.reason);
            connect(button, &QPushButton::clicked, command.action, &QAction::trigger);
            row->addWidget(button);
        }
        row->addStretch();
    }
    updateFreshness();
}

void OverviewPage::updateFreshness()
{
    for (auto& card : m_cards) {
        const auto it = m_snapshot.sections.constFind(card.frame->property("overviewSectionId").toString());
        if (it == m_snapshot.sections.cend() || !it->updatedAt.isValid()) {
            card.freshness->setText(tr("Not updated yet"));
            card.freshness->setStyleSheet({});
            continue;
        }
        const bool stale = it->updatedAt.secsTo(QDateTime::currentDateTimeUtc()) > 300;
        card.freshness->setText(tr("%1 · updated %2").arg(stale ? tr("Stale") : tr("Current"),
            it->updatedAt.toLocalTime().toString(QStringLiteral("HH:mm:ss"))));
        card.freshness->setStyleSheet(stale ? QStringLiteral("color: %1;").arg(
            devhub::presentation::theme::ThemePalette::color(devhub::presentation::theme::ColorToken::Warning).name()) : QString{});
    }
}

void OverviewPage::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    rebuildGrid();
}

} // namespace devhub::presentation::overview
