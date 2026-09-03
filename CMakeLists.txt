cmake_minimum_required(VERSION 3.25)

# Use the user's vcpkg installation automatically when it is available. The
# manifest remains usable with an explicit -DCMAKE_TOOLCHAIN_FILE as well.
if(WIN32 AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VCPKG_ROOT})
        set(VCPKG_ROOT_PATH "$ENV{VCPKG_ROOT}")
    elseif(DEFINED ENV{VCPKG_INSTALLATION_ROOT})
        set(VCPKG_ROOT_PATH "$ENV{VCPKG_INSTALLATION_ROOT}")
    endif()
    if(DEFINED VCPKG_ROOT_PATH)
        set(CMAKE_TOOLCHAIN_FILE
            "${VCPKG_ROOT_PATH}/scripts/buildsystems/vcpkg.cmake"
            CACHE FILEPATH "vcpkg toolchain file")
    endif()
endif()

set(VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_SOURCE_DIR}/ports" CACHE STRING
    "vcpkg overlay ports directory")

set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING
    "vcpkg triplet used for the native Windows build")

project(CommandRunner
    VERSION 0.2.0
    DESCRIPTION "A native Windows command runner"
    LANGUAGES CXX
)

# Keep a Release PDB for post-release crash analysis. The PDB is retained by
# CI as a symbol artifact and is not shipped beside the single EXE.
if(MSVC)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT ProgramDatabase)
endif()

if(NOT WIN32)
    message(FATAL_ERROR "Command Runner is currently supported on Windows only.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

find_package(nlohmann_json CONFIG REQUIRED)
find_path(WIL_INCLUDE_DIR NAMES wil/resource.h REQUIRED)
find_path(WIN32XX_INCLUDE_DIR NAMES win32xx/wxx_wincore.h REQUIRED)

set(COMMAND_RUNNER_SOURCES
    src/main.cpp
    src/application.cpp
    src/core/models.cpp
    src/core/config_store.cpp
    src/core/process_manager.cpp
    src/platform/tray_icon.cpp
    src/ui/command_dialog.cpp
    src/ui/close_dialog.cpp
    src/ui/main_window.cpp
    src/ui/win32xx_helpers.cpp
    resources/CommandRunner.rc
)

add_executable(CommandRunner WIN32 ${COMMAND_RUNNER_SOURCES})

# The checked-in RC file owns the application manifest (including Per-Monitor
# V2 DPI awareness). Prevent the linker wrapper from synthesizing a second
# manifest resource for the WIN32 target.
set_target_properties(CommandRunner PROPERTIES LINK_FLAGS "/MANIFEST:NO")

target_include_directories(CommandRunner
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/resources
        ${WIL_INCLUDE_DIR}
        ${WIN32XX_INCLUDE_DIR}
)

target_link_libraries(CommandRunner
    PRIVATE
        nlohmann_json::nlohmann_json
        comctl32
        gdi32
        ole32
        shell32
        user32
)

target_compile_definitions(CommandRunner PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    WINVER=0x0A00 _WIN32_WINNT=0x0A00
)
target_compile_options(CommandRunner PRIVATE /W4 /permissive- /utf-8)

target_compile_options(CommandRunner PRIVATE
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/O1>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/GL>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/Gw>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/Gy>
)

target_link_options(CommandRunner PRIVATE
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/DEBUG>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/LTCG>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/OPT:REF>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/OPT:ICF>
)

set_target_properties(CommandRunner PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_NAME "CommandRunner"
)

include(CTest)
if(BUILD_TESTING)
    add_executable(CommandRunnerConfigTests
        tests/config_store_tests.cpp
        src/core/models.cpp
        src/core/config_store.cpp
    )

    add_executable(CommandRunnerProcessTests
        tests/process_manager_tests.cpp
        src/core/models.cpp
        src/core/process_manager.cpp
    )

    target_include_directories(CommandRunnerConfigTests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${WIL_INCLUDE_DIR}
            ${WIN32XX_INCLUDE_DIR}
    )
    target_link_libraries(CommandRunnerConfigTests PRIVATE nlohmann_json::nlohmann_json ole32 shell32)
    target_compile_definitions(CommandRunnerConfigTests PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
        WINVER=0x0A00 _WIN32_WINNT=0x0A00
    )
    target_compile_options(CommandRunnerConfigTests PRIVATE /W4 /permissive- /utf-8)

    add_test(NAME CommandRunner.ConfigStore COMMAND CommandRunnerConfigTests)

    target_include_directories(CommandRunnerProcessTests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${WIL_INCLUDE_DIR}
            ${WIN32XX_INCLUDE_DIR}
    )
    target_link_libraries(CommandRunnerProcessTests PRIVATE ole32 shell32 user32)
    target_compile_definitions(CommandRunnerProcessTests PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
        WINVER=0x0A00 _WIN32_WINNT=0x0A00
    )
    target_compile_options(CommandRunnerProcessTests PRIVATE /W4 /permissive- /utf-8)

    add_test(NAME CommandRunner.ProcessManager COMMAND CommandRunnerProcessTests)
endif()
