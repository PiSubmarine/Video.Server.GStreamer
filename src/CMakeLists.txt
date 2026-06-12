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
    target_link_libraries(${PISUBMARINE_MODULE_NAME} PRIVATE PkgConfig::GSTREAMER)
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
    target_link_libraries(${PISUBMARINE_MODULE_NAME} PRIVATE
            ${GSTREAMER_LIBRARY}
            ${GLIB_LIBRARY}
            ${GOBJECT_LIBRARY})
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

function(_pisubmarine_link_gstreamer_static_plugin plugin_basename)
    if(NOT GSTREAMER_STATIC_PLUGIN_DIR)
        message(STATUS "Skipping GStreamer static plugin '${plugin_basename}' because plugin dir is empty")
        return()
    endif()

    find_library(_plugin_library
            NAMES "gst${plugin_basename}" "libgst${plugin_basename}"
            PATHS "${GSTREAMER_STATIC_PLUGIN_DIR}"
            NO_DEFAULT_PATH)
    if(_plugin_library)
        message(STATUS "Linking GStreamer static plugin '${plugin_basename}' from ${_plugin_library}")
        target_link_libraries(${PISUBMARINE_MODULE_NAME} PUBLIC "${_plugin_library}")
    else()
        message(STATUS "Failed to locate GStreamer static plugin '${plugin_basename}' in ${GSTREAMER_STATIC_PLUGIN_DIR}")
    endif()
endfunction()

message(STATUS "GStreamer static plugin dir: ${GSTREAMER_STATIC_PLUGIN_DIR}")

_pisubmarine_link_gstreamer_static_plugin("coreelements")
_pisubmarine_link_gstreamer_static_plugin("autodetect")
_pisubmarine_link_gstreamer_static_plugin("videoconvertscale")
_pisubmarine_link_gstreamer_static_plugin("video4linux2")
_pisubmarine_link_gstreamer_static_plugin("openh264")
_pisubmarine_link_gstreamer_static_plugin("rtp")
_pisubmarine_link_gstreamer_static_plugin("udp")

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
