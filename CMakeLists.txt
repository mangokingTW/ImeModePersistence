cmake_minimum_required(VERSION 3.21)

# Single source of truth for the version. The release workflow reads it and
# refuses to publish a tag that disagrees, so the number stamped into the
# executable always matches the release it shipped in.
project(ImeModePersistence VERSION 0.4.6 LANGUAGES CXX)

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

add_executable(ImeModePersistence WIN32
    src/main.cpp
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
    src/config_dialog.cpp
    src/config_dialog.h
    src/strings.cpp
    src/strings.h
    src/theme.cpp
    src/theme.h
    src/resource.h
    "${CMAKE_CURRENT_BINARY_DIR}/resources.rc"
)

target_compile_definitions(ImeModePersistence PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
)

target_link_libraries(ImeModePersistence PRIVATE
    user32
    imm32
    shell32
    advapi32
    comdlg32
    gdi32
    comctl32
    dwmapi
)

if(MSVC)
    # /utf-8 is not optional here. The sources are UTF-8 without a BOM, and
    # without this MSVC reads them in the system ANSI code page: on an en-US
    # machine the Chinese literals in src/strings.cpp come out as CP1252
    # mojibake. It compiles without a single warning, because those bytes are
    # nearly all valid CP1252, so nothing but looking at the screen catches it.
    target_compile_options(ImeModePersistence PRIVATE /utf-8 /W4 /permissive-)

    # The application manifest is embedded as an RT_MANIFEST resource (see
    # src/resources.rc.in). Letting the linker generate one as well would collide,
    # and the generated one would win -- taking visual styles and Per-Monitor V2
    # DPI awareness with it.
    target_link_options(ImeModePersistence PRIVATE "/MANIFEST:NO")
endif()

set_target_properties(ImeModePersistence PROPERTIES
    OUTPUT_NAME "ImeModePersistence"
)
