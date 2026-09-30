#pragma once
#include <QDialog>
class QCheckBox;

namespace devhub::application::support { class SupportBundleBuilder; }
namespace devhub::presentation::settings {
class SupportBundleDialog final : public QDialog {
    Q_OBJECT
public:
    SupportBundleDialog(application::support::SupportBundleBuilder& builder, QStringList moduleStatus, QWidget* parent = nullptr);
private:
    void generate();
    application::support::SupportBundleBuilder& m_builder;
    QStringList m_moduleStatus;
    QCheckBox* m_settings = nullptr;
    QCheckBox* m_logs = nullptr;
    QCheckBox* m_status = nullptr;
    QCheckBox* m_crash = nullptr;
};
} // namespace devhub::presentation::settings
