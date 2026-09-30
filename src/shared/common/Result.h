#pragma once

#include "../../domain/errors/AppError.h"

#include <variant>
#include <optional>
#include <utility>

namespace devhub::shared::common {

template<typename T>
class Result final
{
public:
    [[nodiscard]] static Result success(T value) { return Result(std::move(value)); }
    [[nodiscard]] static Result failure(domain::errors::AppError error) { return Result(std::move(error)); }

    [[nodiscard]] bool hasValue() const noexcept { return std::holds_alternative<T>(m_value); }
    [[nodiscard]] explicit operator bool() const noexcept { return hasValue(); }
    [[nodiscard]] T& value() & { return std::get<T>(m_value); }
    [[nodiscard]] const T& value() const& { return std::get<T>(m_value); }
    [[nodiscard]] T&& value() && { return std::get<T>(std::move(m_value)); }
    [[nodiscard]] domain::errors::AppError& error() & { return std::get<domain::errors::AppError>(m_value); }
    [[nodiscard]] const domain::errors::AppError& error() const& { return std::get<domain::errors::AppError>(m_value); }

private:
    explicit Result(T value) : m_value(std::move(value)) {}
    explicit Result(domain::errors::AppError error) : m_value(std::move(error)) {}

    std::variant<T, domain::errors::AppError> m_value;
};

template<>
class Result<void> final
{
public:
    [[nodiscard]] static Result success() { return Result(); }
    [[nodiscard]] static Result failure(domain::errors::AppError error) { return Result(std::move(error)); }

    [[nodiscard]] bool hasValue() const noexcept { return !m_error.has_value(); }
    [[nodiscard]] explicit operator bool() const noexcept { return hasValue(); }
    [[nodiscard]] domain::errors::AppError& error() & { return *m_error; }
    [[nodiscard]] const domain::errors::AppError& error() const& { return *m_error; }

private:
    Result() = default;
    explicit Result(domain::errors::AppError error) : m_error(std::move(error)) {}

    std::optional<domain::errors::AppError> m_error;
};

struct OperationCancelled final {};

template<typename T>
using TaskOutcome = std::variant<Result<T>, OperationCancelled>;

} // namespace devhub::shared::common
