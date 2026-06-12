#pragma once

#include "PiSubmarine/Lease/Api/Identifiers.h"
#include "PiSubmarine/Video/Server/GStreamer/Source.h"

namespace PiSubmarine::Video::Server::GStreamer
{
    struct Config
    {
        Lease::Api::ResourceId ResourceId{.Value = "video-main"};
        Source VideoSource{AutoDetectSource{}};

        [[nodiscard]] bool operator==(const Config&) const = default;
    };
}
