#
# SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
#
# SPDX-License-Identifier: MIT
#

cmake_minimum_required(VERSION 3.23)

set(CAMERA_APP_VERSION "0.3.7" CACHE STRING "CameraApp semantic version")
project(camera_app VERSION "${CAMERA_APP_VERSION}" LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json" FORCE)

set(USE_DESKTOP ON CACHE BOOL "Build the desktop emulator target with LVGL SDL support")
set(APP_NAME "${PROJECT_NAME}" CACHE STRING "Application name used for installed asset lookup")
set(APP_ASSETS_ROOT "" CACHE PATH "Optional runtime assets root. Expected subdirectories include fonts/ and audio/")
set(APP_KEY_INPUT_DEVICE "" CACHE STRING "Optional Linux evdev keyboard/keypad device path, e.g. /dev/input/event0")
set(APP_FRAMEBUFFER_DEVICE "/dev/fb0" CACHE STRING "Linux framebuffer device path used by embedded builds")
set(APP_DRM_DEVICE "/dev/dri/card0" CACHE STRING "Linux DRM device path used when APP_USE_DRM=ON")
set(APP_DRM_CONNECTOR_ID "-1" CACHE STRING "DRM connector id used when APP_USE_DRM=ON; -1 auto-selects")
set(APP_USE_DRM OFF CACHE BOOL "Enable LVGL Linux DRM/KMS backend for embedded Linux builds")
set(CM0_ALLOW_FETCH_DEPS ON CACHE BOOL "Allow downloading missing CM0 BSP/dependencies during cross builds")
option(CAMERA_APP_BUILD_TESTS "Build CameraApp unit tests" OFF)

include(FetchContent)
find_package(Threads REQUIRED)

set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(LV_BUILD_CONF_PATH "${PROJECT_SOURCE_DIR}/src/config/lv_conf.h" CACHE PATH "" FORCE)

if(DEFINED FETCHCONTENT_SOURCE_DIR_LVGL AND
   NOT FETCHCONTENT_SOURCE_DIR_LVGL STREQUAL "" AND
   NOT EXISTS "${FETCHCONTENT_SOURCE_DIR_LVGL}/CMakeLists.txt")
    message(WARNING
        "Ignoring missing FETCHCONTENT_SOURCE_DIR_LVGL: ${FETCHCONTENT_SOURCE_DIR_LVGL}. "
        "LVGL will be resolved through FetchContent."
    )
    unset(FETCHCONTENT_SOURCE_DIR_LVGL CACHE)
    unset(FETCHCONTENT_SOURCE_DIR_LVGL)
endif()

FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v9.5.0
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(lvgl)
if(NOT lvgl_POPULATED)
    if(POLICY CMP0169)
        cmake_policy(PUSH)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(lvgl)
    if(POLICY CMP0169)
        cmake_policy(POP)
    endif()
endif()
add_subdirectory("${lvgl_SOURCE_DIR}" "${lvgl_BINARY_DIR}" EXCLUDE_FROM_ALL)
set(LVGL_SOURCE_DIR "${lvgl_SOURCE_DIR}")
get_filename_component(LVGL_INCLUDE_PARENT "${LVGL_SOURCE_DIR}" DIRECTORY)

set(APP_DEP_INCLUDE_DIRS)
set(APP_DEP_LIBS)
set(LVGL_INCLUDE_DIRS)
set(LVGL_LIBS)
set(CJSON_INCLUDE_DIRS)

function(camera_app_require_header out_var header_name)
    find_path(${out_var} NAMES ${header_name} HINTS ${ARGN} NO_DEFAULT_PATH)
    if(NOT ${out_var})
        find_path(${out_var} NAMES ${header_name} HINTS ${ARGN})
    endif()
    if(NOT ${out_var})
        message(FATAL_ERROR "Required header not found: ${header_name}")
    endif()
    set(${out_var} "${${out_var}}" PARENT_SCOPE)
endfunction()

function(camera_app_require_library out_var)
    find_library(${out_var} NAMES ${ARGN} NO_DEFAULT_PATH)
    if(NOT ${out_var})
        find_library(${out_var} NAMES ${ARGN})
    endif()
    if(NOT ${out_var})
        message(FATAL_ERROR "Required library not found: ${ARGN}")
    endif()
    set(${out_var} "${${out_var}}" PARENT_SCOPE)
endfunction()

function(camera_app_add_imported_library target_name library_path include_dirs)
    if(NOT TARGET ${target_name})
        add_library(${target_name} UNKNOWN IMPORTED)
        set_target_properties(${target_name} PROPERTIES
            IMPORTED_LOCATION "${library_path}"
            INTERFACE_INCLUDE_DIRECTORIES "${include_dirs}"
        )
    endif()
endfunction()

if(USE_DESKTOP)
    message(STATUS "Configuring DESKTOP build")

    find_package(PNG CONFIG QUIET)
    if(NOT TARGET PNG::PNG)
        find_package(PNG REQUIRED)
    endif()

    find_package(JPEG CONFIG QUIET)
    if(NOT TARGET JPEG::JPEG)
        find_package(JPEG REQUIRED)
    endif()
    set(JPEG_TARGET JPEG::JPEG)

    find_package(Freetype CONFIG QUIET)
    if(NOT TARGET Freetype::Freetype)
        find_package(Freetype REQUIRED)
    endif()

    find_package(SDL2 CONFIG QUIET)
    if(TARGET SDL2::SDL2)
        set(SDL2_TARGET SDL2::SDL2)
    elseif(TARGET SDL2::SDL2-static)
        set(SDL2_TARGET SDL2::SDL2-static)
    else()
        find_package(SDL2 REQUIRED)
        if(DEFINED SDL2_LIBRARIES)
            set(SDL2_TARGET "")
        endif()
    endif()

    find_package(fmt CONFIG QUIET)
    if(NOT TARGET fmt::fmt)
        FetchContent_Declare(
            fmt
            GIT_REPOSITORY https://github.com/fmtlib/fmt.git
            GIT_TAG 11.1.4
            GIT_SHALLOW TRUE
        )
        set(FMT_INSTALL OFF CACHE BOOL "Disable fmt install rules for app packages" FORCE)
        set(FMT_TEST OFF CACHE BOOL "Disable fmt tests" FORCE)
        set(FMT_DOC OFF CACHE BOOL "Disable fmt docs" FORCE)
        FetchContent_MakeAvailable(fmt)
        if(TARGET fmt AND NOT TARGET fmt::fmt)
            add_library(fmt::fmt ALIAS fmt)
        endif()
    endif()

    find_package(ZLIB CONFIG QUIET)
    if(NOT TARGET ZLIB::ZLIB)
        find_package(ZLIB REQUIRED)
    endif()

    find_package(cJSON CONFIG QUIET)
    if(TARGET cjson)
        set(CJSON_TARGET cjson)
    elseif(TARGET cJSON::cJSON)
        set(CJSON_TARGET cJSON::cJSON)
    else()
        FetchContent_Declare(
            cjson
            GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
            GIT_TAG v1.7.18
            GIT_SHALLOW TRUE
        )
        set(ENABLE_CJSON_TEST OFF CACHE BOOL "" FORCE)
        set(ENABLE_CJSON_UTILS OFF CACHE BOOL "" FORCE)
        set(ENABLE_TARGET_EXPORT OFF CACHE BOOL "" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(cjson)
        if(TARGET cjson)
            set(CJSON_TARGET cjson)
        elseif(TARGET cJSON::cJSON)
            set(CJSON_TARGET cJSON::cJSON)
        else()
            message(FATAL_ERROR "Required cJSON target was not created")
        endif()
    endif()

    find_path(CJSON_HEADER_INCLUDE_DIR NAMES cJSON.h PATH_SUFFIXES cjson)
    if(CJSON_HEADER_INCLUDE_DIR)
        list(APPEND CJSON_INCLUDE_DIRS "${CJSON_HEADER_INCLUDE_DIR}")
    endif()
    if(DEFINED cjson_SOURCE_DIR)
        list(APPEND CJSON_INCLUDE_DIRS "${cjson_SOURCE_DIR}")
    endif()
    if(DEFINED cjson_BINARY_DIR)
        list(APPEND CJSON_INCLUDE_DIRS "${cjson_BINARY_DIR}")
    endif()

    find_path(MINIAUDIO_INCLUDE_DIR
        NAMES miniaudio.h
        HINTS "${CMAKE_CURRENT_SOURCE_DIR}/.cache/sdk_bsp-src/usr/include"
    )
    if(MINIAUDIO_INCLUDE_DIR)
        message(STATUS "miniaudio backend enabled")
    else()
        message(FATAL_ERROR
            "miniaudio.h was not found; install libminiaudio-dev or provide it in "
            ".cache/sdk_bsp-src/usr/include"
        )
    endif()

    set(LVGL_DESKTOP_DEPS ZLIB::ZLIB PNG::PNG ${JPEG_TARGET} Freetype::Freetype ${SDL2_TARGET})
    foreach(dep IN LISTS LVGL_DESKTOP_DEPS)
        if(TARGET ${dep})
            target_link_libraries(lvgl PUBLIC ${dep})
        endif()
    endforeach()

    if(SDL2_FOUND AND NOT TARGET SDL2::SDL2)
        target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
        target_link_libraries(lvgl PUBLIC ${SDL2_LIBRARIES})
    endif()

    target_compile_definitions(lvgl PUBLIC USE_DESKTOP=1 APP_USE_DRM=0)
else()
    message(STATUS "Configuring CM0/aarch64 Linux build")

    if(NOT CM0_SDK_ROOT OR NOT EXISTS "${CM0_SDK_ROOT}")
        message(FATAL_ERROR
            "CM0 SDK sysroot not found or invalid.\n"
            "Use -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/cp0-aarch64-linux-gnu.cmake "
            "or configure with -DCM0_SDK_ROOT=<path_to_sdk_sysroot>."
        )
    endif()

    if(CMAKE_SYSROOT)
        set(CM0_SYSROOT "${CMAKE_SYSROOT}")
    else()
        set(CM0_SYSROOT "${CM0_SDK_ROOT}")
    endif()
    if(NOT CM0_MULTIARCH)
        set(CM0_MULTIARCH "aarch64-linux-gnu")
    endif()
    message(STATUS "Using CM0 SDK root: ${CM0_SDK_ROOT}")
    message(STATUS "Using CM0 dependency root: ${CM0_SYSROOT}")

    set(CM0_DEPENDENCY_ROOTS "${CM0_SYSROOT}" "${CM0_SDK_ROOT}")
    list(REMOVE_DUPLICATES CM0_DEPENDENCY_ROOTS)
    set(CM0_INCLUDE_HINTS)
    set(CM0_LIBRARY_HINTS)
    foreach(CM0_DEPENDENCY_ROOT IN LISTS CM0_DEPENDENCY_ROOTS)
        list(APPEND CM0_INCLUDE_HINTS
            "${CM0_DEPENDENCY_ROOT}/usr/include/${CM0_MULTIARCH}"
            "${CM0_DEPENDENCY_ROOT}/usr/include"
            "${CM0_DEPENDENCY_ROOT}/usr/local/include"
            "${CM0_DEPENDENCY_ROOT}/include/${CM0_MULTIARCH}"
            "${CM0_DEPENDENCY_ROOT}/include"
        )
        list(APPEND CM0_LIBRARY_HINTS
            "${CM0_DEPENDENCY_ROOT}/usr/lib/${CM0_MULTIARCH}"
            "${CM0_DEPENDENCY_ROOT}/usr/lib"
            "${CM0_DEPENDENCY_ROOT}/usr/local/lib/${CM0_MULTIARCH}"
            "${CM0_DEPENDENCY_ROOT}/usr/local/lib"
            "${CM0_DEPENDENCY_ROOT}/lib/${CM0_MULTIARCH}"
            "${CM0_DEPENDENCY_ROOT}/lib"
        )
    endforeach()
    list(REMOVE_DUPLICATES CM0_INCLUDE_HINTS)
    list(REMOVE_DUPLICATES CM0_LIBRARY_HINTS)

    function(cm0_find_header out_var header_name)
        find_path(${out_var} NAMES ${header_name} HINTS ${ARGN} ${CM0_INCLUDE_HINTS} NO_DEFAULT_PATH)
        set(${out_var} "${${out_var}}" PARENT_SCOPE)
    endfunction()

    function(cm0_find_library out_var)
        find_library(${out_var} NAMES ${ARGN} HINTS ${CM0_LIBRARY_HINTS} NO_DEFAULT_PATH)
        set(${out_var} "${${out_var}}" PARENT_SCOPE)
    endfunction()

    cm0_find_header(FREETYPE_INCLUDE_DIR ft2build.h "${CM0_SYSROOT}/usr/include/freetype2" "${CM0_SYSROOT}/usr/include")
    cm0_find_library(FREETYPE_LIBRARY freetype)
    if(NOT FREETYPE_INCLUDE_DIR OR NOT FREETYPE_LIBRARY)
        message(FATAL_ERROR "Missing required Freetype header/library in BSP sysroot: ${CM0_SYSROOT}")
    endif()
    camera_app_add_imported_library(CM0::Freetype "${FREETYPE_LIBRARY}" "${FREETYPE_INCLUDE_DIR}")

    cm0_find_header(PNG_INCLUDE_DIR png.h "${CM0_SYSROOT}/usr/include/libpng16" "${CM0_SYSROOT}/usr/include/libpng" "${CM0_SYSROOT}/usr/include")
    cm0_find_library(PNG_LIBRARY png16 png)
    if(NOT PNG_INCLUDE_DIR OR NOT PNG_LIBRARY)
        message(FATAL_ERROR "Missing required PNG header/library in BSP sysroot: ${CM0_SYSROOT}")
    endif()
    camera_app_add_imported_library(CM0::PNG "${PNG_LIBRARY}" "${PNG_INCLUDE_DIR}")

    cm0_find_header(JPEG_INCLUDE_DIR jpeglib.h "${CM0_SYSROOT}/usr/include")
    cm0_find_library(JPEG_LIBRARY jpeg)
    if(NOT JPEG_INCLUDE_DIR OR NOT JPEG_LIBRARY)
        message(FATAL_ERROR "Missing required JPEG header/library in BSP sysroot: ${CM0_SYSROOT}")
    endif()
    camera_app_add_imported_library(CM0::JPEG "${JPEG_LIBRARY}" "${JPEG_INCLUDE_DIR}")

    cm0_find_header(ZLIB_INCLUDE_DIR zlib.h "${CM0_SYSROOT}/usr/include")
    cm0_find_library(ZLIB_LIBRARY z)
    if(NOT ZLIB_INCLUDE_DIR OR NOT ZLIB_LIBRARY)
        message(FATAL_ERROR "Missing required ZLIB header/library in BSP sysroot: ${CM0_SYSROOT}")
    endif()
    camera_app_add_imported_library(ZLIB::ZLIB "${ZLIB_LIBRARY}" "${ZLIB_INCLUDE_DIR}")

    cm0_find_header(MINIAUDIO_INCLUDE_DIR miniaudio.h "${CM0_SYSROOT}/usr/include" "${CM0_SDK_ROOT}/usr/include")
    if(MINIAUDIO_INCLUDE_DIR)
        message(STATUS "miniaudio backend enabled")
    else()
        message(FATAL_ERROR "miniaudio.h was not found in the CM0 SDK sysroot")
    endif()

    cm0_find_header(LIBCAMERA_INCLUDE_DIR libcamera/camera.h "${CM0_SYSROOT}/usr/include")
    cm0_find_library(LIBCAMERA_LIBRARY camera libcamera.so)
    cm0_find_library(LIBCAMERA_BASE_LIBRARY camera-base libcamera-base.so)
    if(NOT LIBCAMERA_INCLUDE_DIR OR NOT LIBCAMERA_LIBRARY OR NOT LIBCAMERA_BASE_LIBRARY)
        message(FATAL_ERROR "Missing required libcamera headers/libraries in BSP sysroot: ${CM0_SYSROOT}")
    endif()
    camera_app_add_imported_library(CM0::Libcamera "${LIBCAMERA_LIBRARY}" "${LIBCAMERA_INCLUDE_DIR}")
    camera_app_add_imported_library(CM0::LibcameraBase "${LIBCAMERA_BASE_LIBRARY}" "${LIBCAMERA_INCLUDE_DIR}")

    cm0_find_header(FMT_INCLUDE_DIR fmt/format.h "${CM0_SYSROOT}/usr/include")
    cm0_find_library(FMT_LIBRARY fmt libfmt.so libfmt.so.11 libfmt.so.10)
    if(NOT FMT_INCLUDE_DIR OR NOT FMT_LIBRARY)
        message(FATAL_ERROR "Missing required fmt header/library in BSP sysroot: ${CM0_SYSROOT}")
    endif()
    camera_app_add_imported_library(fmt::fmt "${FMT_LIBRARY}" "${FMT_INCLUDE_DIR}")

    cm0_find_header(LIBCJSON_INCLUDE_DIR cjson/cJSON.h "${CM0_SYSROOT}/usr/include" "${CM0_SDK_ROOT}/usr/include")
    if(NOT LIBCJSON_INCLUDE_DIR)
        cm0_find_header(LIBCJSON_INCLUDE_DIR cJSON.h "${CM0_SYSROOT}/usr/include" "${CM0_SDK_ROOT}/usr/include")
    endif()
    cm0_find_library(LIBCJSON_LIBRARY cjson libcjson.so libcjson.a)
    if(NOT LIBCJSON_INCLUDE_DIR OR NOT LIBCJSON_LIBRARY)
        message(FATAL_ERROR "Missing required cJSON header/library in BSP sysroot: ${CM0_SYSROOT}")
    endif()
    camera_app_add_imported_library(CM0::Libcjson "${LIBCJSON_LIBRARY}" "${LIBCJSON_INCLUDE_DIR}")
    set(CJSON_TARGET CM0::Libcjson)
    list(APPEND CJSON_INCLUDE_DIRS "${LIBCJSON_INCLUDE_DIR}")

    cm0_find_header(LIBV4L2_INCLUDE_DIR libv4l2.h "${CM0_SYSROOT}/usr/include")
    cm0_find_library(LIBV4L2_LIBRARY v4l2 libv4l2.so libv4l2.a)
    if(NOT LIBV4L2_INCLUDE_DIR OR NOT LIBV4L2_LIBRARY)
        message(FATAL_ERROR "Missing required libv4l2 header/library in BSP sysroot: ${CM0_SYSROOT}")
    endif()
    camera_app_add_imported_library(CM0::V4L2 "${LIBV4L2_LIBRARY}" "${LIBV4L2_INCLUDE_DIR}")

    set(CM0_MULTIARCH_INCLUDE_DIRS)
    foreach(CM0_INCLUDE_DIR IN ITEMS "${CM0_SYSROOT}/usr/include/${CM0_MULTIARCH}" "${CM0_SYSROOT}/include/${CM0_MULTIARCH}")
        if(EXISTS "${CM0_INCLUDE_DIR}/bits/timesize.h" OR EXISTS "${CM0_INCLUDE_DIR}/bits/wordsize.h")
            list(APPEND CM0_MULTIARCH_INCLUDE_DIRS "${CM0_INCLUDE_DIR}")
            message(STATUS "Found multiarch include: ${CM0_INCLUDE_DIR}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES CM0_MULTIARCH_INCLUDE_DIRS)

    cm0_find_library(PTHREAD_LIBRARY pthread libpthread.so.0 libpthread.a)
    cm0_find_library(M_LIBRARY m libm.so libm.so.6 libm.a)
    if(NOT PTHREAD_LIBRARY)
        message(FATAL_ERROR "Missing required pthread library in BSP sysroot: ${CM0_SYSROOT}")
    endif()
    if(NOT M_LIBRARY)
        message(FATAL_ERROR "Missing required math library in BSP sysroot: ${CM0_SYSROOT}")
    endif()

    if(APP_USE_DRM)
        cm0_find_header(DRM_XF86_INCLUDE_DIR xf86drm.h "${CM0_SYSROOT}/usr/include" "${CM0_SYSROOT}/usr/include/libdrm")
        cm0_find_header(DRM_XF86_MODE_INCLUDE_DIR xf86drmMode.h "${CM0_SYSROOT}/usr/include" "${CM0_SYSROOT}/usr/include/libdrm")
        cm0_find_header(DRM_MODE_INCLUDE_DIR drm.h "${CM0_SYSROOT}/usr/include/drm" "${CM0_SYSROOT}/usr/include/libdrm" "${CM0_SYSROOT}/usr/include")
        cm0_find_library(DRM_LIBRARY drm libdrm.so libdrm.so.2)
        if(NOT DRM_XF86_INCLUDE_DIR OR NOT DRM_XF86_MODE_INCLUDE_DIR OR NOT DRM_MODE_INCLUDE_DIR OR NOT DRM_LIBRARY)
            message(FATAL_ERROR "APP_USE_DRM=ON but libdrm headers/library were not found in BSP sysroot.")
        endif()
        camera_app_add_imported_library(CM0::DRM "${DRM_LIBRARY}" "${DRM_XF86_INCLUDE_DIR};${DRM_XF86_MODE_INCLUDE_DIR};${DRM_MODE_INCLUDE_DIR}")
        message(STATUS "CM0 DRM display support enabled")
    else()
        message(STATUS "CM0 framebuffer display backend is the default; DRM support disabled")
    endif()

    set_target_properties(CM0::PNG PROPERTIES INTERFACE_LINK_LIBRARIES ZLIB::ZLIB)
    set_target_properties(CM0::Freetype PROPERTIES INTERFACE_LINK_LIBRARIES "CM0::PNG;ZLIB::ZLIB")

    list(APPEND APP_DEP_INCLUDE_DIRS
        ${CM0_MULTIARCH_INCLUDE_DIRS}
        ${LIBCAMERA_INCLUDE_DIR}
        ${LIBV4L2_INCLUDE_DIR}
    )
    list(APPEND APP_DEP_LIBS
        CM0::Libcamera
        CM0::LibcameraBase
        CM0::V4L2
        ${PTHREAD_LIBRARY}
        ${M_LIBRARY}
    )

    set(LVGL_INCLUDE_DIRS ${FREETYPE_INCLUDE_DIR} ${PNG_INCLUDE_DIR} ${JPEG_INCLUDE_DIR} ${ZLIB_INCLUDE_DIR})
    set(LVGL_LIBS CM0::Freetype CM0::PNG CM0::JPEG ZLIB::ZLIB)
    if(APP_USE_DRM)
        list(APPEND LVGL_INCLUDE_DIRS ${DRM_XF86_INCLUDE_DIR} ${DRM_XF86_MODE_INCLUDE_DIR} ${DRM_MODE_INCLUDE_DIR})
        list(APPEND LVGL_LIBS CM0::DRM)
    endif()

    list(REMOVE_DUPLICATES APP_DEP_INCLUDE_DIRS)
    list(REMOVE_DUPLICATES APP_DEP_LIBS)
    list(REMOVE_DUPLICATES LVGL_INCLUDE_DIRS)
    list(REMOVE_DUPLICATES LVGL_LIBS)
    list(REMOVE_DUPLICATES CJSON_INCLUDE_DIRS)

    target_compile_definitions(lvgl PUBLIC USE_DESKTOP=0 APP_USE_DRM=$<BOOL:${APP_USE_DRM}>)
    target_include_directories(lvgl PUBLIC ${LVGL_INCLUDE_DIRS} ${APP_DEP_INCLUDE_DIRS})
    target_link_libraries(lvgl PUBLIC ${LVGL_LIBS} ${APP_DEP_LIBS})
endif()

if(UNIX AND NOT APPLE)
    list(APPEND APP_DEP_LIBS ${CMAKE_DL_LIBS} m)
    list(REMOVE_DUPLICATES APP_DEP_LIBS)
endif()

set(APP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(APP_COMMON_INCLUDE_DIRS
    ${APP_SOURCE_DIR}
    ${LVGL_INCLUDE_PARENT}
    ${LVGL_INCLUDE_DIRS}
    ${APP_DEP_INCLUDE_DIRS}
    ${CJSON_INCLUDE_DIRS}
)

add_library(${PROJECT_NAME}_utils STATIC
    src/utils/logger.cpp
    src/utils/asset_manager.cpp
    src/utils/device_config.cpp
    src/utils/json_helper.cpp
)
target_include_directories(${PROJECT_NAME}_utils PUBLIC ${APP_COMMON_INCLUDE_DIRS})
target_compile_definitions(${PROJECT_NAME}_utils PUBLIC
    APP_CMAKE_ASSETS_ROOT="${APP_ASSETS_ROOT}"
    APP_SOURCE_ASSETS_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)
target_link_libraries(${PROJECT_NAME}_utils PUBLIC fmt::fmt ${CJSON_TARGET})

add_library(${PROJECT_NAME}_services STATIC
    src/services/audio_service.cpp
    src/services/camera_backend_utils.cpp
    src/services/jpeg_metadata.cpp
    src/services/video_recorder.cpp
    src/services/libcamera_backend.cpp
    src/services/camera_service.cpp
    src/services/v4l2_backend.cpp
    src/services/gallery_service.cpp
)
target_include_directories(${PROJECT_NAME}_services PUBLIC ${APP_COMMON_INCLUDE_DIRS})
target_compile_definitions(${PROJECT_NAME}_services PUBLIC
    USE_DESKTOP=$<BOOL:${USE_DESKTOP}>
    APP_MINIAUDIO_HEADER="${MINIAUDIO_INCLUDE_DIR}/miniaudio.h"
)
target_link_libraries(${PROJECT_NAME}_services PUBLIC ${PROJECT_NAME}_utils ${APP_DEP_LIBS})

if(CAMERA_APP_BUILD_TESTS)
    enable_testing()
    add_executable(camera_preview_test tests/camera_preview_test.cpp)
    target_include_directories(camera_preview_test PRIVATE ${APP_SOURCE_DIR})
    target_link_libraries(camera_preview_test PRIVATE
        ${PROJECT_NAME}_services
        $<$<BOOL:${USE_DESKTOP}>:${JPEG_TARGET}>
        $<$<NOT:$<BOOL:${USE_DESKTOP}>>:CM0::JPEG>
    )
    add_test(NAME camera_preview_test COMMAND camera_preview_test)

    add_executable(device_config_test tests/device_config_test.cpp)
    target_include_directories(device_config_test PRIVATE ${APP_SOURCE_DIR})
    target_link_libraries(device_config_test PRIVATE ${PROJECT_NAME}_utils)
    add_test(NAME device_config_test COMMAND device_config_test)
endif()

add_library(${PROJECT_NAME}_viewmodels STATIC
    src/viewmodels/splash_viewmodel.cpp
    src/viewmodels/camera_viewmodel.cpp
    src/viewmodels/gallery_viewmodel.cpp
)
target_include_directories(${PROJECT_NAME}_viewmodels PUBLIC ${APP_COMMON_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME}_viewmodels PUBLIC ${PROJECT_NAME}_services lvgl)

add_library(${PROJECT_NAME}_ui STATIC
    src/ui/app_style.cpp
    src/ui/app_font.cpp
    src/views/camera_view.cpp
    src/views/gallery_view.cpp
    src/views/help_popup.cpp
    src/views/splash_view.cpp
)
target_include_directories(${PROJECT_NAME}_ui PUBLIC ${APP_COMMON_INCLUDE_DIRS})
target_compile_definitions(${PROJECT_NAME}_ui PUBLIC USE_DESKTOP=$<BOOL:${USE_DESKTOP}>)
target_link_libraries(${PROJECT_NAME}_ui PUBLIC ${PROJECT_NAME}_viewmodels lvgl::lvgl)

add_library(${PROJECT_NAME}_input STATIC
    src/input/linux_keypad.cpp
    src/input/sdl_keypad.cpp
)
target_include_directories(${PROJECT_NAME}_input PUBLIC ${APP_COMMON_INCLUDE_DIRS})
target_compile_definitions(${PROJECT_NAME}_input PUBLIC
    USE_DESKTOP=$<BOOL:${USE_DESKTOP}>
    APP_KEY_INPUT_DEVICE="${APP_KEY_INPUT_DEVICE}"
)
target_link_libraries(${PROJECT_NAME}_input PUBLIC ${PROJECT_NAME}_utils lvgl::lvgl)

if(USE_DESKTOP)
    target_link_libraries(${PROJECT_NAME}_ui PUBLIC ${SDL2_TARGET})
    target_link_libraries(${PROJECT_NAME}_input PUBLIC ${SDL2_TARGET})
endif()

add_executable(${PROJECT_NAME} src/main.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE ${APP_COMMON_INCLUDE_DIRS})
target_compile_definitions(${PROJECT_NAME} PRIVATE
    USE_DESKTOP=$<BOOL:${USE_DESKTOP}>
    APP_USE_DRM=$<BOOL:${APP_USE_DRM}>
    APP_FRAMEBUFFER_DEVICE="${APP_FRAMEBUFFER_DEVICE}"
    APP_DRM_DEVICE="${APP_DRM_DEVICE}"
    APP_DRM_CONNECTOR_ID=\(${APP_DRM_CONNECTOR_ID}\)
)
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${PROJECT_NAME}_ui
    ${PROJECT_NAME}_input
    ${PROJECT_NAME}_services
    ${APP_DEP_LIBS}
    Threads::Threads
)

if(NOT USE_DESKTOP)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        SKIP_BUILD_RPATH TRUE
        BUILD_WITH_INSTALL_RPATH FALSE
        INSTALL_RPATH ""
    )
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,--allow-shlib-undefined)
endif()

include(cmake/cm0-package.cmake)
