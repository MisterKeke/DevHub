#pragma once
#include "GitTypes.h"
#include "../../shared/common/Result.h"

namespace devhub::application::git {
class GitExecutableLocator final {
public:
    [[nodiscard]] shared::common::Result<GitExecutable> locate() const;
    [[nodiscard]] shared::common::Result<GitExecutable> validate(const QString& path) const;
    void store(const GitExecutable& executable) const;
};
}
