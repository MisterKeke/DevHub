#pragma once
#include <QTextBrowser>
namespace devhub::presentation::widgets {
class SafeMarkdownBrowser final : public QTextBrowser {
    Q_OBJECT
public:
    enum class ResourcePolicy { BlockAll, NotesQrc };
    explicit SafeMarkdownBrowser(QWidget* parent = nullptr, ResourcePolicy policy = ResourcePolicy::BlockAll);
    void setSourceText(const QString& source, bool markdown = true);
    [[nodiscard]] static bool acceptsLink(const QUrl& url);
protected:
    QVariant loadResource(int type, const QUrl& name) override;
private:
    ResourcePolicy m_policy;
};
}
