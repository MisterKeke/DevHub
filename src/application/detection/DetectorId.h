#pragma once
#include <QString>
#include <utility>

namespace devhub::application::detection {
class DetectorId final
{
public:
    explicit DetectorId(QString value) : m_value(std::move(value)) {}
    [[nodiscard]] const QString& value() const noexcept { return m_value; }
    friend bool operator==(const DetectorId&, const DetectorId&) = default;
private:
    QString m_value;
};
}
