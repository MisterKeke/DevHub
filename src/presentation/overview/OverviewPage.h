#pragma once

#include "../../application/overview/WorkspaceOverviewSnapshot.h"
#include <QWidget>
#include <QVector>

class QGridLayout;
class QLabel;
class QResizeEvent;
class QFrame;
class QTimer;
namespace devhub::application::commands { class CommandRegistry; }

namespace devhub::presentation::overview {

class OverviewPage final : public QWidget
{
    Q_OBJECT
public:
    explicit OverviewPage(application::commands::CommandRegistry& commands, QWidget* parent = nullptr);
    void setSnapshot(application::overview::WorkspaceOverviewSnapshot snapshot);
protected:
    void resizeEvent(QResizeEvent* event) override;
private:
    struct Card final {
        QFrame* frame = nullptr;
        QLabel* icon = nullptr;
        QLabel* title = nullptr;
        QLabel* status = nullptr;
        QLabel* summary = nullptr;
        QWidget* details = nullptr;
        QLabel* freshness = nullptr;
        QLabel* error = nullptr;
        QWidget* actions = nullptr;
    };
    void rebuildGrid();
    void updateCards();
    void updateFreshness();
    int columnsForWidth() const;
    application::commands::CommandRegistry& m_commands;
    application::overview::WorkspaceOverviewSnapshot m_snapshot;
    QWidget* m_gridHost = nullptr;
    QGridLayout* m_grid = nullptr;
    QVector<Card> m_cards;
    int m_columns = 0;
    QTimer* m_freshnessTimer = nullptr;
};

} // namespace devhub::presentation::overview
