#include <gtest/gtest.h>

#include "PiSubmarine/Video/Server/GStreamer/Config.h"

namespace PiSubmarine::Video::Server::GStreamer
{
    TEST(ConfigTest, DefaultsResourceIdToVideoMain)
    {
        const Config config;
        EXPECT_EQ(config.ResourceId.Value, "video-main");
    }
}
