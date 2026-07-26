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
set(QP_VERSION_STRING "${QP_RELEASE_VERSION}")

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
    "(PE ${QP_VERSION_MAJOR}.${QP_VERSION_MINOR}.${QP_VERSION_PATCH})")

add_compile_definitions(
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8)
    add_compile_options("$<$<CONFIG:Debug>:/Od;/Zi>")
    add_compile_options("$<$<CONFIG:RelWithDebInfo>:/O2;/Zi>")
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
    resources/resource.h
)

add_executable(qiuckprompts WIN32 ${QP_SOURCES} ${QP_HEADERS})

target_include_directories(qiuckprompts PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/resources
    ${QP_GENERATED_DIR}
)

target_link_libraries(qiuckprompts PRIVATE
    user32
    shell32
    gdi32
    advapi32
    ole32
    oleaut32
    psapi
    uiautomationcore
)

# Portable layout next to the exe
add_custom_command(TARGET qiuckprompts POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        "$<TARGET_FILE_DIR:qiuckprompts>/config"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/config"
        "$<TARGET_FILE_DIR:qiuckprompts>/config"
    COMMENT "Copy config/qiuckprompts.ini beside executable"
)

enable_testing()
add_test(NAME self_test COMMAND qiuckprompts --self-test)

install(TARGETS qiuckprompts RUNTIME DESTINATION bin)
install(DIRECTORY config/ DESTINATION bin/config)
