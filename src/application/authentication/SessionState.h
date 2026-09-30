#pragma once

#include <QMetaType>

namespace devhub::application::authentication {

enum class SessionState
{
    NoAccount,
    CreatingAccount,
    SignedOut,
    Authenticating,
    SignedIn,
    Locking,
    Locked,
    Unlocking,
    SigningOut,
    Cleared
};

} // namespace devhub::application::authentication

Q_DECLARE_METATYPE(devhub::application::authentication::SessionState)
