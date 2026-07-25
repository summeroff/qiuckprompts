cmake_minimum_required(VERSION 3.20)
project(qiuckprompts VERSION 0.3.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_definitions(
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
    QP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    QP_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    QP_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    QP_VERSION_STRING="${PROJECT_VERSION}"
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

set_source_files_properties(resources/app.rc PROPERTIES
    COMPILE_DEFINITIONS "QP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR};QP_VERSION_MINOR=${PROJECT_VERSION_MINOR};QP_VERSION_PATCH=${PROJECT_VERSION_PATCH}"
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
