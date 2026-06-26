#include <gtest/gtest.h>

#include "PiSubmarine/Video/Server/GStreamer/Config.h"

namespace PiSubmarine::Video::Server::GStreamer
{
    TEST(ConfigTest, DefaultsResourceIdToVideoMain)
    {
        const Config config;
        EXPECT_EQ(config.ResourceId.Value, "video-main");
    }

    TEST(ConfigTest, DefaultsToAutoDetectPipelineHead)
    {
        const Config config;
        EXPECT_TRUE(std::holds_alternative<AutoDetectPipelineHead>(config.VideoHead));
    }
}
