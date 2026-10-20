#include "CodexProcessTree.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace devhub::infrastructure::codex {
CodexProcessTree::~CodexProcessTree() { close(); }
bool CodexProcessTree::prepare(QProcess& process)
{
    close();
#ifdef Q_OS_WIN
    m_job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!m_job || !SetInformationJobObject(static_cast<HANDLE>(m_job), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) { close(); return false; }
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    m_attributes = HeapAlloc(GetProcessHeap(), 0, bytes);
    if (!m_attributes || !InitializeProcThreadAttributeList(static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(m_attributes), 1, 0, &bytes)) {
        if (m_attributes) HeapFree(GetProcessHeap(), 0, m_attributes);
        m_attributes = nullptr; close(); return false;
    }
    if (!UpdateProcThreadAttribute(static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(m_attributes), 0,
            PROC_THREAD_ATTRIBUTE_JOB_LIST, &m_job, sizeof(HANDLE), nullptr, nullptr)) { close(); return false; }
    m_startup = new STARTUPINFOEXW{};
    process.setCreateProcessArgumentsModifier([this](QProcess::CreateProcessArguments* args) {
        auto* startup = static_cast<STARTUPINFOEXW*>(m_startup);
        startup->StartupInfo = *args->startupInfo;
        startup->StartupInfo.cb = sizeof(STARTUPINFOEXW);
        startup->lpAttributeList = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(m_attributes);
        args->startupInfo = &startup->StartupInfo;
        args->flags |= CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT;
    });
    return true;
#else
    Q_UNUSED(process);
    return false;
#endif
}
void CodexProcessTree::close()
{
#ifdef Q_OS_WIN
    if (m_job) { CloseHandle(static_cast<HANDLE>(m_job)); m_job = nullptr; }
    if (m_attributes) {
        DeleteProcThreadAttributeList(static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(m_attributes));
        HeapFree(GetProcessHeap(), 0, m_attributes); m_attributes = nullptr;
    }
    delete static_cast<STARTUPINFOEXW*>(m_startup); m_startup = nullptr;
#endif
}
}
