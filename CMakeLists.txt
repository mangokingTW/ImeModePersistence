cmake_minimum_required(VERSION 3.21)
project(ImeModePersistence LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(ImeModePersistence WIN32
    src/main.cpp
    src/ime_state.cpp
    src/ime_state.h
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
)

if(MSVC)
    target_compile_options(ImeModePersistence PRIVATE /W4 /permissive-)
endif()

set_target_properties(ImeModePersistence PROPERTIES
    OUTPUT_NAME "ImeModePersistence"
)
