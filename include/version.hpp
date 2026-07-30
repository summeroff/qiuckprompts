#pragma once

// Build identity: prefer CMake-generated version_build.h
// (local = 0.0.0-dev+gSHORTSHA[.dirty], tagged CI = X.Y.Z+gSHORTSHA).
// Fallbacks keep editors happy before the first configure.
#if defined(__has_include)
#if __has_include("version_build.h")
#include "version_build.h"
#endif
#endif

#ifndef QP_VERSION_MAJOR
#define QP_VERSION_MAJOR 0
#endif
#ifndef QP_VERSION_MINOR
#define QP_VERSION_MINOR 0
#endif
#ifndef QP_VERSION_PATCH
#define QP_VERSION_PATCH 0
#endif

#ifndef QP_VERSION_STRING
#define QP_VERSION_STRING "0.0.0-dev+gunknown"
#endif

#ifndef QP_GIT_HASH
#define QP_GIT_HASH "unknown"
#endif

#ifndef QP_GIT_DIRTY
#define QP_GIT_DIRTY 0
#endif

#define QP_APP_NAME "qiuckprompts"
#define QP_APP_NAME_W L"qiuckprompts"
#define QP_APP_DISPLAY_W L"QiuckPrompts"
// Per-user data under %LOCALAPPDATA%\QiuckPrompts (config, logs, nm host).
#define QP_USER_DATA_NAME_W L"QiuckPrompts"
#define QP_MUTEX_NAME_W L"Local\\QiuckPrompts_SingleInstance_v1"
#define QP_SHUTDOWN_EVENT_NAME_W L"Local\\QiuckPrompts_Shutdown_v1"
#define QP_WND_CLASS_W L"QiuckPromptsMessageWindow"
#define QP_EXT_BRIDGE_PIPE_W L"\\\\.\\pipe\\QiuckPrompts_ExtBridge_v1"
#define QP_NM_HOST_NAME "com.qiuckprompts.host"
#define QP_NM_HOST_NAME_W L"com.qiuckprompts.host"
// Stable unpacked-extension id (manifest "key"); keep in sync with extension/manifest.json.
#define QP_EXTENSION_ID "aodehlngahndannepofbddnacfaldmih"
#define QP_EXTENSION_ID_W L"aodehlngahndannepofbddnacfaldmih"
