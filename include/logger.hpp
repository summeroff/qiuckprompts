#pragma once

#include <windows.h>

#include <string>
#include <mutex>
#include <cstdarg>

namespace qp
{

enum class LogLevel
{
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Off = 5,
};

// Thread-safe logger: file + OutputDebugStringW (+ optional stdout if console).
class Logger
{
public:
    static Logger& Instance();

    void Init(const std::wstring& logFilePath, LogLevel level, bool mirrorStdout = false);
    void Shutdown();

    void SetLevel(LogLevel level);
    LogLevel Level() const;

    void Log(LogLevel level, const char* file, int line, const char* func,
             const std::wstring& message);

    static const wchar_t* LevelName(LogLevel level);
    static LogLevel ParseLevel(const std::wstring& s);

private:
    Logger() = default;
    mutable std::mutex mutex_;
    LogLevel level_ = LogLevel::Info;
    bool mirrorStdout_ = false;
    HANDLE file_ = INVALID_HANDLE_VALUE;
    bool initialized_ = false;
};

} // namespace qp

namespace qp
{
namespace detail
{
std::wstring FormatLog(const wchar_t* fmt, ...);
} // namespace detail
} // namespace qp

#define QP_LOG_AT(level, fmt, ...)                                                                 \
    do                                                                                             \
    {                                                                                              \
        if (static_cast<int>(level) >= static_cast<int>(qp::Logger::Instance().Level()))           \
        {                                                                                          \
            qp::Logger::Instance().Log((level), __FILE__, __LINE__, __FUNCTION__,                  \
                                       qp::detail::FormatLog(fmt, ##__VA_ARGS__));                 \
        }                                                                                          \
    } while (0)

#define QP_LOG_TRACE(fmt, ...) QP_LOG_AT(qp::LogLevel::Trace, fmt, ##__VA_ARGS__)
#define QP_LOG_DEBUG(fmt, ...) QP_LOG_AT(qp::LogLevel::Debug, fmt, ##__VA_ARGS__)
#define QP_LOG_INFO(fmt, ...) QP_LOG_AT(qp::LogLevel::Info, fmt, ##__VA_ARGS__)
#define QP_LOG_WARN(fmt, ...) QP_LOG_AT(qp::LogLevel::Warn, fmt, ##__VA_ARGS__)
#define QP_LOG_ERROR(fmt, ...) QP_LOG_AT(qp::LogLevel::Error, fmt, ##__VA_ARGS__)
