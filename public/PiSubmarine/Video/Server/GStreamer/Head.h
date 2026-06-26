#pragma once

#include <string>
#include <variant>
#include <vector>

#include "PiSubmarine/Video/Server/GStreamer/Source.h"

namespace PiSubmarine::Video::Server::GStreamer
{
    struct AutoDetectPipelineHead
    {
        Source VideoSource{AutoDetectSource{}};

        [[nodiscard]] bool operator==(const AutoDetectPipelineHead&) const = default;
    };

    struct ExternalProcessHead
    {
        std::string Executable;
        std::vector<std::string> Arguments;

        [[nodiscard]] bool operator==(const ExternalProcessHead&) const = default;
    };

    using PipelineHead = std::variant<AutoDetectPipelineHead, ExternalProcessHead>;
}
