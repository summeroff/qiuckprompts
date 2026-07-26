#pragma once

// Build identity: prefer CMake-generated version_build.h (local = 0.0.0-dev,
// tagged CI release = tag without leading 'v'). Fallbacks keep editors happy
// before the first configure.
#if defined(__has_include)
#  if __has_include("version_build.h")
#    include "version_build.h"
#  endif
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
#define QP_STRINGIFY2(x) #x
#define QP_STRINGIFY(x) QP_STRINGIFY2(x)
#define QP_VERSION_STRING "0.0.0-dev"
#endif

#define QP_APP_NAME "qiuckprompts"
#define QP_APP_NAME_W L"qiuckprompts"
#define QP_APP_DISPLAY_W L"QiuckPrompts"
#define QP_MUTEX_NAME_W L"Local\\QiuckPrompts_SingleInstance_v1"
#define QP_WND_CLASS_W L"QiuckPromptsMessageWindow"
