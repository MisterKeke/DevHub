#include "CommandPaletteDialog.h"

#include <QKeyEvent>
#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

namespace devhub::presentation::commands {

CommandPaletteDialog::CommandPaletteDialog(application::commands::CommandRegistry& registry, QWidget* parent)
    : QDialog(parent, Qt::Tool | Qt::FramelessWindowHint), m_registry(registry)
{
    setObjectName(QStringLiteral("commandPalette"));
    setWindowTitle(tr("Command palette"));
    setModal(true);
    setMinimumWidth(560);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 10);
    layout->addWidget(new QLabel(tr("Search commands, workspaces, services, notes, databases, and settings"), this));
    m_query = new QLineEdit(this);
    m_query->setObjectName(QStringLiteral("commandPaletteQuery"));
    m_query->setPlaceholderText(tr("Type a command…"));
    layout->addWidget(m_query);
    m_results = new QListWidget(this);
    m_results->setObjectName(QStringLiteral("commandPaletteResults"));
    m_results->setAccessibleName(tr("Search results"));
    m_results->setMinimumHeight(300);
    layout->addWidget(m_results);
    connect(m_query, &QLineEdit::textChanged, this, &CommandPaletteDialog::beginSearch);
    connect(m_results, &QListWidget::itemActivated, this, [this] { executeCurrent(); });
    connect(m_query, &QLineEdit::returnPressed, this, &CommandPaletteDialog::executeCurrent);
    connect(&m_registry, &application::commands::CommandRegistry::commandsChanged,
            this, [this] { beginSearch(m_query->text()); });
    connect(&m_registry, &application::commands::CommandRegistry::contextChanged,
            this, [this] { beginSearch(m_query->text()); });
    connect(this, &QDialog::finished, this, [this] {
        if (!m_previousFocus.isNull() && m_previousFocus->isVisible())
            m_previousFocus->setFocus(Qt::OtherFocusReason);
    });
    beginSearch({});
}

void CommandPaletteDialog::addProvider(application::commands::ICommandSearchProvider* provider)
{
    if (provider) { m_providers.push_back(provider); m_ownedProviders.emplace_back(provider); }
}

void CommandPaletteDialog::openPalette()
{
    m_acceptedSelection = false;
    m_previousFocus = QApplication::focusWidget();
    adjustSize();
    if (parentWidget()) {
        const QRect parentRect = parentWidget()->geometry();
        move(parentWidget()->mapToGlobal(QPoint((parentRect.width() - width()) / 2, 90)));
    }
    show();
    m_query->clear();
    m_query->setFocus(Qt::ShortcutFocusReason);
}

void CommandPaletteDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    m_query->setFocus(Qt::ShortcutFocusReason);
}

void CommandPaletteDialog::beginSearch(QString query)
{
    const quint64 generation = ++m_generation;
    m_results->clear();
    QTimer::singleShot(0, this, [this, generation, query = std::move(query)] {
        searchNextProvider(generation, query, 0);
    });
}

void CommandPaletteDialog::searchNextProvider(quint64 generation, QString query, int index)
{
    if (generation != m_generation) return;
    if (index >= m_providers.size()) return;
    const auto matches = m_providers[index]->search(query, m_registry.context());
    for (const auto& match : matches) {
        const auto command = m_registry.descriptor(match.commandId);
        const QString reason = command.availability.available ? match.provider : command.availability.reason;
        auto* item = new QListWidgetItem(QStringLiteral("%1    ·    %2").arg(match.title, reason), m_results);
        item->setToolTip(match.detail);
        item->setData(Qt::UserRole, match.commandId);
        item->setData(Qt::UserRole + 1, match.provider);
        item->setData(Qt::UserRole + 2, match.payload);
        if (!command.availability.available) item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
    }
    if (m_results->currentRow() < 0 && m_results->count() > 0) {
        for (int row = 0; row < m_results->count(); ++row) {
            if (m_results->item(row)->flags() & Qt::ItemIsEnabled) { m_results->setCurrentRow(row); break; }
        }
    }
    QTimer::singleShot(0, this, [this, generation, query = std::move(query), index] {
        searchNextProvider(generation, query, index + 1);
    });
}

void CommandPaletteDialog::executeCurrent()
{
    auto* item = m_results->currentItem();
    if (!item || !(item->flags() & Qt::ItemIsEnabled)) return;
    m_acceptedSelection = m_registry.execute(item->data(Qt::UserRole).toString(),
                                             item->data(Qt::UserRole + 2));
    if (m_acceptedSelection) accept();
}

} // namespace devhub::presentation::commands
