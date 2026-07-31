cmake_minimum_required(VERSION 3.20)

# Single version knob. Local/PR builds stay on the default so a Debug exe is never
# mistaken for a GitHub Release. CI tag builds pass e.g. -DQP_RELEASE_VERSION=0.3.2
# (tag v0.3.2 without the leading v).
set(QP_RELEASE_VERSION "0.0.0-dev" CACHE STRING
    "App version string (CI sets from git tag; local default is 0.0.0-dev)")

# Numeric PE/FILEVERSION fields: take leading X.Y.Z; ignore -dev / +meta suffix.
set(_qp_ver "${QP_RELEASE_VERSION}")
string(REGEX REPLACE "^[vV]" "" _qp_ver "${_qp_ver}")
if(_qp_ver MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(QP_VERSION_MAJOR "${CMAKE_MATCH_1}")
    set(QP_VERSION_MINOR "${CMAKE_MATCH_2}")
    set(QP_VERSION_PATCH "${CMAKE_MATCH_3}")
else()
    set(QP_VERSION_MAJOR 0)
    set(QP_VERSION_MINOR 0)
    set(QP_VERSION_PATCH 0)
endif()

# Short git identity for logs (especially 0.0.0-dev builds).
set(QP_GIT_HASH "unknown")
set(QP_GIT_DIRTY 0)
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE _qp_git_hash
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _qp_git_hash_rc
    )
    if(_qp_git_hash_rc EQUAL 0 AND NOT _qp_git_hash STREQUAL "")
        set(QP_GIT_HASH "${_qp_git_hash}")
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" status --porcelain
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE _qp_git_status
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _qp_git_status_rc
    )
    if(_qp_git_status_rc EQUAL 0 AND NOT _qp_git_status STREQUAL "")
        set(QP_GIT_DIRTY 1)
    endif()
endif()

# Display / log string: always append +gHASH (from git at configure time).
# Pass bare versions only: 0.0.0-dev or X.Y.Z — do not include +g… in QP_RELEASE_VERSION.
# Examples: 0.0.0-dev+g1a2b3c4d5e6f  |  0.3.2+g1a2b3c4d5e6f  |  0.0.0-dev+g1a2b3c4d5e6f.dirty
set(_qp_base "${QP_RELEASE_VERSION}")
# Strip a mistaken pre-baked +g… / .dirty so we never double-append.
string(REGEX REPLACE "\\+g[0-9a-fA-F]+(\\.dirty)?$" "" _qp_base "${_qp_base}")
string(REGEX REPLACE "\\.dirty$" "" _qp_base "${_qp_base}")
set(QP_VERSION_STRING "${_qp_base}+g${QP_GIT_HASH}")
if(QP_GIT_DIRTY)
    set(QP_VERSION_STRING "${QP_VERSION_STRING}.dirty")
endif()

project(qiuckprompts
    VERSION ${QP_VERSION_MAJOR}.${QP_VERSION_MINOR}.${QP_VERSION_PATCH}
    LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(QP_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${QP_GENERATED_DIR}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version_build.h.in"
    "${QP_GENERATED_DIR}/version_build.h"
    @ONLY
)
message(STATUS "qiuckprompts version: ${QP_VERSION_STRING} "
    "(PE ${QP_VERSION_MAJOR}.${QP_VERSION_MINOR}.${QP_VERSION_PATCH}, "
    "git ${QP_GIT_HASH}, dirty=${QP_GIT_DIRTY})")

add_compile_definitions(
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)

# Dev-only CLI (--crash-test, …): ON for Debug by default so local/CI test builds
# keep tools; OFF for RelWithDebInfo/Release so production tag zips strip them.
# Override: -DQP_DEV_TOOLS_IN_RELWITHDEBINFO=ON to keep tools in a local Rel build
# (applies to both RelWithDebInfo and Release configs).
option(QP_DEV_TOOLS_IN_RELWITHDEBINFO
       "Compile --crash-test and other dev tools into RelWithDebInfo and Release" OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8)
    add_compile_options("$<$<CONFIG:Debug>:/Od;/Zi>")
    add_compile_options("$<$<CONFIG:RelWithDebInfo>:/O2;/Zi>")
    # Better stacks in optimized builds (still keep /O2 via RelWithDebInfo flags above).
    add_compile_options("$<$<CONFIG:RelWithDebInfo>:/Oy->")
    add_link_options("$<$<CONFIG:Debug>:/DEBUG:FULL>")
    add_link_options("$<$<CONFIG:RelWithDebInfo>:/DEBUG:FULL>")
endif()

set(QP_SOURCES
    src/main.cpp
    src/app.cpp
    src/logger.cpp
    src/config.cpp
    src/hotkeys.cpp
    src/injector.cpp
    src/input_sim.cpp
    src/clipboard_image.cpp
    src/browser.cpp
    src/page_ready.cpp
    src/title_sample.cpp
    src/workflow.cpp
    src/tray.cpp
    src/util.cpp
    src/crash_log.cpp
    src/crash_test.cpp
    src/ext_bridge.cpp
    src/updater.cpp
    src/autostart.cpp
    resources/app.rc
)

set(QP_HEADERS
    include/app.hpp
    include/logger.hpp
    include/config.hpp
    include/hotkeys.hpp
    include/injector.hpp
    include/input_sim.hpp
    include/clipboard_image.hpp
    include/browser.hpp
    include/page_ready.hpp
    include/title_sample.hpp
    include/workflow.hpp
    include/tray.hpp
    include/util.hpp
    include/version.hpp
    include/crash_log.hpp
    include/crash_test.hpp
    include/ext_bridge.hpp
    include/updater.hpp
    include/autostart.hpp
    resources/resource.h
)

add_executable(qiuckprompts WIN32 ${QP_SOURCES} ${QP_HEADERS})

target_include_directories(qiuckprompts PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/resources
    ${QP_GENERATED_DIR}
)

# Per-config: Debug always gets dev tools; RelWithDebInfo only if option ON.
target_compile_definitions(qiuckprompts PRIVATE
    $<$<CONFIG:Debug>:QP_ENABLE_DEV_TOOLS=1>
    $<$<AND:$<CONFIG:RelWithDebInfo>,$<BOOL:${QP_DEV_TOOLS_IN_RELWITHDEBINFO}>>:QP_ENABLE_DEV_TOOLS=1>
    $<$<AND:$<CONFIG:Release>,$<BOOL:${QP_DEV_TOOLS_IN_RELWITHDEBINFO}>>:QP_ENABLE_DEV_TOOLS=1>
)
message(STATUS "dev tools: Debug=ON; RelWithDebInfo/Release="
    "${QP_DEV_TOOLS_IN_RELWITHDEBINFO} (QP_DEV_TOOLS_IN_RELWITHDEBINFO)")

target_link_libraries(qiuckprompts PRIVATE
    user32
    shell32
    gdi32
    advapi32
    ole32
    oleaut32
    psapi
    uiautomationcore
    dbghelp
    winhttp
)

# Portable layout next to the exe (install template + unpacked extension for dev)
add_custom_command(TARGET qiuckprompts POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        "$<TARGET_FILE_DIR:qiuckprompts>/config"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/config"
        "$<TARGET_FILE_DIR:qiuckprompts>/config"
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        "$<TARGET_FILE_DIR:qiuckprompts>/extension"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/extension"
        "$<TARGET_FILE_DIR:qiuckprompts>/extension"
    COMMENT "Copy config/ + extension/ beside executable"
)

enable_testing()
add_test(NAME self_test COMMAND qiuckprompts --self-test)

install(TARGETS qiuckprompts RUNTIME DESTINATION bin)
install(DIRECTORY config/ DESTINATION bin/config)
install(DIRECTORY extension/ DESTINATION bin/extension)
