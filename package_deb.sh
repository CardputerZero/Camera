#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
# SPDX-License-Identifier: MIT

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRESET="${PRESET:-cp0-cross}"
CONFIGURATION="${CONFIGURATION:-Release}"

SOURCE_VERSION="$(sed -nE 's/^set\(CAMERA_APP_VERSION "([0-9]+\.[0-9]+\.[0-9]+)".*$/\1/p' \
  "${ROOT_DIR}/CMakeLists.txt" | head -n1)"
if [[ -z "${SOURCE_VERSION}" ]]; then
  echo "Failed to read CAMERA_APP_VERSION from CMakeLists.txt" >&2
  exit 1
fi

# Arguments passed by the caller come last so an explicit version can override the source default.
cmake --preset "${PRESET}" -DCAMERA_APP_VERSION="${SOURCE_VERSION}" "$@"
cmake --build --preset "${PRESET}-rel"
cmake --build "${ROOT_DIR}/build/${PRESET}" --config "${CONFIGURATION}" --target package

BUILD_DIR="${ROOT_DIR}/build/${PRESET}"
CMAKE_CACHE="${BUILD_DIR}/CMakeCache.txt"
CPACK_CONFIG="${BUILD_DIR}/CPackConfig.cmake"

EFFECTIVE_VERSION="$(sed -nE 's/^CAMERA_APP_VERSION:[^=]+=(.+)$/\1/p' "${CMAKE_CACHE}" | head -n1)"
CPACK_VERSION="$(sed -nE 's/^set\(CPACK_PACKAGE_VERSION "([^"]+)"\)$/\1/p' \
  "${CPACK_CONFIG}" | head -n1)"
CPACK_DEBIAN_VERSION="$(sed -nE 's/^set\(CPACK_DEBIAN_PACKAGE_VERSION "([^"]+)"\)$/\1/p' \
  "${CPACK_CONFIG}" | head -n1)"
CPACK_DEBIAN_RELEASE="$(sed -nE 's/^set\(CPACK_DEBIAN_PACKAGE_RELEASE "([^"]*)"\)$/\1/p' \
  "${CPACK_CONFIG}" | head -n1)"
CPACK_OUTPUT_PREFIX="$(sed -nE 's/^set\(CPACK_OUTPUT_FILE_PREFIX "([^"]+)"\)$/\1/p' \
  "${CPACK_CONFIG}" | head -n1)"
CPACK_FILE_NAME="$(sed -nE 's/^set\(CPACK_PACKAGE_FILE_NAME "([^"]+)"\)$/\1/p' \
  "${CPACK_CONFIG}" | head -n1)"

if [[ -z "${EFFECTIVE_VERSION}" || "${CPACK_VERSION}" != "${EFFECTIVE_VERSION}" ||
      "${CPACK_DEBIAN_VERSION}" != "${EFFECTIVE_VERSION}" ]]; then
  echo "Package version mismatch: effective=${EFFECTIVE_VERSION:-missing}, " \
       "cpack=${CPACK_VERSION:-missing}, debian=${CPACK_DEBIAN_VERSION:-missing}" >&2
  exit 1
fi

PACKAGE_PATH="${CPACK_OUTPUT_PREFIX}/${CPACK_FILE_NAME}.deb"
if [[ ! -f "${PACKAGE_PATH}" ]]; then
  echo "Expected package was not generated: ${PACKAGE_PATH}" >&2
  exit 1
fi

EXPECTED_DEBIAN_VERSION="${EFFECTIVE_VERSION}"
if [[ -n "${CPACK_DEBIAN_RELEASE}" ]]; then
  EXPECTED_DEBIAN_VERSION+="-${CPACK_DEBIAN_RELEASE}"
fi
ACTUAL_DEBIAN_VERSION="$(dpkg-deb --field "${PACKAGE_PATH}" Version)"
if [[ "${ACTUAL_DEBIAN_VERSION}" != "${EXPECTED_DEBIAN_VERSION}" ]]; then
  echo "Generated package has version ${ACTUAL_DEBIAN_VERSION}; " \
       "expected ${EXPECTED_DEBIAN_VERSION}" >&2
  exit 1
fi

echo "Verified package version ${ACTUAL_DEBIAN_VERSION}: ${PACKAGE_PATH}"
