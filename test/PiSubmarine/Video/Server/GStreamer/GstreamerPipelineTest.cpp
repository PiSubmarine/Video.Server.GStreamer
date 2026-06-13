#include <memory>
#include <optional>
#include <string>
#include <vector>

#define private public
#include "PiSubmarine/Video/Server/GStreamer/GstreamerPipeline.h"
#undef private

#include <gtest/gtest.h>
#include <spdlog/sinks/null_sink.h>

namespace PiSubmarine::Video::Server::GStreamer
{
    namespace
    {
        [[nodiscard]] std::shared_ptr<spdlog::logger> CreateLogger()
        {
            return std::make_shared<spdlog::logger>(
                "Video.Server.GStreamer.Test",
                std::make_shared<spdlog::sinks::null_sink_mt>());
        }
    }

    TEST(GstreamerPipelineTest, BuildSourceDescriptionReturnsExplicitElementSource)
    {
        const auto logger = CreateLogger();

        EXPECT_EQ(
            GstreamerPipeline::BuildSourceDescription(ElementSource{.Description = "mfvideosrc device-index=1"}, logger),
            "mfvideosrc device-index=1");
    }

    TEST(GstreamerPipelineTest, BuildPayloaderDescriptionUsesLowLatencyRtpSettings)
    {
        EXPECT_EQ(
            GstreamerPipeline::BuildPayloaderDescription(),
            "h264parse config-interval=-1 ! rtph264pay pt=96 config-interval=-1 aggregate-mode=none");
    }

    TEST(GstreamerPipelineTest, BuildEncoderDescriptionUsesValidatedMediaFoundationSettingsForExplicitSource)
    {
        const auto logger = CreateLogger();

        EXPECT_EQ(
            GstreamerPipeline::BuildEncoderDescription(
                "mfvideosrc ! video/x-raw,width=1280,height=720,framerate=30/1",
                Control::Video::Api::StreamProfile::LowLatency,
                logger),
            "videoconvert ! video/x-raw,format=NV12 ! mfh264enc low-latency=true bitrate=1000");
    }
}
