#include "logger.hpp"
#include "util.hpp"

#include <windows.h>

#include <cstdio>
#include <cwchar>
#include <string>

namespace qp
{

Logger& Logger::Instance()
{
    static Logger g;
    return g;
}

bool Logger::OpenFileUnlocked()
{
    if (path_.empty())
        return false;

    const size_t pos = path_.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        EnsureDirectory(path_.substr(0, pos));

    // Pre-rotate if existing file already oversized.
    if (FileExists(path_) && maxBytes_ > 0)
    {
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if (GetFileAttributesExW(path_.c_str(), GetFileExInfoStandard, &fad))
        {
            ULARGE_INTEGER uli{};
            uli.HighPart = fad.nFileSizeHigh;
            uli.LowPart = fad.nFileSizeLow;
            if (uli.QuadPart >= maxBytes_)
            {
                RotateLogFile(path_, maxFiles_);
            }
        }
    }

    file_ = CreateFileW(path_.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    writtenThisSession_ = 0;
    if (file_ != INVALID_HANDLE_VALUE)
    {
        LARGE_INTEGER sz{};
        if (GetFileSizeEx(file_, &sz) && sz.QuadPart > 0)
            writtenThisSession_ = static_cast<std::uint64_t>(sz.QuadPart);
    }
    return file_ != INVALID_HANDLE_VALUE;
}

void Logger::RotateIfNeededUnlocked()
{
    if (file_ == INVALID_HANDLE_VALUE || path_.empty() || maxBytes_ == 0)
        return;
    if (writtenThisSession_ < maxBytes_)
        return;

    CloseHandle(file_);
    file_ = INVALID_HANDLE_VALUE;
    RotateLogFile(path_, maxFiles_);
    OpenFileUnlocked();
}

void Logger::Init(const std::wstring& logFilePath, LogLevel level, bool mirrorStdout,
                  std::uint64_t maxBytes, int maxFiles)
{
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
    mirrorStdout_ = mirrorStdout;
    maxBytes_ = maxBytes;
    maxFiles_ = maxFiles < 2 ? 2 : maxFiles;
    path_ = logFilePath;

    if (file_ != INVALID_HANDLE_VALUE)
    {
        CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
    }

    if (!logFilePath.empty())
        OpenFileUnlocked();

    initialized_ = true;
}

void Logger::Shutdown()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_ != INVALID_HANDLE_VALUE)
    {
        CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
    }
    initialized_ = false;
}

void Logger::SetLevel(LogLevel level)
{
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

LogLevel Logger::Level() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return level_;
}

const wchar_t* Logger::LevelName(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Trace:
        return L"TRACE";
    case LogLevel::Debug:
        return L"DEBUG";
    case LogLevel::Info:
        return L"INFO ";
    case LogLevel::Warn:
        return L"WARN ";
    case LogLevel::Error:
        return L"ERROR";
    default:
        return L"OFF  ";
    }
}

LogLevel Logger::ParseLevel(const std::wstring& s)
{
    const std::wstring t = ToLower(Trim(s));
    if (t == L"trace")
        return LogLevel::Trace;
    if (t == L"debug")
        return LogLevel::Debug;
    if (t == L"info")
        return LogLevel::Info;
    if (t == L"warn" || t == L"warning")
        return LogLevel::Warn;
    if (t == L"error")
        return LogLevel::Error;
    if (t == L"off")
        return LogLevel::Off;
    return LogLevel::Info;
}

void Logger::Log(LogLevel level, const char* file, int line, const char* func,
                 const std::wstring& message)
{
    SYSTEMTIME st{};
    GetLocalTime(&st);

    const char* base = file;
    for (const char* p = file; *p; ++p)
    {
        if (*p == '\\' || *p == '/')
            base = p + 1;
    }

    wchar_t header[256];
    swprintf(header, 256, L"%04u-%02u-%02u %02u:%02u:%02u.%03u %s [%hs:%d %hs] ", st.wYear,
             st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
             LevelName(level), base, line, func ? func : "");

    std::wstring lineOut = header;
    lineOut += message;
    lineOut += L"\r\n";

    OutputDebugStringW(lineOut.c_str());

    std::lock_guard<std::mutex> lock(mutex_);

    if (mirrorStdout_)
    {
        fputws(lineOut.c_str(), stdout);
        fflush(stdout);
    }

    if (file_ != INVALID_HANDLE_VALUE)
    {
        const std::string utf8 = WideToUtf8(lineOut);
        DWORD written = 0;
        if (WriteFile(file_, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr))
        {
            writtenThisSession_ += written;
            RotateIfNeededUnlocked();
        }
    }
}

namespace detail
{

std::wstring FormatLog(const wchar_t* fmt, ...)
{
    if (!fmt)
        return {};

    va_list ap;
    va_start(ap, fmt);

    va_list ap2;
    va_copy(ap2, ap);
    const int needed = _vscwprintf(fmt, ap2);
    va_end(ap2);

    if (needed <= 0)
    {
        va_end(ap);
        return fmt;
    }

    std::wstring out(static_cast<size_t>(needed), L'\0');
    vswprintf(out.data(), out.size() + 1, fmt, ap);
    va_end(ap);
    return out;
}

} // namespace detail
} // namespace qp
