#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <windows.h>

namespace qp
{

// Paths
std::wstring GetExeDir();
std::wstring GetExePath();
std::wstring GetAppDataDir(bool ensure = true);
std::wstring PathJoin(const std::wstring& a, const std::wstring& b);
std::wstring PathJoin(std::initializer_list<std::wstring> parts);

bool DirectoryExists(const std::wstring& path);
bool FileExists(const std::wstring& path);
bool EnsureDirectory(const std::wstring& path);
bool OpenInExplorer(const std::wstring& path);
bool OpenTextFile(const std::wstring& path);

// UTF-8 / wide
std::wstring Utf8ToWide(const std::string& utf8);
std::string WideToUtf8(const std::wstring& wide);

std::wstring Trim(const std::wstring& s);
std::wstring ToLower(const std::wstring& s);

// Hotkey
struct HotkeySpec
{
    UINT modifiers = 0;   // MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN [| MOD_NOREPEAT]
    UINT vk = 0;          // virtual-key code
    std::wstring display; // "Ctrl+Alt+1"
};

std::wstring FormatHotkeyDisplay(UINT modifiers, UINT vk);

// Errors
std::wstring Win32ErrorMessage(DWORD code);
std::wstring LastErrorMessage();

// RAII single-instance mutex
class SingleInstance
{
public:
    SingleInstance() = default;
    ~SingleInstance();
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    bool Acquire(const std::wstring& name);

private:
    HANDLE mutex_ = nullptr;
};

} // namespace qp
