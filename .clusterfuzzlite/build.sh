#!/bin/bash -eu
# Compile the presets parser fuzz target. presets.cpp's Windows-only code is
# behind #ifdef _WIN32, so on this Linux image only the pure parser is built.
# -iquote (not -I): our headers are all quote-included, and putting src/ on the
# angle-bracket path let libc++'s <string> pull in src/strings.h instead of the
# system <strings.h>, dragging in windows.h and failing the Linux build.
$CXX $CXXFLAGS -std=c++20 -iquote src \
  tests/fuzz/fuzz_presets.cpp src/presets.cpp \
  $LIB_FUZZING_ENGINE -o "$OUT/fuzz_presets"
