#include "CodexSchemaSurface.h"
#include "CodexWireContract.h"
#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QHash>
#include <QDeadlineTimer>
#include <QThread>

namespace devhub::infrastructure::codex {
using namespace application::assistant;
namespace {
struct Node { QJsonValue shape; QString file; };
QJsonObject consumedSnapshot(QJsonObject snapshot)
{
    // Only fields the client sends or reads belong to the comparison surface.
    const QHash<QString, QStringList> fields{
        {"ThreadStartParams", {"cwd", "model", "sandbox", "approvalPolicy", "historyMode", "environments", "runtimeWorkspaceRoots", "selectedCapabilityRoots", "dynamicTools", "allowProviderModelFallback"}},
        {"ThreadResumeParams", {"threadId", "cwd", "sandbox", "approvalPolicy", "runtimeWorkspaceRoots", "initialTurnsPage"}},
        {"TurnStartParams", {"threadId", "input", "environments", "runtimeWorkspaceRoots", "approvalPolicy", "sandboxPolicy"}},
        {"PluginListParams", {"cwds"}}, {"HooksListParams", {"cwds"}},
        {"Model", {"id", "model", "displayName", "hidden", "isDefault", "inputModalities", "availabilityNux"}},
        {"ExperimentalFeature", {"name", "enabled"}},
        {"PluginMarketplaceEntry", {"plugins"}}, {"PluginSummary", {"enabled"}},
        {"Thread", {"id", "cwd", "modelProvider", "historyMode", "environments", "turns"}}
    };
    auto definitions = snapshot.value("definitions").toObject();
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        if (!definitions.contains(it.key())) continue;
        auto definition = definitions.value(it.key()).toObject();
        auto properties = definition.value("properties").toObject();
        for (const auto& name : properties.keys()) if (!it.value().contains(name)) properties.remove(name);
        QJsonArray required;
        for (const auto& name : definition.value("required").toArray()) if (properties.contains(name.toString())) required.append(name);
        definition.insert("properties", properties);
        if (definition.contains("required")) definition.insert("required", required);
        definitions.insert(it.key(), definition);
    }
    snapshot.insert("definitions", definitions);
    return snapshot;
}
class Comparison {
public:
    QJsonObject files;
    QJsonObject baseline;
    CodexCompatibilityReason failure = CodexCompatibilityReason::ProtocolRequirementMissing;
    int budget = 150000;
    QSet<QString> visiting;
    QSet<QString> matchedReferences;
    int recursiveAssumptions = 0;
    QDeadlineTimer deadline{15000};
    bool withinLimits() {
        if (QThread::currentThread()->isInterruptionRequested()) { failure = CodexCompatibilityReason::Cancelled; return false; }
        if (deadline.hasExpired()) { failure = CodexCompatibilityReason::SchemaDeadline; return false; }
        if (--budget < 0) { failure = CodexCompatibilityReason::SchemaLimit; return false; }
        return true;
    }
    QJsonValue resolve(Node& node, int depth = 0) {
        if (!withinLimits()) return {};
        if (depth > 64) { failure = CodexCompatibilityReason::SchemaLimit; return {}; }
        const auto object = node.shape.toObject();
        if (!object.contains("$ref")) return node.shape;
        for (auto it = object.begin(); it != object.end(); ++it)
            if (it.key() != "$ref" && !annotation(it.key())) { failure = CodexCompatibilityReason::SchemaUnsupported; return {}; }
        const auto ref = object.value("$ref").toString();
        const auto hash = ref.indexOf('#');
        const auto filename = hash < 0 ? ref : ref.left(hash);
        const auto pointer = hash < 0 ? QString{} : ref.mid(hash + 1);
        if (filename.contains(':') || filename.startsWith('/') || filename.contains('\\')
            || (!pointer.isEmpty() && !pointer.startsWith('/'))) {
            failure = CodexCompatibilityReason::SchemaUnsupported; return {};
        }
        if (!filename.isEmpty()) {
            const auto slash = node.file.lastIndexOf('/');
            const auto path = QDir::cleanPath((slash < 0 ? QString{} : node.file.left(slash + 1)) + filename);
            if (path.startsWith("../") || path == ".." || !files.contains(path)
                || (path == "baseline.json" && node.file != "baseline.json")) {
                failure = CodexCompatibilityReason::SchemaMalformed; return {};
            }
            node.file = path;
        }
        auto value = files.value(node.file);
        for (auto part : pointer.split('/', Qt::SkipEmptyParts)) {
            part.replace("~1", "/"); part.replace("~0", "~");
            if (!value.isObject() || !value.toObject().contains(part)) {
                failure = CodexCompatibilityReason::SchemaMalformed; return {};
            }
            value = value.toObject().value(part);
        }
        node.shape = value;
        return resolve(node, depth + 1);
    }
    static bool annotation(const QString& key) {
        return QSet<QString>{"title", "description", "$schema", "$id", "default", "examples", "deprecated", "format"}.contains(key);
    }
    static bool supported(const QJsonObject& node) {
        static const QSet<QString> keywords{"$ref", "$defs", "definitions", "type", "enum", "const", "properties",
            "required", "additionalProperties", "items", "oneOf", "anyOf", "allOf", "minimum", "maximum", "minLength"};
        for (auto it = node.begin(); it != node.end(); ++it)
            if (!keywords.contains(it.key()) && !annotation(it.key())) return false;
        return true;
    }
    bool same(Node expected, Node actual, bool outbound, int depth = 0) {
        if (!withinLimits()) return false;
        if (depth > 64) { failure = CodexCompatibilityReason::SchemaLimit; return false; }
        const QString pair = expected.file + QChar('|') + expected.shape.toObject().value("$ref").toString()
            + QChar('|') + actual.file + QChar('|') + actual.shape.toObject().value("$ref").toString()
            + (outbound ? "|out" : "|in");
        const bool referenced = expected.shape.toObject().contains("$ref") && actual.shape.toObject().contains("$ref");
        if (referenced && matchedReferences.contains(pair)) return true;
        if (referenced && visiting.contains(pair)) { ++recursiveAssumptions; return true; }
        const auto left = resolve(expected), right = resolve(actual);
        if (left.isUndefined() || left.isNull() || right.isUndefined() || right.isNull()) return false;
        if (!left.isObject() || !right.isObject()) return left == right;
        auto a = left.toObject(), b = right.toObject();
        if (!supported(a) || !supported(b)) { failure = CodexCompatibilityReason::SchemaUnsupported; return false; }
        if (referenced) visiting.insert(pair);
        const auto assumptionsBefore = recursiveAssumptions;
        const auto finish = [this, &pair, referenced, assumptionsBefore](bool result) {
            if (referenced) {
                visiting.remove(pair);
                // A recursive assumption is valid only within its enclosing
                // comparison, which might still fail on a later property.
                if (result && assumptionsBefore == recursiveAssumptions) matchedReferences.insert(pair);
            }
            return result;
        };
        for (auto* node : {&a, &b}) if (node->contains("const")) { node->insert("enum", QJsonArray{node->take("const")}); }
        // Keep unions conservative: output enums include statuses, identities and
        // tool discriminators. Never guess semantics for a new actionable variant.
        for (const auto& key : {"type", "enum", "minimum", "maximum", "minLength"})
            if (a.value(key) != b.value(key)) return finish(false);
        for (const auto& key : {"oneOf", "anyOf", "allOf"}) {
            if (a.contains(key) != b.contains(key)) return finish(false);
            const auto alts = a.value(key).toArray(), candidates = b.value(key).toArray();
            if (alts.size() != candidates.size()) return finish(false);
            QSet<int> matched;
            for (const auto& variant : alts) {
                bool found = false;
                for (qsizetype i = 0; i < candidates.size(); ++i) if (!matched.contains(static_cast<int>(i))
                    && same({variant, expected.file}, {candidates.at(i), actual.file}, outbound, depth + 1)) {
                    matched.insert(static_cast<int>(i)); found = true; break;
                }
                if (!found) return finish(false);
            }
        }
        const auto ar = a.value("required").toArray(), br = b.value("required").toArray();
        // Requests may not acquire new mandatory fields; responses must retain
        // mandatory consumed fields. Extra informational fields are harmless.
        for (const auto& field : outbound ? br : ar) if (!(outbound ? ar : br).contains(field)) return finish(false);
        const auto ap = a.value("properties").toObject(), bp = b.value("properties").toObject();
        for (auto it = ap.begin(); it != ap.end(); ++it) {
            if (!bp.contains(it.key()) || !same({it.value(), expected.file}, {bp.value(it.key()), actual.file}, outbound, depth + 1)) return finish(false);
        }
        if (a.contains("items") != b.contains("items")) return finish(false);
        if (a.contains("items") && !same({a.value("items"), expected.file}, {b.value("items"), actual.file}, outbound, depth + 1)) return finish(false);
        if (a.value("additionalProperties").isObject() || b.value("additionalProperties").isObject()) {
            if (!same({a.value("additionalProperties"), expected.file}, {b.value("additionalProperties"), actual.file}, outbound, depth + 1)) return finish(false);
        } else if (outbound && b.value("additionalProperties") == false && a.value("additionalProperties") != false) return finish(false);
        return finish(true);
    }
    Node find(const QString& filename) {
        Node result;
        for (auto it = files.begin(); it != files.end(); ++it) {
            if (it.key().section('/', -1) != filename) continue;
            if (!result.file.isEmpty()) { failure = CodexCompatibilityReason::SchemaMalformed; return {}; }
            result = {it.value(), it.key()};
        }
        if (!result.file.isEmpty()) return result;
        // Recent generators can also publish one aggregate document with
        // definitions/$defs instead of one file per named response.
        const auto name = filename.chopped(5);
        for (auto it = files.begin(); it != files.end(); ++it) {
            if (it.key() == "baseline.json") continue;
            for (const auto& container : {"definitions", "$defs"}) {
                const auto definitions = it.value().toObject().value(container).toObject();
                if (!definitions.contains(name)) continue;
                if (!result.file.isEmpty()) { failure = CodexCompatibilityReason::SchemaMalformed; return {}; }
                result = {definitions.value(name), it.key()};
            }
        }
        return result;
    }
    Node contract(const QString& label) {
        // Also support our deterministic projected layout for offline regression
        // fixtures; generated schemas never enter CodexWireContract.
        if (files.contains("projection.json")) return {files.value("projection.json").toObject().value("contracts").toObject().value(label), "projection.json"};
        const auto kind = label.section(':', 0, 0), name = label.section(':', 1);
        if (kind == "Response") return find(name + ".json");
        auto root = find(kind + ".json");
        const auto shape = resolve(root).toObject();
        auto variants = shape.value("oneOf").toArray();
        if (variants.isEmpty()) variants = shape.value("anyOf").toArray();
        Node result;
        for (const auto& value : variants) {
            Node candidate{value, root.file};
            const auto properties = resolve(candidate).toObject().value("properties").toObject();
            Node method{properties.value("method"), candidate.file};
            const auto selector = resolve(method).toObject();
            if (selector.value("const") != name && selector.value("enum").toArray() != QJsonArray{name}) continue;
            if (!result.file.isEmpty()) { failure = CodexCompatibilityReason::SchemaMalformed; return {}; }
            result = candidate;
        }
        return result;
    }
};
}
QString CodexSchemaSurface::identity(const QJsonObject& files)
{
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(files).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}
CodexCompatibilityReport CodexSchemaSurface::inspect(const QJsonObject& files)
{
    CodexCompatibilityReport report;
    report.state = CodexCompatibilityState::Incompatible;
    report.reason = CodexCompatibilityReason::ProtocolRequirementMissing;
    report.explanation = "The executable does not expose the protocol surface required by DevHub.";
    if (files.contains("baseline.json")) { report.reason = CodexCompatibilityReason::SchemaMalformed; return report; }
    for (const auto& profile : CodexProtocolProfile::profiles()) {
        const CodexWireContract wire(profile.snapshotVersion);
        const auto snapshot = consumedSnapshot(wire.snapshot());
        Comparison comparison; comparison.files = files;
        comparison.files.insert("baseline.json", snapshot);
        bool matches = true;
        const auto contracts = snapshot.value("contracts").toObject();
        for (auto it = contracts.begin(); it != contracts.end(); ++it) {
            const auto actual = comparison.contract(it.key());
            const bool outbound = it.key().startsWith("Client") || (it.key().startsWith("Response:")
                && (it.key().contains("ApprovalResponse") || it.key().contains("DynamicToolCallResponse")
                    || it.key().contains("ToolRequestUserInputResponse") || it.key().contains("ElicitationRequestResponse")));
            if (actual.file.isEmpty() || !comparison.same({it.value(), "baseline.json"}, actual, outbound)) { matches = false; break; }
        }
        if (matches) {
            report.protocolCompatible = true; report.adapter = profile.id;
            report.schemaIdentity = identity(files);
            report.dynamicTools = profile.dynamicTools; report.resume = true;
            report.state = CodexCompatibilityState::VerificationRequired;
            report.reason = CodexCompatibilityReason::RuntimeVerificationRequired;
            report.explanation = "Protocol compatible. Runtime restrictions still require verification before sending.";
            report.evidence = {"executable-schema-surface-v1"};
            return report;
        }
        if (comparison.failure != CodexCompatibilityReason::ProtocolRequirementMissing) report.reason = comparison.failure;
        if (comparison.failure == CodexCompatibilityReason::Cancelled || comparison.failure == CodexCompatibilityReason::SchemaDeadline) break;
    }
    if (report.reason == CodexCompatibilityReason::SchemaUnsupported)
        report.explanation = "A required schema uses a construct DevHub cannot safely compare.";
    if (report.reason == CodexCompatibilityReason::SchemaMalformed)
        report.explanation = "The required schema surface has missing, ambiguous or invalid local references.";
    if (report.reason == CodexCompatibilityReason::SchemaLimit)
        report.explanation = "The required schema surface exceeded DevHub's comparison limits.";
    if (report.reason == CodexCompatibilityReason::SchemaDeadline || report.reason == CodexCompatibilityReason::Cancelled) {
        report.state = CodexCompatibilityState::VerificationRequired;
        report.explanation = report.reason == CodexCompatibilityReason::Cancelled ? "Schema comparison was cancelled."
            : "Schema comparison exceeded its deadline.";
    }
    return report;
}
}
