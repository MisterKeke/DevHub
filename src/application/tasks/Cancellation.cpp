#include "Cancellation.h"

#include <utility>

namespace devhub::application::tasks {

CancellationToken::CancellationToken(std::shared_ptr<std::atomic_bool> state)
    : m_state(std::move(state))
{
}

bool CancellationToken::isCancellationRequested() const noexcept
{
    return m_state && m_state->load(std::memory_order_acquire);
}

CancellationSource::CancellationSource()
    : m_state(std::make_shared<std::atomic_bool>(false))
{
}

CancellationToken CancellationSource::token() const
{
    return CancellationToken(m_state);
}

void CancellationSource::requestCancellation() const noexcept
{
    m_state->store(true, std::memory_order_release);
}

bool CancellationSource::isCancellationRequested() const noexcept
{
    return m_state->load(std::memory_order_acquire);
}

} // namespace devhub::application::tasks
