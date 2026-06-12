cmake_minimum_required(VERSION 3.20)

include(FetchContent)

project(${PISUBMARINE_MODULE_NAME}.Test LANGUAGES CXX)

set(${PISUBMARINE_MODULE_NAME}.Test.Sources
        "PiSubmarine/Video/Server/GStreamer/ControllerCoreTest.cpp"
        "PiSubmarine/Video/Server/GStreamer/ConfigTest.cpp")

enable_testing()

find_package(GTest)
find_package(spdlog CONFIG REQUIRED)

add_executable(${PISUBMARINE_MODULE_NAME}.Test ${${PISUBMARINE_MODULE_NAME}.Test.Sources})
target_include_directories(${PISUBMARINE_MODULE_NAME}.Test PRIVATE "../src")
PiSubmarineInitTarget(${PISUBMARINE_MODULE_NAME}.Test)

target_link_libraries(${PISUBMARINE_MODULE_NAME}.Test PRIVATE ${PISUBMARINE_MODULE_NAME})
target_link_libraries(${PISUBMARINE_MODULE_NAME}.Test PRIVATE PiSubmarine.Lease.Api.Mock)
target_link_libraries(${PISUBMARINE_MODULE_NAME}.Test PRIVATE GTest::gtest_main)
target_link_libraries(${PISUBMARINE_MODULE_NAME}.Test PRIVATE spdlog::spdlog)
include(GoogleTest)
if(CMAKE_CROSSCOMPILING)
    gtest_add_tests(TARGET ${PISUBMARINE_MODULE_NAME}.Test)
else()
    gtest_discover_tests(${PISUBMARINE_MODULE_NAME}.Test)
endif()
