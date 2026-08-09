cmake_minimum_required(VERSION 3.21)

# Single source of truth for the version. The release workflow reads it and
# refuses to publish a tag that disagrees, so the number stamped into the
# executable always matches the release it shipped in.
project(ImeModePersistence VERSION 0.9.5 LANGUAGES CXX)

set(APP_VERSION "${PROJECT_VERSION}" CACHE STRING "Version stamped into the executable")

if(NOT APP_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    message(FATAL_ERROR "APP_VERSION must be MAJOR.MINOR.PATCH, got '${APP_VERSION}'")
endif()
set(APP_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(APP_VERSION_MINOR "${CMAKE_MATCH_2}")
set(APP_VERSION_PATCH "${CMAKE_MATCH_3}")

configure_file(src/resources.rc.in "${CMAKE_CURRENT_BINARY_DIR}/resources.rc" @ONLY)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Everything except the entry point and the dialog, so the tests can link the
# same objects the shipped executable is built from rather than a copy compiled
# with different flags. main.cpp is excluded because it is a WinMain and a
# message loop; config_dialog.cpp because it needs the resource script.
add_library(ime_core STATIC
    src/ime_state.cpp
    src/ime_state.h
    src/ime_interop.cpp
    src/ime_interop.h
    src/autostart.cpp
    src/autostart.h
    src/layout.cpp
    src/layout.h
    src/rules.cpp
    src/rules.h
    src/settings.cpp
    src/settings.h
    src/schedule.cpp
    src/schedule.h
    src/diagnostic.cpp
    src/diagnostic.h
    src/strings.cpp
    src/strings.h
    src/theme.cpp
    src/theme.h
    src/tsf.cpp
    src/tsf.h
)

target_include_directories(ime_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

# PUBLIC so the tests inherit them. UNICODE in particular has to match: linking
# objects built with a different setting would resolve the wrong API names.
target_compile_definitions(ime_core PUBLIC
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
)

target_link_libraries(ime_core PUBLIC
    user32
    imm32
    shell32
    advapi32
    comdlg32
    gdi32
    comctl32
    dwmapi
    ole32
    wtsapi32
)

if(MSVC)
    # /utf-8 is not optional here, and it is PUBLIC for the same reason: the
    # sources are UTF-8 without a BOM, and without this MSVC reads them in the
    # system ANSI code page. On an en-US machine the Chinese literals in
    # src/strings.cpp come out as CP1252 mojibake, and it compiles without a
    # single warning, because those bytes are nearly all valid CP1252. Nothing
    # but looking at the screen -- or the byte check in CI -- catches it.
    target_compile_options(ime_core PUBLIC /utf-8)
    target_compile_options(ime_core PRIVATE /W4 /permissive-)
endif()

add_executable(ImeModePersistence WIN32
    src/main.cpp
    src/config_dialog.cpp
    src/config_dialog.h
    src/resource.h
    "${CMAKE_CURRENT_BINARY_DIR}/resources.rc"
)

target_link_libraries(ImeModePersistence PRIVATE ime_core)

target_compile_definitions(ImeModePersistence PRIVATE
    APP_VERSION_STRING="${APP_VERSION}"
)

if(MSVC)
    target_compile_options(ImeModePersistence PRIVATE /W4 /permissive-)

    # The application manifest is embedded as an RT_MANIFEST resource (see
    # src/resources.rc.in). Letting the linker generate one as well would collide,
    # and the generated one would win -- taking visual styles and Per-Monitor V2
    # DPI awareness with it.
    target_link_options(ImeModePersistence PRIVATE "/MANIFEST:NO")
endif()

set_target_properties(ImeModePersistence PROPERTIES
    OUTPUT_NAME "ImeModePersistence"
)

option(BUILD_TESTING "Build the test suite" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
