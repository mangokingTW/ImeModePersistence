# ClusterFuzzLite builds the fuzz targets in the OSS-Fuzz base image (Linux,
# Clang, libFuzzer). Only the platform-independent parser is fuzzed, so no
# Windows SDK is needed here.
# Pinned by digest so the build is reproducible and Scorecard's
# Pinned-Dependencies check is satisfied. Dependabot (docker ecosystem, see
# .github/dependabot.yml) watches the :latest tag and opens a PR to bump the
# digest when OSS-Fuzz publishes a new base image, so pinning does not mean going
# stale.
FROM gcr.io/oss-fuzz-base/base-builder:latest@sha256:4d95d77396ce573bbfca794166cd2f1e28df5b3445a45132e52ef0e4ff7f566b
COPY . $SRC/imemodepersistence
WORKDIR $SRC/imemodepersistence
COPY .clusterfuzzlite/build.sh $SRC/
