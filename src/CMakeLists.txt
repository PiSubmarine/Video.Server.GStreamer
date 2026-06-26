include("${_build_cmake_local}")

find_package(spdlog CONFIG REQUIRED)

message("Adding library: ${PISUBMARINE_MODULE_NAME}")
add_library(${PISUBMARINE_MODULE_NAME}
        PiSubmarine/Video/Server/GStreamer/Controller.cpp
        PiSubmarine/Video/Server/GStreamer/ControllerCore.cpp
        PiSubmarine/Video/Server/GStreamer/GstreamerPipeline.cpp
        "../public/PiSubmarine/Video/Server/GStreamer/Config.h"
        "../public/PiSubmarine/Video/Server/GStreamer/Controller.h"
        "../public/PiSubmarine/Video/Server/GStreamer/Head.h"
        "../public/PiSubmarine/Video/Server/GStreamer/Source.h"
        "PiSubmarine/Video/Server/GStreamer/ControllerCore.h"
        "PiSubmarine/Video/Server/GStreamer/Pipeline.h"
        "PiSubmarine/Video/Server/GStreamer/GstreamerPipeline.h")
target_include_directories(${PISUBMARINE_MODULE_NAME}
        PUBLIC "../public"
        PRIVATE "./")
PiSubmarineInitTarget(${PISUBMARINE_MODULE_NAME})
target_link_libraries(${PISUBMARINE_MODULE_NAME} PUBLIC spdlog::spdlog)

target_link_libraries(${PISUBMARINE_MODULE_NAME} PUBLIC PiSubmarine.Gstreamer.Build)

foreach(_gstreamer_plugin IN ITEMS
        coreelements
        autodetect
        videoconvertscale
        videoparsersbad
        openh264
        rtp
        udp)
    PiSubmarineGstreamerAddPlugin(${PISUBMARINE_MODULE_NAME} ${_gstreamer_plugin})
endforeach()

if(WIN32)
    PiSubmarineGstreamerAddPlugin(${PISUBMARINE_MODULE_NAME} mediafoundation)
else()
    PiSubmarineGstreamerAddPlugin(${PISUBMARINE_MODULE_NAME} video4linux2)
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
