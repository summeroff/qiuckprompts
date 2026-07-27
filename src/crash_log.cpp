#include "crash_log.hpp"
#include "version.hpp"

#include <dbghelp.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

#pragma comment(lib, "dbghelp.lib")

namespace qp
{
namespace
{

constexpr size_t kCrashPathCap = 1024;
wchar_t g_crashLogPath[kCrashPathCap] = {};
bool g_handlersInstalled = false;

void AppendUtf8ToCrashLog(const char* text)
{
    if (!text || !text[0])
        return;

    // Prefer configured path; fall back next to the exe.
    wchar_t path[kCrashPathCap];
    path[0] = 0;
    wcsncpy_s(path, g_crashLogPath[0] ? g_crashLogPath : L"", _TRUNCATE);
    if (!path[0])
    {
        wchar_t exe[MAX_PATH]{};
        if (GetModuleFileNameW(nullptr, exe, MAX_PATH) > 0)
        {
            std::wstring p = exe;
            const size_t slash = p.find_last_of(L"\\/");
            if (slash != std::wstring::npos)
                p.resize(slash);
            p += L"\\logs\\qiuckprompts.log";
            wcsncpy_s(path, p.c_str(), _TRUNCATE);
        }
    }
    if (!path[0])
        return;

    // Ensure parent dir exists (best effort, no util.hpp dependency in crash path).
    {
        std::wstring dir = path;
        const size_t slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos)
        {
            dir.resize(slash);
            CreateDirectoryW(dir.c_str(), nullptr);
        }
    }

    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;

    DWORD written = 0;
    const DWORD len = static_cast<DWORD>(strlen(text));
    WriteFile(file, text, len, &written, nullptr);
    FlushFileBuffers(file);
    CloseHandle(file);

    OutputDebugStringA(text);
}

void AppendLine(const char* fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    AppendUtf8ToCrashLog(buf);
    AppendUtf8ToCrashLog("\r\n");
}

const char* ExceptionName(DWORD code)
{
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION:
        return "ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        return "ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_BREAKPOINT:
        return "BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT:
        return "DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        return "FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INVALID_OPERATION:
        return "FLT_INVALID_OPERATION";
    case EXCEPTION_FLT_OVERFLOW:
        return "FLT_OVERFLOW";
    case EXCEPTION_FLT_STACK_CHECK:
        return "FLT_STACK_CHECK";
    case EXCEPTION_FLT_UNDERFLOW:
        return "FLT_UNDERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:
        return "IN_PAGE_ERROR";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW:
        return "INT_OVERFLOW";
    case EXCEPTION_INVALID_DISPOSITION:
        return "INVALID_DISPOSITION";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION:
        return "NONCONTINUABLE_EXCEPTION";
    case EXCEPTION_PRIV_INSTRUCTION:
        return "PRIV_INSTRUCTION";
    case EXCEPTION_SINGLE_STEP:
        return "SINGLE_STEP";
    case EXCEPTION_STACK_OVERFLOW:
        return "STACK_OVERFLOW";
    default:
        return "EXCEPTION";
    }
}

void WriteStackTrace(CONTEXT* ctxIn)
{
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();

    // Fresh init each crash — filter may run after a partial prior attempt.
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    if (!SymInitialize(process, nullptr, TRUE))
    {
        AppendLine("  (SymInitialize failed err=%lu)", GetLastError());
        // Still try CaptureStackBackTrace addresses.
        void* frames[64]{};
        const USHORT n = CaptureStackBackTrace(0, 64, frames, nullptr);
        for (USHORT i = 0; i < n; ++i)
            AppendLine("  #%02u %p", static_cast<unsigned>(i), frames[i]);
        return;
    }

    CONTEXT ctx = {};
    if (ctxIn)
        ctx = *ctxIn;
    else
        RtlCaptureContext(&ctx);

    STACKFRAME64 frame{};
    DWORD machine = 0;
#if defined(_M_X64) || defined(__x86_64__)
    machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_IX86)
    machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = ctx.Eip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Ebp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Esp;
    frame.AddrStack.Mode = AddrModeFlat;
#else
    AppendLine("  (stack walk unsupported on this arch)");
    SymCleanup(process);
    return;
#endif

    char symBuf[sizeof(SYMBOL_INFO) + 256]{};
    auto* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;

    for (unsigned i = 0; i < 64; ++i)
    {
        if (!StackWalk64(machine, process, thread, &frame, &ctx, nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr))
        {
            break;
        }
        if (frame.AddrPC.Offset == 0)
            break;

        const DWORD64 addr = frame.AddrPC.Offset;
        DWORD64 displacement = 0;
        const BOOL gotSym = SymFromAddr(process, addr, &displacement, sym);

        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisp = 0;
        const BOOL gotLine = SymGetLineFromAddr64(process, addr, &lineDisp, &line);

        char modName[MAX_PATH]{};
        const DWORD64 modBase = SymGetModuleBase64(process, addr);
        if (modBase)
            GetModuleFileNameA(reinterpret_cast<HMODULE>(modBase), modName, MAX_PATH);

        const char* base = modName;
        if (const char* slash = strrchr(modName, '\\'))
            base = slash + 1;

        if (gotSym && gotLine)
        {
            AppendLine("  #%02u %s!%s +0x%llx [%s:%lu] (%p)", i, base[0] ? base : "?", sym->Name,
                       static_cast<unsigned long long>(displacement), line.FileName,
                       line.LineNumber, reinterpret_cast<void*>(static_cast<uintptr_t>(addr)));
        } else if (gotSym)
        {
            AppendLine("  #%02u %s!%s +0x%llx (%p)", i, base[0] ? base : "?", sym->Name,
                       static_cast<unsigned long long>(displacement),
                       reinterpret_cast<void*>(static_cast<uintptr_t>(addr)));
        } else
        {
            AppendLine("  #%02u %s!%p", i, base[0] ? base : "?",
                       reinterpret_cast<void*>(static_cast<uintptr_t>(addr)));
        }
    }

    SymCleanup(process);
}

void WriteCrashBanner(const char* kind, DWORD code, void* address)
{
    SYSTEMTIME st{};
    GetLocalTime(&st);
    AppendLine("");
    AppendLine("========== CRASH %04u-%02u-%02u %02u:%02u:%02u.%03u ==========", st.wYear,
               st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    AppendLine("kind=%s version=%s git=%s dirty=%d", kind, QP_VERSION_STRING, QP_GIT_HASH,
               QP_GIT_DIRTY ? 1 : 0);
    if (code || address)
    {
        AppendLine("exception=%s (0x%08lX) address=%p", ExceptionName(code),
                   static_cast<unsigned long>(code), address);
    }
    AppendLine("pid=%lu tid=%lu", GetCurrentProcessId(), GetCurrentThreadId());
    AppendLine("stack:");
}

LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* info)
{
    // Avoid re-entrancy if logging itself faults.
    static volatile long once = 0;
    if (InterlockedCompareExchange(&once, 1, 0) != 0)
        return EXCEPTION_CONTINUE_SEARCH;

    DWORD code = 0;
    void* addr = nullptr;
    CONTEXT* ctx = nullptr;
    if (info && info->ExceptionRecord)
    {
        code = info->ExceptionRecord->ExceptionCode;
        addr = info->ExceptionRecord->ExceptionAddress;
    }
    if (info)
        ctx = info->ContextRecord;

    WriteCrashBanner("SEH", code, addr);
    WriteStackTrace(ctx);
    AppendLine("========== END CRASH ==========");
    AppendLine("");

    return EXCEPTION_CONTINUE_SEARCH; // still allow WER / debugger
}

void __cdecl OnPureCall()
{
    WriteCrashBanner("PURECALL", 0, nullptr);
    WriteStackTrace(nullptr);
    AppendLine("========== END CRASH ==========");
    AppendLine("");
    abort();
}

void __cdecl OnInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int,
                                uintptr_t)
{
    WriteCrashBanner("INVALID_PARAMETER", 0, nullptr);
    WriteStackTrace(nullptr);
    AppendLine("========== END CRASH ==========");
    AppendLine("");
    abort();
}

void OnTerminate()
{
    WriteCrashBanner("STD_TERMINATE", 0, nullptr);
    WriteStackTrace(nullptr);
    AppendLine("========== END CRASH ==========");
    AppendLine("");
    abort();
}

} // namespace

void SetCrashLogPath(const std::wstring& logFilePath)
{
    if (logFilePath.empty())
        g_crashLogPath[0] = 0;
    else
        wcsncpy_s(g_crashLogPath, logFilePath.c_str(), _TRUNCATE);
}

void InstallCrashHandlers()
{
    if (g_handlersInstalled)
        return;
    g_handlersInstalled = true;

    SetUnhandledExceptionFilter(UnhandledFilter);
    _set_purecall_handler(OnPureCall);
    _set_invalid_parameter_handler(OnInvalidParameter);
    std::set_terminate(OnTerminate);
}

} // namespace qp
