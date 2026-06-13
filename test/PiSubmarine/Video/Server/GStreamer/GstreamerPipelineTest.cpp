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
            "rtph264pay pt=96 config-interval=-1 aggregate-mode=zero-latency");
    }
}
