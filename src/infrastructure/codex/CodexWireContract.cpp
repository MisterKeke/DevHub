#include "CodexWireContract.h"
#include "CodexVersionSupport.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <cmath>

static void initializeCodexContractResource() { Q_INIT_RESOURCE(codex_contracts); }

namespace devhub::infrastructure::codex {
namespace {
bool hasType(const QString& type, const QJsonValue& value)
{
    if (type == "object") return value.isObject();
    if (type == "array") return value.isArray();
    if (type == "string") return value.isString();
    if (type == "boolean") return value.isBool();
    if (type == "null") return value.isNull();
    if (type == "number") return value.isDouble() && std::isfinite(value.toDouble());
    if (type == "integer") return value.isDouble() && std::isfinite(value.toDouble())
        && std::floor(value.toDouble()) == value.toDouble();
    return false;
}
}
CodexWireContract::CodexWireContract(QString version)
{
    initializeCodexContractResource();
    const bool loaded = setVersion(version);
    Q_UNUSED(loaded);
}
bool CodexWireContract::setVersion(const QString& version)
{
    if (!CodexVersionSupport::isSupported(version)) return false;
    QFile file(QStringLiteral(":/codex/%1/experimental.json").arg(version));
    if (!file.open(QIODevice::ReadOnly)) return false;
    const auto snapshot = QJsonDocument::fromJson(file.readAll()).object();
    if (snapshot.value("codexVersion").toString() != version) return false;
    m_contracts = snapshot.value("contracts").toObject();
    m_definitions = snapshot.value("definitions").toObject();
    m_version = version;
    return !m_contracts.isEmpty() && !m_definitions.isEmpty();
}
bool CodexWireContract::contains(const QString& contract) const { return m_contracts.contains(contract); }
bool CodexWireContract::accepts(const QString& contract, const QJsonValue& value) const
{
    int budget = 100000;
    return contains(contract) && validate(m_contracts.value(contract), value, 0, budget);
}
bool CodexWireContract::validate(const QJsonValue& schema, const QJsonValue& value, int depth, int& budget) const
{
    if (depth > 64 || --budget < 0 || value.isUndefined()) return false;
    if (schema.isBool()) return schema.toBool();
    if (!schema.isObject()) return false;
    const auto node = schema.toObject();
    static const QSet<QString> supported{"$ref", "anyOf", "oneOf", "allOf", "type", "enum", "properties",
        "required", "additionalProperties", "items", "format", "default", "minimum", "maximum", "minLength"};
    for (auto it = node.begin(); it != node.end(); ++it) if (!supported.contains(it.key())) return false;
    const auto ref = node.value("$ref").toString();
    if (!ref.isEmpty()) {
        const auto prefix = QStringLiteral("#/definitions/");
        if (!ref.startsWith(prefix) || !m_definitions.contains(ref.mid(prefix.size()))
            || !validate(m_definitions.value(ref.mid(prefix.size())), value, depth + 1, budget)) return false;
    }
    for (const auto& key : {"anyOf", "oneOf", "allOf"}) {
        if (!node.contains(QLatin1String(key))) continue;
        int matches = 0;
        const auto alternatives = node.value(QLatin1String(key)).toArray();
        for (const auto& alternative : alternatives) if (validate(alternative, value, depth + 1, budget)) ++matches;
        if ((qstrcmp(key, "anyOf") == 0 && matches == 0)
            || (qstrcmp(key, "oneOf") == 0 && matches != 1)
            || (qstrcmp(key, "allOf") == 0 && matches != alternatives.size())) return false;
    }
    const auto type = node.value("type");
    if (type.isString() && !hasType(type.toString(), value)) return false;
    if (type.isArray()) {
        bool found = false;
        for (const auto& variant : type.toArray()) found = found || hasType(variant.toString(), value);
        if (!found) return false;
    }
    if (node.contains("enum") && !node.value("enum").toArray().contains(value)) return false;
    if (value.isString() && node.contains("minLength") && value.toString().toUcs4().size() < node.value("minLength").toInteger()) return false;
    if (value.isDouble() && node.contains("minimum") && value.toDouble() < node.value("minimum").toDouble()) return false;
    if (value.isDouble() && node.contains("maximum") && value.toDouble() > node.value("maximum").toDouble()) return false;
    if (value.isObject()) {
        const auto object = value.toObject();
        for (const auto& required : node.value("required").toArray()) if (!object.contains(required.toString())) return false;
        const auto properties = node.value("properties").toObject();
        const auto additional = node.value("additionalProperties");
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (properties.contains(it.key())) {
                if (!validate(properties.value(it.key()), it.value(), depth + 1, budget)) return false;
            } else if ((additional.isBool() && !additional.toBool())
                || (additional.isObject() && !validate(additional, it.value(), depth + 1, budget))) return false;
        }
    }
    if (value.isArray() && node.contains("items"))
        for (const auto& item : value.toArray()) if (!validate(node.value("items"), item, depth + 1, budget)) return false;
    return budget >= 0;
}
}
