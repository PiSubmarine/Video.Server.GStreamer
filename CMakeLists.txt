cmake_minimum_required(VERSION 3.25)

include(FetchContent)

FetchContent_Declare(
        PiSubmarineBuild
        GIT_REPOSITORY "https://github.com/PiSubmarine/Build.CMake.git"
        GIT_TAG        "main"
)

FetchContent_MakeAvailable(PiSubmarineBuild)

set(PISUBMARINE_BUILD_SYSTEM "${pisubmarinebuild_SOURCE_DIR}/PiSubmarine.Build.CMake.cmake")
set(_build_cmake_local "${pisubmarinebuild_SOURCE_DIR}/PiSubmarine.Build.CMake.cmake")

include("${pisubmarinebuild_SOURCE_DIR}/PiSubmarine.Build.CMake.cmake")

PiSubmarineInitModule("PiSubmarine.Video.Server.GStreamer")
project(${PISUBMARINE_MODULE_NAME})
enable_testing()
PiSubmarineAddDependency("https://github.com/PiSubmarine/Gstreamer.Build.git" "")
PiSubmarineConfigureModule()

if(TARGET ${PISUBMARINE_MODULE_NAME}.App)
    PiSubmarineGstreamerFinalizeCompositionRoot(${PISUBMARINE_MODULE_NAME}.App)
endif()
