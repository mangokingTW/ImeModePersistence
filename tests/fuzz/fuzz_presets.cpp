#include <cstddef>
#include <cstdint>
#include <string>

#include "presets.h"

// The marker parser is the one place ImeModePersistence reads a whole file of
// external bytes and turns it into structured rules, so it is the piece worth
// fuzzing. presets::parse takes raw UTF-8 bytes, so the fuzz input is handed to
// it unchanged -- what the fuzzer explores is exactly what a real (or corrupt,
// or hostile) presets.txt would drive.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    presets::parse(std::string(reinterpret_cast<const char*>(data), size));
    return 0;
}
