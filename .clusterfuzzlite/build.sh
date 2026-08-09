#!/bin/bash -eu
# Compile the presets parser fuzz target. presets.cpp's Windows-only code is
# behind #ifdef _WIN32, so on this Linux image only the pure parser is built.
$CXX $CXXFLAGS -std=c++20 -I src \
  tests/fuzz/fuzz_presets.cpp src/presets.cpp \
  $LIB_FUZZING_ENGINE -o "$OUT/fuzz_presets"
