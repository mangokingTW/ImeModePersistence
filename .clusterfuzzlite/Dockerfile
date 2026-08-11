# ClusterFuzzLite builds the fuzz targets in the OSS-Fuzz base image (Linux,
# Clang, libFuzzer). Only the platform-independent parser is fuzzed, so no
# Windows SDK is needed here.
# Pinned by digest so the build is reproducible and Scorecard's
# Pinned-Dependencies check is satisfied. Dependabot (docker ecosystem, see
# .github/dependabot.yml) watches the :latest tag and opens a PR to bump the
# digest when OSS-Fuzz publishes a new base image, so pinning does not mean going
# stale.
FROM gcr.io/oss-fuzz-base/base-builder:latest@sha256:8b4a73d83374b298a0a771eeec9a1d44ceff3416a678b7d7946303389d022aca
COPY . $SRC/imemodepersistence
WORKDIR $SRC/imemodepersistence
COPY .clusterfuzzlite/build.sh $SRC/
