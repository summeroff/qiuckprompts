#pragma once

// Dev-only CLI / diagnostics (e.g. --crash-test).
// CMake sets QP_ENABLE_DEV_TOOLS=1 for Debug (and optionally RelWithDebInfo/Release
// when QP_DEV_TOOLS_IN_RELWITHDEBINFO=ON). QP_DEV_TOOLS is always 0 or 1 so
// `#if QP_DEV_TOOLS` strips the real implementations when tools are off.

#if defined(QP_ENABLE_DEV_TOOLS) && QP_ENABLE_DEV_TOOLS
#define QP_DEV_TOOLS 1
#else
#define QP_DEV_TOOLS 0
#endif

namespace qp
{

#if QP_DEV_TOOLS

// Run the normal app; after delayMs a worker thread crashes through a deep call
// chain so the log stack is multi-frame (not a one-liner in wWinMain).
// delayMs default ~2s so tray/hotkeys are up first.
void StartDeferredCrashTest(unsigned delayMs = 2000);

#else

inline void StartDeferredCrashTest(unsigned = 0)
{
}

#endif

} // namespace qp
