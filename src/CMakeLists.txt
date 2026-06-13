include("${_build_cmake_local}")

find_package(spdlog CONFIG REQUIRED)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(GSTREAMER QUIET IMPORTED_TARGET
            gstreamer-1.0
            gstreamer-video-1.0
            gstreamer-rtp-1.0)
endif()

message("Adding library: ${PISUBMARINE_MODULE_NAME}")
add_library(${PISUBMARINE_MODULE_NAME}
        PiSubmarine/Video/Server/GStreamer/Controller.cpp
        PiSubmarine/Video/Server/GStreamer/ControllerCore.cpp
        PiSubmarine/Video/Server/GStreamer/GstreamerPipeline.cpp
        PiSubmarine/Video/Server/GStreamer/StaticPluginRegistration.cpp
        "../public/PiSubmarine/Video/Server/GStreamer/Config.h"
        "../public/PiSubmarine/Video/Server/GStreamer/Controller.h"
        "../public/PiSubmarine/Video/Server/GStreamer/Source.h"
        "PiSubmarine/Video/Server/GStreamer/ControllerCore.h"
        "PiSubmarine/Video/Server/GStreamer/Pipeline.h"
        "PiSubmarine/Video/Server/GStreamer/GstreamerPipeline.h"
        "PiSubmarine/Video/Server/GStreamer/StaticPluginRegistration.h")
target_include_directories(${PISUBMARINE_MODULE_NAME}
        PUBLIC "../public"
        PRIVATE "./")
PiSubmarineInitTarget(${PISUBMARINE_MODULE_NAME})
target_link_libraries(${PISUBMARINE_MODULE_NAME} PUBLIC spdlog::spdlog)
if(TARGET PkgConfig::GSTREAMER)
    pkg_get_variable(GSTREAMER_LIBRARY_DIR gstreamer-1.0 libdir)
else()
    find_path(GSTREAMER_INCLUDE_DIR gst/gst.h PATH_SUFFIXES gstreamer-1.0)
    find_path(GLIB_INCLUDE_DIR glib.h PATH_SUFFIXES glib-2.0)
    find_path(GLIB_CONFIG_INCLUDE_DIR glibconfig.h PATH_SUFFIXES lib/glib-2.0/include)
    find_library(GSTREAMER_LIBRARY NAMES gstreamer-1.0)
    find_library(GLIB_LIBRARY NAMES glib-2.0)
    find_library(GOBJECT_LIBRARY NAMES gobject-2.0)

    if(NOT GSTREAMER_INCLUDE_DIR OR NOT GLIB_INCLUDE_DIR OR NOT GLIB_CONFIG_INCLUDE_DIR OR
            NOT GSTREAMER_LIBRARY OR NOT GLIB_LIBRARY OR NOT GOBJECT_LIBRARY)
        message(FATAL_ERROR
                "Failed to locate GStreamer development files. "
                "Install GStreamer development packages such as "
                "'libgstreamer1.0-dev' and 'libgstreamer-plugins-base1.0-dev', "
                "or provide discovery hints through PKG_CONFIG_PATH / CMAKE_PREFIX_PATH.")
    endif()

    target_include_directories(${PISUBMARINE_MODULE_NAME} PRIVATE
            ${GSTREAMER_INCLUDE_DIR}
            ${GLIB_INCLUDE_DIR}
            ${GLIB_CONFIG_INCLUDE_DIR})
    get_filename_component(GSTREAMER_LIBRARY_DIR "${GSTREAMER_LIBRARY}" DIRECTORY)
endif()

set(GSTREAMER_STATIC_PLUGIN_DIR "")
if(GSTREAMER_LIBRARY_DIR)
    set(GSTREAMER_STATIC_PLUGIN_DIR "${GSTREAMER_LIBRARY_DIR}/gstreamer-1.0")
endif()
if(NOT GSTREAMER_STATIC_PLUGIN_DIR)
    set(_candidate_plugin_dirs
            "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/lib/gstreamer-1.0")
    foreach(_candidate_plugin_dir IN LISTS _candidate_plugin_dirs)
        if(EXISTS "${_candidate_plugin_dir}")
            set(GSTREAMER_STATIC_PLUGIN_DIR "${_candidate_plugin_dir}")
            break()
        endif()
    endforeach()
endif()

set(PISUBMARINE_GSTREAMER_STATIC_PLUGIN_NAMES
        coreelements
        autodetect
        videoconvertscale
        videoparsersbad
        openh264
        rtp
        udp)
set(PISUBMARINE_GSTREAMER_STATIC_PLUGIN_PACKAGES
        gstcoreelements
        gstautodetect
        gstvideoconvertscale
        gstvideoparsersbad
        gstopenh264
        gstrtp
        gstudp)

if(WIN32)
    list(APPEND PISUBMARINE_GSTREAMER_STATIC_PLUGIN_NAMES mediafoundation)
    list(APPEND PISUBMARINE_GSTREAMER_STATIC_PLUGIN_PACKAGES gstmediafoundation)
else()
    list(APPEND PISUBMARINE_GSTREAMER_STATIC_PLUGIN_NAMES video4linux2)
    list(APPEND PISUBMARINE_GSTREAMER_STATIC_PLUGIN_PACKAGES gstvideo4linux2)
endif()

function(_pisubmarine_collect_gstreamer_static_plugin plugin_basename result_variable)
    if(NOT GSTREAMER_STATIC_PLUGIN_DIR)
        set(${result_variable} "" PARENT_SCOPE)
        return()
    endif()

    set(_plugin_variable_name "PISUBMARINE_GSTREAMER_PLUGIN_${plugin_basename}")
    unset(${_plugin_variable_name} CACHE)
    unset(${_plugin_variable_name})

    set(_find_library_suffixes "${CMAKE_FIND_LIBRARY_SUFFIXES}")
    if(UNIX)
        set(CMAKE_FIND_LIBRARY_SUFFIXES "${CMAKE_STATIC_LIBRARY_SUFFIX}")
    endif()
    find_library(${_plugin_variable_name}
            NAMES "gst${plugin_basename}" "libgst${plugin_basename}"
            PATHS "${GSTREAMER_STATIC_PLUGIN_DIR}"
            NO_DEFAULT_PATH)
    set(CMAKE_FIND_LIBRARY_SUFFIXES "${_find_library_suffixes}")

    set(${result_variable} "${${_plugin_variable_name}}" PARENT_SCOPE)
endfunction()

message(STATUS "GStreamer static plugin dir: ${GSTREAMER_STATIC_PLUGIN_DIR}")

set(PISUBMARINE_GSTREAMER_STATIC_PLUGIN_LIBRARIES "")
set(PISUBMARINE_GSTREAMER_HAS_STATIC_PLUGINS FALSE)
if(TARGET PkgConfig::GSTREAMER)
    pkg_check_modules(GSTREAMER_STATIC_PLUGINS QUIET IMPORTED_TARGET
            ${PISUBMARINE_GSTREAMER_STATIC_PLUGIN_PACKAGES})
    if(TARGET PkgConfig::GSTREAMER_STATIC_PLUGINS)
        set(PISUBMARINE_GSTREAMER_HAS_STATIC_PLUGINS TRUE)
        message(STATUS "Linking GStreamer static plugins via pkg-config packages: ${PISUBMARINE_GSTREAMER_STATIC_PLUGIN_PACKAGES}")
        target_link_libraries(${PISUBMARINE_MODULE_NAME} PRIVATE PkgConfig::GSTREAMER_STATIC_PLUGINS)
    else()
        message(STATUS "Failed to resolve GStreamer static plugin packages via pkg-config; falling back to dynamic plugin discovery")
    endif()
else()
    set(PISUBMARINE_GSTREAMER_HAS_STATIC_PLUGINS TRUE)
    foreach(_plugin_basename IN LISTS PISUBMARINE_GSTREAMER_STATIC_PLUGIN_NAMES)
        _pisubmarine_collect_gstreamer_static_plugin("${_plugin_basename}" _plugin_library)
        if(_plugin_library)
            list(APPEND PISUBMARINE_GSTREAMER_STATIC_PLUGIN_LIBRARIES "${_plugin_library}")
        else()
            message(STATUS
                    "GStreamer static plugin archive '${_plugin_basename}' was not found in "
                    "${GSTREAMER_STATIC_PLUGIN_DIR}; skipping registration for this plugin")
            continue()
        endif()
    endforeach()

    if(PISUBMARINE_GSTREAMER_HAS_STATIC_PLUGINS)
        message(STATUS "Linking GStreamer static plugins: ${PISUBMARINE_GSTREAMER_STATIC_PLUGIN_LIBRARIES}")
        target_link_libraries(${PISUBMARINE_MODULE_NAME} PRIVATE ${PISUBMARINE_GSTREAMER_STATIC_PLUGIN_LIBRARIES})
    endif()
endif()

target_compile_definitions(${PISUBMARINE_MODULE_NAME}
        PRIVATE PISUBMARINE_GSTREAMER_HAS_STATIC_PLUGINS=$<BOOL:${PISUBMARINE_GSTREAMER_HAS_STATIC_PLUGINS}>)

if(TARGET PkgConfig::GSTREAMER)
    target_link_libraries(${PISUBMARINE_MODULE_NAME} PRIVATE PkgConfig::GSTREAMER)
else()
    target_link_libraries(${PISUBMARINE_MODULE_NAME} PRIVATE
            ${GSTREAMER_LIBRARY}
            ${GLIB_LIBRARY}
            ${GOBJECT_LIBRARY})
endif()

if(WIN32)
    set(PISUBMARINE_GSTREAMER_WINDOWS_SUPPORT_LIBRARY_DIRS
            "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/debug/lib"
            "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/lib")
    set(PISUBMARINE_GSTREAMER_WINDOWS_SUPPORT_LIBRARIES "")
    macro(_pisubmarine_append_windows_library result_variable library_label)
        set(_windows_library_names ${ARGN})
        if(NOT _windows_library_names)
            message(FATAL_ERROR "No library names were provided for '${library_label}'")
        endif()

        unset(PISUBMARINE_GSTREAMER_WINDOWS_SUPPORT_LIBRARY CACHE)
        find_library(PISUBMARINE_GSTREAMER_WINDOWS_SUPPORT_LIBRARY
                NAMES ${_windows_library_names}
                PATHS ${PISUBMARINE_GSTREAMER_WINDOWS_SUPPORT_LIBRARY_DIRS}
                NO_DEFAULT_PATH)
        if(NOT PISUBMARINE_GSTREAMER_WINDOWS_SUPPORT_LIBRARY)
            message(FATAL_ERROR
                    "Failed to locate required Windows GStreamer support library "
                    "'${library_label}' in: "
                    "${PISUBMARINE_GSTREAMER_WINDOWS_SUPPORT_LIBRARY_DIRS}")
        endif()

        list(APPEND ${result_variable}
                "${PISUBMARINE_GSTREAMER_WINDOWS_SUPPORT_LIBRARY}")
    endmacro()

    foreach(_windows_gstreamer_library_spec IN ITEMS
            "gstbase-1.0:gstbase-1.0"
            "gstvideo-1.0:gstvideo-1.0"
            "gstcodecparsers-1.0:gstcodecparsers-1.0"
            "gstrtp-1.0:gstrtp-1.0"
            "gstaudio-1.0:gstaudio-1.0"
            "gsttag-1.0:gsttag-1.0"
            "gstpbutils-1.0:gstpbutils-1.0"
            "gstnet-1.0:gstnet-1.0"
            "gstd3d11-1.0:gstd3d11-1.0"
            "gstd3dshader-1.0:gstd3dshader-1.0"
            "gstwinrt-1.0:gstwinrt-1.0"
            "gio-2.0:gio-2.0"
            "openh264:openh264")
        string(REPLACE ":" ";" _windows_gstreamer_library_parts "${_windows_gstreamer_library_spec}")
        list(GET _windows_gstreamer_library_parts 0 _windows_gstreamer_library_label)
        list(GET _windows_gstreamer_library_parts 1 _windows_gstreamer_library_name_list)
        _pisubmarine_append_windows_library(
                PISUBMARINE_GSTREAMER_WINDOWS_SUPPORT_LIBRARIES
                "${_windows_gstreamer_library_label}"
                ${_windows_gstreamer_library_name_list})
    endforeach()

    foreach(_windows_support_library_spec IN ITEMS
            "gmodule-2.0:gmodule-2.0"
            "intl:intl"
            "iconv:iconv"
            "zlib:zlibd;zlib"
            "pcre2-8:pcre2-8d;pcre2-8"
            "ffi:ffi")
        string(REPLACE ":" ";" _windows_support_library_parts "${_windows_support_library_spec}")
        list(GET _windows_support_library_parts 0 _windows_support_library_label)
        list(GET _windows_support_library_parts 1 _windows_support_library_name_list)
        _pisubmarine_append_windows_library(
                PISUBMARINE_GSTREAMER_WINDOWS_SUPPORT_LIBRARIES
                "${_windows_support_library_label}"
                ${_windows_support_library_name_list})
    endforeach()

    list(REMOVE_DUPLICATES PISUBMARINE_GSTREAMER_WINDOWS_SUPPORT_LIBRARIES)
    target_link_libraries(${PISUBMARINE_MODULE_NAME} PUBLIC
            ${PISUBMARINE_GSTREAMER_WINDOWS_SUPPORT_LIBRARIES}
            ws2_32
            winmm
            shlwapi
            dnsapi
            iphlpapi
            mf
            mfplat
            mfreadwrite
            mfuuid
            strmiids
            ole32
            runtimeobject
            d3d11
            dxgi)
endif()

PiSubmarineAddDependency("https://github.com/PiSubmarine/Control.Video.Api.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME} PUBLIC PiSubmarine.Control.Video.Api)

PiSubmarineAddDependency("https://github.com/PiSubmarine/Error.Api.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME} PUBLIC PiSubmarine.Error.Api)

PiSubmarineAddDependency("https://github.com/PiSubmarine/Lease.Api.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME} PUBLIC PiSubmarine.Lease.Api)

PiSubmarineAddDependency("https://github.com/PiSubmarine/Logging.Api.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME} PUBLIC PiSubmarine.Logging.Api)

PiSubmarineAddDependency("https://github.com/PiSubmarine/Time.Tickable.Api.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME} PUBLIC PiSubmarine.Time.Tickable.Api)

PiSubmarineAddDependency("https://github.com/PiSubmarine/Video.Subscription.Api.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME} PUBLIC PiSubmarine.Video.Subscription.Api)

PiSubmarineAddDependency("https://github.com/PiSubmarine/Video.Telemetry.Api.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME} PUBLIC PiSubmarine.Video.Telemetry.Api)
