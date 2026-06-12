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
        "../public/PiSubmarine/Video/Server/GStreamer/Config.h"
        "../public/PiSubmarine/Video/Server/GStreamer/Controller.h"
        "../public/PiSubmarine/Video/Server/GStreamer/Source.h"
        "PiSubmarine/Video/Server/GStreamer/ControllerCore.h"
        "PiSubmarine/Video/Server/GStreamer/Pipeline.h"
        "PiSubmarine/Video/Server/GStreamer/GstreamerPipeline.h")
target_include_directories(${PISUBMARINE_MODULE_NAME}
        PUBLIC "../public"
        PRIVATE "./")
PiSubmarineInitTarget(${PISUBMARINE_MODULE_NAME})
target_link_libraries(${PISUBMARINE_MODULE_NAME} PUBLIC spdlog::spdlog)
if(TARGET PkgConfig::GSTREAMER)
    target_link_libraries(${PISUBMARINE_MODULE_NAME} PRIVATE PkgConfig::GSTREAMER)
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
