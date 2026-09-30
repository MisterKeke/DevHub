#pragma once

#include "StartupFailure.h"

#include <QDialog>

namespace devhub::app {

class RecoveryModeWindow final : public QDialog
{
    Q_OBJECT

public:
    explicit RecoveryModeWindow(StartupFailure failure, QWidget* parent = nullptr);
    [[nodiscard]] StartupRecoveryAction selectedAction() const noexcept { return m_selectedAction; }

private:
    StartupRecoveryAction m_selectedAction = StartupRecoveryAction::Quit;
};

} // namespace devhub::app
