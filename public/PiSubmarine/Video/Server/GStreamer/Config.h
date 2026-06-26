#pragma once

#include "PiSubmarine/Lease/Api/Identifiers.h"
#include "PiSubmarine/Video/Server/GStreamer/Head.h"

namespace PiSubmarine::Video::Server::GStreamer
{
    struct Config
    {
        Lease::Api::ResourceId ResourceId{.Value = "video-main"};
        PipelineHead VideoHead{AutoDetectPipelineHead{}};

        [[nodiscard]] bool operator==(const Config&) const = default;
    };
}
