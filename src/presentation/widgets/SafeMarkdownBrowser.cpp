#include "SafeMarkdownBrowser.h"
#include <QScrollBar>
#include <QTextDocument>
#include <QTextCursor>
namespace devhub::presentation::widgets {
namespace {
class SafeMarkdownDocument final : public QTextDocument {
public:
    SafeMarkdownDocument(QObject* parent, SafeMarkdownBrowser::ResourcePolicy policy) : QTextDocument(parent), m_policy(policy) {}
protected:
    QVariant loadResource(int type, const QUrl& name) override
    {
        // QTextDocument's default loader can fall back to file/data resources
        // after its parent browser returns null. Block at the document boundary.
        if (m_policy == SafeMarkdownBrowser::ResourcePolicy::NotesQrc && name.scheme() == "qrc")
            return QTextDocument::loadResource(type, name);
        return {};
    }
private:
    SafeMarkdownBrowser::ResourcePolicy m_policy;
};
}
SafeMarkdownBrowser::SafeMarkdownBrowser(QWidget* parent, ResourcePolicy policy)
    : QTextBrowser(parent), m_policy(policy)
{
    setDocument(new SafeMarkdownDocument(this, policy));
    setOpenLinks(false); setOpenExternalLinks(false); setReadOnly(true);
    setTextInteractionFlags(Qt::TextBrowserInteraction);
    setLineWrapMode(QTextEdit::WidgetWidth);
    document()->setDefaultStyleSheet(QStringLiteral("pre, code { font-family: '%1'; }").arg(font().family()));
}
QVariant SafeMarkdownBrowser::loadResource(int type, const QUrl& name)
{
    if (m_policy == ResourcePolicy::NotesQrc && name.scheme() == "qrc") return QTextBrowser::loadResource(type, name);
    return {};
}
bool SafeMarkdownBrowser::acceptsLink(const QUrl& url)
{
    return url.isValid() && !url.host().isEmpty() && url.userInfo().isEmpty() && (url.scheme() == "https" || url.scheme() == "http");
}
void SafeMarkdownBrowser::setSourceText(const QString& source, bool markdown)
{
    const int anchor = textCursor().anchor(); const int position = textCursor().position();
    const auto vertical = verticalScrollBar()->value(); const auto horizontal = horizontalScrollBar()->value();
    if (markdown) document()->setMarkdown(source, QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub) | QTextDocument::MarkdownNoHTML);
    else setPlainText(source);
    QTextCursor restored(document());
    const auto last = qMax(0, document()->characterCount() - 1);
    restored.setPosition(qMin(anchor, last)); restored.setPosition(qMin(position, last), QTextCursor::KeepAnchor);
    setTextCursor(restored); verticalScrollBar()->setValue(vertical); horizontalScrollBar()->setValue(horizontal);
}
}
