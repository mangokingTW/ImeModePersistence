# ClusterFuzzLite builds the fuzz targets in the OSS-Fuzz base image (Linux,
# Clang, libFuzzer). Only the platform-independent parser is fuzzed, so no
# Windows SDK is needed here.
FROM gcr.io/oss-fuzz-base/base-builder
COPY . $SRC/imemodepersistence
WORKDIR $SRC/imemodepersistence
COPY .clusterfuzzlite/build.sh $SRC/
