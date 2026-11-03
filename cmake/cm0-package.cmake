#
# SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
#
# SPDX-License-Identifier: MIT
#

include(GNUInstallDirs)

if(NOT DEFINED CAMERA_APP_VERSION OR CAMERA_APP_VERSION STREQUAL "")
    message(FATAL_ERROR "CAMERA_APP_VERSION must be set before configuring the package")
endif()
if(NOT PROJECT_VERSION STREQUAL CAMERA_APP_VERSION)
    message(FATAL_ERROR
        "Package version mismatch: PROJECT_VERSION=${PROJECT_VERSION}, "
        "CAMERA_APP_VERSION=${CAMERA_APP_VERSION}"
    )
endif()

set(APP_DISPLAY_NAME "Camera" CACHE STRING "Human-readable application name used by launchers and package filename")
set(APP_DEBIAN_REVISION "m5stack1" CACHE STRING "Debian package revision/vendor suffix")
set(APP_DEBIAN_ARCHITECTURE "arm64" CACHE STRING "Debian package architecture")
set(APP_MAINTAINER "M5Stack <support@m5stack.com>" CACHE STRING "Debian package maintainer")
set(APP_PACKAGE_DESCRIPTION "Camera application for M5CardputerZero APPLaunch" CACHE STRING "Debian package summary")

set(APP_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/package")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/templates/camera_app.desktop.in"
    "${APP_GENERATED_DIR}/${PROJECT_NAME}.desktop"
    @ONLY
)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/audio/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_NAME}/audio"
    PATTERN ".DS_Store" EXCLUDE
    PATTERN "._*" EXCLUDE
)
install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_NAME}/fonts"
    PATTERN ".DS_Store" EXCLUDE
    PATTERN "._*" EXCLUDE
)
install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_NAME}/images"
    PATTERN ".DS_Store" EXCLUDE
    PATTERN "._*" EXCLUDE
)

# Compatibility path for the original CameraApp asset lookup.
install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/CameraApp/assets"
    PATTERN ".DS_Store" EXCLUDE
    PATTERN "._*" EXCLUDE
)
install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/audio"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/CameraApp/assets"
    PATTERN ".DS_Store" EXCLUDE
    PATTERN "._*" EXCLUDE
)

install(
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/camera1.png"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/APPLaunch/share/images"
)
install(
    FILES "${APP_GENERATED_DIR}/${PROJECT_NAME}.desktop"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/APPLaunch/applications"
)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md" DESTINATION "${CMAKE_INSTALL_DOCDIR}")
install(
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/License.txt"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
    RENAME "third-party-assets-license.txt"
)

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/dist")
set(CPACK_PACKAGE_NAME "${APP_DISPLAY_NAME}")
set(CPACK_PACKAGE_VENDOR "M5Stack")
set(CPACK_PACKAGE_CONTACT "${APP_MAINTAINER}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${APP_PACKAGE_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${CAMERA_APP_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "${APP_DISPLAY_NAME}_${CAMERA_APP_VERSION}_${APP_DEBIAN_REVISION}_${APP_DEBIAN_ARCHITECTURE}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/License.txt")

string(TOLOWER "${APP_DISPLAY_NAME}" APP_DEBIAN_PACKAGE_NAME)
string(REGEX REPLACE "[^a-z0-9+.-]" "-" APP_DEBIAN_PACKAGE_NAME "${APP_DEBIAN_PACKAGE_NAME}")
set(CPACK_DEBIAN_PACKAGE_NAME "${APP_DEBIAN_PACKAGE_NAME}")
set(CPACK_DEBIAN_PACKAGE_VERSION "${CAMERA_APP_VERSION}")
set(CPACK_DEBIAN_PACKAGE_RELEASE "${APP_DEBIAN_REVISION}")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${APP_DEBIAN_ARCHITECTURE}")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${APP_MAINTAINER}")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")

set(APP_DEBIAN_PACKAGE_DEPENDS
    libc6
    libstdc++6
    libgcc-s1
    libfreetype6
    libpng16-16
    libjpeg62-turbo
    zlib1g
    libfmt10
    # libcamera 0.7.2 rebuilt the public API on std::span without bumping the
    # SONAME
    "libcamera0.7 (>= 0.7.2)"
    libcamera-ipa
    libpulse0
    libcjson1
    libv4l-0
)
if(APP_USE_DRM)
    list(APPEND APP_DEBIAN_PACKAGE_DEPENDS libdrm2)
endif()
list(REMOVE_DUPLICATES APP_DEBIAN_PACKAGE_DEPENDS)
string(REPLACE ";" ", " CPACK_DEBIAN_PACKAGE_DEPENDS "${APP_DEBIAN_PACKAGE_DEPENDS}")

set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)

include(CPack)
