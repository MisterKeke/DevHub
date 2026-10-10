#include "AssistantPage.h"
#include "application/assistant/AssistantApprovalCoordinator.h"
#include "application/assistant/AssistantToolRuntime.h"
#include "presentation/widgets/SafeMarkdownBrowser.h"
#include "presentation/theme/ThemeTokens.h"
#include <QApplication>
#include <QAbstractTextDocumentLayout>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QClipboard>
#include <QCheckBox>
#include <QDesktopServices>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextDocument>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QTimer>
#include <QtMath>
#include <utility>

namespace devhub::presentation::assistant {
using namespace application::assistant;
AssistantPage::AssistantPage(AssistantController& controller, QWidget* parent) : QWidget(parent), m_controller(controller)
{
    setObjectName(QStringLiteral("assistantPage"));
    auto* root = new QVBoxLayout(this); root->setContentsMargins(24, 20, 24, 20); root->setSpacing(10);
    auto* heading = new QHBoxLayout;
    auto* label = new QLabel(tr("Assistant"), this); label->setFont(theme::ThemeTokens::font(theme::TypographyToken::PageTitle));
    m_title = new QLabel(this); m_title->setTextFormat(Qt::PlainText);
    m_new = new QPushButton(tr("New Chat"), this); m_new->setObjectName(QStringLiteral("assistantNewChat"));
    heading->addWidget(label); heading->addWidget(m_title, 1); heading->addWidget(m_new); root->addLayout(heading);
    m_status = new QLabel(this); m_status->setObjectName(QStringLiteral("assistantStatus")); m_status->setTextFormat(Qt::PlainText); m_status->setWordWrap(true); root->addWidget(m_status);
    auto* contextRow = new QHBoxLayout;
    m_context = new QLabel(this); m_context->setObjectName(QStringLiteral("assistantContext")); m_context->setTextFormat(Qt::PlainText); m_context->setWordWrap(true);
    m_reconnect = new QPushButton(tr("Reconnect"), this); m_reconnect->setObjectName(QStringLiteral("assistantReconnect"));
    contextRow->addWidget(m_context, 1); contextRow->addWidget(m_reconnect); root->addLayout(contextRow);
    auto* disclosureRow = new QHBoxLayout;
    m_allowNotes = new QCheckBox(tr("Allow note text for this chat"), this); m_allowNotes->setObjectName(QStringLiteral("assistantAllowNoteText"));
    m_allowFiles = new QCheckBox(tr("Allow file text for this chat"), this); m_allowFiles->setObjectName(QStringLiteral("assistantAllowFileText"));
    m_allowNotes->setToolTip(tr("Allows read-only Assistant tools to return bounded text from notes in the selected workspace."));
    m_allowFiles->setToolTip(tr("Allows read-only Assistant tools to return bounded text from files in the selected workspace."));
    m_toolActivity = new QLabel(this); m_toolActivity->setObjectName(QStringLiteral("assistantToolActivity"));
    m_toolActivity->setTextFormat(Qt::PlainText); m_toolActivity->setAccessibleName(tr("Assistant tool activity"));
    disclosureRow->addWidget(m_allowNotes); disclosureRow->addWidget(m_allowFiles); disclosureRow->addWidget(m_toolActivity, 1);
    root->addLayout(disclosureRow);
    auto* uncertainAction = new QLabel(this); uncertainAction->setObjectName(QStringLiteral("assistantUncertainAction"));
    uncertainAction->setTextFormat(Qt::PlainText); uncertainAction->setWordWrap(true); uncertainAction->hide(); root->addWidget(uncertainAction);
    connect(&controller, &AssistantController::toolMutationUncertain, uncertainAction, [uncertainAction](const QString& name) {
        uncertainAction->setText(tr("The outcome of %1 is uncertain. Check the affected service or note before requesting it again. The action will not be retried automatically.").arg(name));
        uncertainAction->show();
    });
    m_approvalCards = new QVBoxLayout; root->addLayout(m_approvalCards);
    connect(&controller, &AssistantController::approvalCoordinatorChanged, this, &AssistantPage::refreshApprovals);
    m_scroll = new QScrollArea(this); m_scroll->setObjectName(QStringLiteral("assistantTranscript")); m_scroll->setWidgetResizable(true); m_scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(m_scroll); m_messages = new QVBoxLayout(content); m_messages->setContentsMargins(0, 0, 0, 0); m_messages->setSpacing(12); m_messages->addStretch();
    m_scroll->setWidget(content); root->addWidget(m_scroll, 1);
    connect(m_scroll->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (!m_rendering && !m_adjustingScroll) { ++m_scrollRevision; m_followLatest = m_scroll->verticalScrollBar()->maximum() - value <= 24; }
    });
    connect(m_scroll->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this](int, int maximum) {
        if (shouldFollow()) { const QScopedValueRollback<bool> adjusting(m_adjustingScroll, true); m_scroll->verticalScrollBar()->setValue(maximum); }
    });
    m_latest = new QPushButton(tr("Jump to latest"), this); m_latest->setObjectName(QStringLiteral("assistantLatest")); m_latest->hide(); root->addWidget(m_latest, 0, Qt::AlignRight);
    m_composer = new QPlainTextEdit(this); m_composer->setObjectName(QStringLiteral("assistantComposer")); m_composer->setAccessibleName(tr("Assistant message"));
    m_composer->setPlaceholderText(tr("Write a message. Enter sends; Shift+Enter adds a line.")); m_composer->setMinimumHeight(85); m_composer->setMaximumHeight(150); m_composer->installEventFilter(this); root->addWidget(m_composer);
    m_composer->setTabChangesFocus(true);
    auto* actions = new QHBoxLayout;
    m_send = new QPushButton(tr("Send"), this); m_send->setObjectName(QStringLiteral("assistantSend"));
    m_stop = new QPushButton(tr("Stop"), this); m_stop->setObjectName(QStringLiteral("assistantStop"));
    m_error = new QLabel(this); m_error->setObjectName(QStringLiteral("assistantError")); m_error->setWordWrap(true); m_error->setTextFormat(Qt::PlainText);
    actions->addWidget(m_send); actions->addWidget(m_stop); actions->addWidget(m_error, 1); root->addLayout(actions);
    for (auto* button : {m_send, m_stop, m_new, m_reconnect, m_latest}) button->setAccessibleName(button->text());
    setTabOrder(m_composer, m_send); setTabOrder(m_send, m_stop); setTabOrder(m_stop, m_new); setTabOrder(m_new, m_reconnect);
    connect(m_send, &QPushButton::clicked, this, &AssistantPage::sendDraft);
    connect(m_stop, &QPushButton::clicked, &controller, &AssistantController::stopTurn);
    connect(m_new, &QPushButton::clicked, &controller, &AssistantController::newChat);
    connect(m_reconnect, &QPushButton::clicked, &controller, &AssistantController::reconnect);
    connect(m_latest, &QPushButton::clicked, this, [this] { m_followLatest = true; m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum()); m_latest->hide(); });
    connect(m_composer, &QPlainTextEdit::textChanged, this, &AssistantPage::refreshState);
    m_allowNotes->setChecked(controller.notesDisclosureGranted());
    m_allowFiles->setChecked(controller.filesDisclosureGranted());
    connect(m_allowNotes, &QCheckBox::toggled, this, [this] {
        m_controller.setToolDisclosureGrants(m_allowNotes->isChecked(), m_allowFiles->isChecked());
    });
    connect(m_allowFiles, &QCheckBox::toggled, this, [this] {
        m_controller.setToolDisclosureGrants(m_allowNotes->isChecked(), m_allowFiles->isChecked());
    });
    connect(&controller, &AssistantController::toolDisclosureChanged, this, [this](bool notes, bool files) {
        const QSignalBlocker notesBlock(m_allowNotes), filesBlock(m_allowFiles);
        m_allowNotes->setChecked(notes); m_allowFiles->setChecked(files); refreshState();
    });
    connect(&controller, &AssistantController::toolActivityChanged, this,
        [this](const QString& tool, bool active, bool success) {
            if (active) { if (!m_activeTools.contains(tool)) m_activeTools.append(tool); }
            else m_activeTools.removeAll(tool);
            QStringList labels;
            for (const auto& name : m_activeTools) labels.append(name);
            m_toolActivity->setText(!labels.isEmpty() ? tr("Using %1…").arg(labels.join(QStringLiteral(", ")))
                : tool.isEmpty() ? QString{} : tr("Finished %1 (%2)").arg(tool, success ? tr("success") : tr("not completed")));
        });
    connect(&controller, &AssistantController::stateChanged, this, &AssistantPage::refreshState);
    connect(&controller, &AssistantController::messageAdded, this, &AssistantPage::addMessage);
    connect(&controller, &AssistantController::messageChanged, this, &AssistantPage::updateMessage);
    connect(&controller, &AssistantController::sendAdmitted, this, [this](const QString& source) { if (m_composer->toPlainText() == source) m_composer->clear(); });
    connect(&controller, &AssistantController::transcriptReset, this, [this] {
        ++m_scrollGeneration;
        m_followLatest = true;
        for (const auto& card : std::as_const(m_cards)) delete card.widget;
        m_cards.clear(); m_latest->hide(); refreshState();
    });
    for (const auto& value : controller.messages()) addMessage(value.id);
    refreshState();
    refreshApprovals();
}
void AssistantPage::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    if (auto* approvals = m_controller.approvals()) approvals->revokeAll();
}
void AssistantPage::refreshApprovals() {
    while (auto* item = m_approvalCards->takeAt(0)) {
        if (item->widget()) { item->widget()->setEnabled(false); item->widget()->hide(); item->widget()->deleteLater(); }
        delete item;
    }
    auto* approvals = m_controller.approvals(); if (!approvals) { refreshState(); return; }
    for (const auto& request : approvals->pending()) {
        auto* card = new QWidget(this); card->setObjectName(QStringLiteral("assistantApprovalCard"));
        auto* layout = new QVBoxLayout(card);
        auto* label = new QLabel(tr("Assistant requests: %1\nWorkspace: %2\nEffect: %3\nExpires in 30 seconds; approval applies once.")
            .arg(AssistantToolRuntime::internalName(request.tool), request.workspace, request.effect), card);
        label->setTextFormat(Qt::PlainText); label->setWordWrap(true); layout->addWidget(label);
        if (!request.preview.isEmpty() || request.tool == QStringLiteral("devhub_notes_edit")) {
            auto* preview = new QPlainTextEdit(card); preview->setObjectName(QStringLiteral("assistantApprovalPreview"));
            preview->setReadOnly(true); preview->setPlainText(request.preview); preview->setMaximumHeight(150);
            preview->setAccessibleName(tr("Proposed note content")); layout->addWidget(preview);
        }
        auto* row = new QHBoxLayout;
        auto* deny = new QPushButton(tr("Deny"), card); deny->setObjectName(QStringLiteral("assistantApprovalDeny"));
        auto* allow = new QPushButton(tr("Allow once"), card); allow->setObjectName(QStringLiteral("assistantApprovalAllow"));
        deny->setDefault(true); allow->setAutoDefault(false);
        deny->setAccessibleName(tr("Deny Assistant action")); allow->setAccessibleName(tr("Allow Assistant action once"));
        row->addWidget(deny); row->addWidget(allow); row->addStretch(); layout->addLayout(row); m_approvalCards->addWidget(card);
        connect(deny, &QPushButton::clicked, approvals, [approvals, id = request.id] { (void)approvals->decide(id, false); });
        connect(allow, &QPushButton::clicked, approvals, [approvals, id = request.id] { (void)approvals->decide(id, true); });
    }
    refreshState();
}
void AssistantPage::activate() { m_controller.open(); m_composer->setFocus(); }
void AssistantPage::clearDraft() { m_composer->clear(); m_preedit = false; }
bool AssistantPage::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_composer) {
        if (event->type() == QEvent::FocusOut) m_preedit = false;
        if (event->type() == QEvent::InputMethod) m_preedit = !static_cast<QInputMethodEvent*>(event)->preeditString().isEmpty();
        if (event->type() == QEvent::KeyPress) {
            const auto* key = static_cast<QKeyEvent*>(event);
            if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && !m_preedit
                && key->modifiers() == Qt::NoModifier) { if (!key->isAutoRepeat()) sendDraft(); return true; }
        }
    }
    return QWidget::eventFilter(watched, event);
}
void AssistantPage::sendDraft() { (void)m_controller.send(m_composer->toPlainText()); refreshState(); }
void AssistantPage::refreshState()
{
    m_title->setText(m_controller.conversation().title); m_status->setText(m_controller.statusText()); m_error->setText(m_controller.errorText());
    const auto context = m_controller.conversation().context;
    m_context->setText(context.workspaceId.isEmpty() ? tr("Context: General chat · no workspace access") : tr("Context: %1 · no native workspace access").arg(context.name));
    const auto bytes = m_composer->toPlainText().toUtf8().size();
    m_send->setEnabled(m_controller.canSend() && !m_composer->toPlainText().trimmed().isEmpty() && bytes <= 64 * 1024);
    const bool workspaceSelected = !context.workspaceId.isEmpty();
    m_allowNotes->setEnabled(workspaceSelected && m_controller.accessAllowed());
    m_allowFiles->setEnabled(workspaceSelected && m_controller.accessAllowed());
    if (bytes > 64 * 1024) m_error->setText(tr("The message exceeds the 64 KiB text limit."));
    m_stop->setEnabled(m_controller.canStop()); m_new->setEnabled(m_controller.canNewChat());
    m_reconnect->setEnabled(m_controller.canReconnect());
    for (auto it = m_cards.begin(); it != m_cards.end(); ++it) {
        it->retry->setEnabled(m_controller.canRetry(it.key()));
        const auto* value = m_controller.message(it.key());
        it->retry->setVisible(value && value->role == AssistantRole::User && (value->delivery == AssistantDelivery::NotSent
            || value->delivery == AssistantDelivery::Failed || value->delivery == AssistantDelivery::Interrupted));
        it->retry->setText(m_controller.retryIsNewTurn(it.key()) ? tr("Send again as new turn")
            : value && value->delivery == AssistantDelivery::NotSent ? tr("Retry") : tr("Check history before retry"));
    }
}
bool AssistantPage::shouldFollow() const
{
    for (const auto& card : m_cards) if (card.body->textCursor().hasSelection()) return false;
    return m_followLatest;
}
void AssistantPage::restoreScroll(bool follow, int previous)
{
    const auto generation = ++m_scrollGeneration;
    const auto revision = m_scrollRevision;
    QTimer::singleShot(0, this, [this, follow, previous, generation, revision] {
        if (generation != m_scrollGeneration) return;
        m_scroll->widget()->layout()->activate();
        bool followNow = follow && m_followLatest;
        for (const auto& card : m_cards) if (card.body->textCursor().hasSelection()) followNow = false;
        // Leave valueChanged connected to QScrollArea so the content, not just
        // the scrollbar thumb, actually moves. Ignore only our own flag update.
        const QScopedValueRollback<bool> adjusting(m_adjustingScroll, true);
        if (followNow) { m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum()); m_latest->hide(); }
        else { if (revision == m_scrollRevision) m_scroll->verticalScrollBar()->setValue(previous); m_latest->show(); }
    });
}
void AssistantPage::addMessage(const QUuid& id)
{
    if (m_cards.contains(id)) return;
    const auto* value = m_controller.message(id); if (!value) return;
    const bool follow = shouldFollow(); const int previous = m_scroll->verticalScrollBar()->value();
    auto* card = new QWidget(m_scroll->widget()); card->setObjectName(QStringLiteral("assistantMessage"));
    card->setStyleSheet(QStringLiteral("QWidget#assistantMessage { border: 1px solid palette(mid); border-radius: 8px; }"));
    auto* layout = new QVBoxLayout(card); layout->setContentsMargins(12, 10, 12, 10);
    auto* heading = new QHBoxLayout;
    auto* role = new QLabel(value->role == AssistantRole::User ? tr("You") : tr("Assistant"), card); role->setTextFormat(Qt::PlainText);
    auto* timestamp = new QLabel(value->createdAtUtc.toLocalTime().toString(QStringLiteral("HH:mm")), card);
    heading->addWidget(role); heading->addWidget(timestamp); heading->addStretch();
    auto* copy = new QPushButton(tr("Copy"), card); copy->setAccessibleName(tr("Copy message source")); heading->addWidget(copy);
    auto* retry = new QPushButton(tr("Retry"), card); retry->setVisible(value->role == AssistantRole::User); heading->addWidget(retry); layout->addLayout(heading);
    auto* body = new widgets::SafeMarkdownBrowser(card); body->setAccessibleName(role->text()); body->setObjectName(id.toString(QUuid::WithoutBraces)); body->setFrameShape(QFrame::NoFrame); body->setMinimumHeight(60); body->setMaximumHeight(600); layout->addWidget(body);
    body->setFixedHeight(60);
    connect(body->document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this, [this, body](const QSizeF& size) {
        if (m_rendering) return;
        const int height = qBound(60, qCeil(size.height()) + 12, 600);
        if (body->height() != height) body->setFixedHeight(height);
    });
    connect(body, &QTextEdit::selectionChanged, this, [this] {
        if (m_rendering) return;
        for (const auto& message : m_cards) if (message.body->textCursor().hasSelection()) { m_followLatest = false; return; }
        const auto* bar = m_scroll->verticalScrollBar(); m_followLatest = bar->maximum() - bar->value() <= 24;
    });
    auto* status = new QLabel(card); status->setTextFormat(Qt::PlainText); layout->addWidget(status);
    m_messages->insertWidget(m_messages->count() - 1, card); m_cards.insert(id, {card, body, status, retry});
    connect(copy, &QPushButton::clicked, this, [this, id] { if (const auto* message = m_controller.message(id)) QApplication::clipboard()->setText(message->source); });
    connect(retry, &QPushButton::clicked, this, [this, id] { m_controller.retry(id); });
    connect(body, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        if (widgets::SafeMarkdownBrowser::acceptsLink(url) && !QDesktopServices::openUrl(url)) m_error->setText(tr("The system could not open this link."));
    });
    updateMessage(id); restoreScroll(follow, previous);
}
void AssistantPage::updateMessage(const QUuid& id)
{
    const auto found = m_cards.find(id); const auto* value = m_controller.message(id); if (found == m_cards.end() || !value) return;
    const bool follow = shouldFollow(); const int previous = m_scroll->verticalScrollBar()->value();
    m_rendering = true;
    found->body->setSourceText(value->source, value->role == AssistantRole::Assistant);
    m_rendering = false;
    found->body->setFixedHeight(qBound(60, qCeil(found->body->document()->size().height()) + 12, 600));
    const QString status = value->truncated ? tr("Text limit reached · partial answer retained")
        : value->delivery == AssistantDelivery::NotSent ? tr("Not sent") : value->delivery == AssistantDelivery::Queued ? tr("Awaiting acknowledgement")
        : value->delivery == AssistantDelivery::Accepted ? tr("Responding…") : value->delivery == AssistantDelivery::Failed ? tr("Failed")
        : value->delivery == AssistantDelivery::Interrupted ? tr("Interrupted · partial text retained") : value->delivery == AssistantDelivery::Uncertain ? tr("Delivery uncertain") : QString{};
    found->status->setText(status); refreshState(); restoreScroll(follow, previous);
}
}
