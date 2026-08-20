cmake_minimum_required(VERSION 3.21)

# Single source of truth for the version. The release workflow reads it and
# refuses to publish a tag that disagrees, so the number stamped into the
# executable always matches the release it shipped in.
project(ImeModePersistence VERSION 1.4.0 LANGUAGES CXX)

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

# Link the C/C++ runtime statically (/MT) so the executable is self-contained.
# The default (/MD) makes it depend on VCRUNTIME140.dll and MSVCP140.dll, which
# ship only with the VC++ Redistributable and are absent on a clean Windows
# install -- so the exe fails to launch there with a missing-DLL error. That is
# what winget's validation sandbox hit (Validation-Executable-Error), and it
# would equally bite anyone running the portable zip on a fresh machine. Set
# before any target is created so every target -- library, exe and tests -- uses
# the same runtime and does not mix /MT with /MD. (CMP0091 is NEW at 3.21.)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

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
    src/persist.cpp
    src/persist.h
    src/rules.cpp
    src/rules.h
    src/settings.cpp
    src/settings.h
    src/schedule.cpp
    src/schedule.h
    src/presets.cpp
    src/presets.h
    src/tipbridge.cpp
    src/tipbridge.h
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
    uxtheme
    ole32
    taskschd
    windowsapp
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
    src/overlay.cpp
    src/overlay.h
    src/caret.cpp
    src/caret.h
    src/resource.h
    "${CMAKE_CURRENT_BINARY_DIR}/resources.rc"
)

# oleaut32 for the SafeArray helpers the caret's UI-Automation path uses; ole32
# and the rest propagate from ime_core.
target_link_libraries(ImeModePersistence PRIVATE ime_core oleaut32 gdiplus)

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

# The in-process Text Input Processor: a COM/TSF keyboard text service Windows
# loads into other applications so their conversion mode can be read and set from
# inside the process (what the IMM32 interop cannot do for TSF/packaged apps). A
# standalone SHARED library -- deliberately not linked against ime_core -- because
# it is mapped into every application that loads a keyboard service and must stay
# tiny. It talks to the exe over the named pipe in src/tip/tip_protocol.h.
add_library(ImeModePersistenceTip SHARED
    src/tip/tip.cpp
    src/tip/tip_protocol.h
    src/tip/tip.def
)

target_include_directories(ImeModePersistenceTip PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_definitions(ImeModePersistenceTip PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
)

# ole32/oleaut32 for COM + VARIANT; uuid.lib (a default MSVC library) carries the
# TSF GUIDs the same way ime_core relies on for src/tsf.cpp.
target_link_libraries(ImeModePersistenceTip PRIVATE ole32 oleaut32 user32 advapi32)

if(MSVC)
    # Default warning level only (not the /W4 the rest of the tree uses): a COM
    # in-process server is mostly SDK-shaped boilerplate, and the release build
    # compiles with warnings-as-error, so the stricter level would turn the
    # framework's own idioms into build failures for no safety gained here.
    target_compile_options(ImeModePersistenceTip PRIVATE /utf-8)
endif()

option(BUILD_TESTING "Build the test suite" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
