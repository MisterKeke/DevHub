#pragma once

#include <atomic>
#include <memory>

namespace devhub::application::tasks {

class CancellationToken final
{
public:
    CancellationToken() = default;
    [[nodiscard]] bool isCancellationRequested() const noexcept;

private:
    explicit CancellationToken(std::shared_ptr<std::atomic_bool> state);
    std::shared_ptr<std::atomic_bool> m_state;

    friend class CancellationSource;
};

class CancellationSource final
{
public:
    CancellationSource();
    [[nodiscard]] CancellationToken token() const;
    void requestCancellation() const noexcept;
    [[nodiscard]] bool isCancellationRequested() const noexcept;

private:
    std::shared_ptr<std::atomic_bool> m_state;
};

} // namespace devhub::application::tasks
