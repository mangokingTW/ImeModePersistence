# ClusterFuzzLite builds the fuzz targets in the OSS-Fuzz base image (Linux,
# Clang, libFuzzer). Only the platform-independent parser is fuzzed, so no
# Windows SDK is needed here.
# Pinned by digest so the build is reproducible and Scorecard's
# Pinned-Dependencies check is satisfied. Dependabot (docker ecosystem, see
# .github/dependabot.yml) watches the :latest tag and opens a PR to bump the
# digest when OSS-Fuzz publishes a new base image, so pinning does not mean going
# stale.
FROM gcr.io/oss-fuzz-base/base-builder:latest@sha256:23f294580c94a53c1625f5b98c7c22d7155fb030b877d0c2f937fbc8156b5aca
COPY . $SRC/imemodepersistence
WORKDIR $SRC/imemodepersistence
COPY .clusterfuzzlite/build.sh $SRC/
