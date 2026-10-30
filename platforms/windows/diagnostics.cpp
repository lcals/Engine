// 在所有 Windows 宿主及测试入口之前配置诊断，失败通过 stderr 和退出码报告。
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <crtdbg.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>

namespace
{
void report_abort(int)
{
    constexpr char message[] = "Fatal error: abort() called\n";
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), message, sizeof(message) - 1, &written, nullptr);
}

LONG WINAPI report_unhandled_exception(EXCEPTION_POINTERS* exception)
{
    const auto* record = exception != nullptr ? exception->ExceptionRecord : nullptr;
    char message[160]{};
    const int size =
        std::snprintf(message, sizeof(message), "Unhandled Windows exception 0x%08lx at %p\n",
                      record != nullptr ? record->ExceptionCode : 0UL,
                      record != nullptr ? record->ExceptionAddress : nullptr);
    if (size > 0 && static_cast<std::size_t>(size) < sizeof(message))
    {
        DWORD written = 0;
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), message, static_cast<DWORD>(size), &written,
                  nullptr);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

[[nodiscard]] bool configure_diagnostics()
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter(report_unhandled_exception);
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    std::signal(SIGABRT, report_abort);
#if defined(_DEBUG)
    constexpr int report_types[] = {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT};
    for (const int type : report_types)
    {
        _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
    }
#endif
    return true;
}

[[maybe_unused]] const bool diagnostics_ready = configure_diagnostics();
} // namespace
