#pragma once
#include <QProcess>

namespace devhub::infrastructure::codex {
// Own all descendants of a bounded discovery/verification command on Windows.
// prepare() assigns the job at process creation, before any child code executes.
class CodexProcessTree final {
public:
    ~CodexProcessTree();
    CodexProcessTree() = default;
    CodexProcessTree(const CodexProcessTree&) = delete;
    CodexProcessTree& operator=(const CodexProcessTree&) = delete;
    bool prepare(QProcess& process);
    void close();
private:
    void* m_job = nullptr;
    void* m_startup = nullptr;
    void* m_attributes = nullptr;
};
}
