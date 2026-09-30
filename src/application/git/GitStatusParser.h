#pragma once
#include "GitTypes.h"

namespace devhub::application::git {
class GitStatusParser final {
public:
    [[nodiscard]] static GitRepositoryState parse(const QByteArray& output);
};
}
