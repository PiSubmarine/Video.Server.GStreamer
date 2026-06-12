#pragma once

#include <string>
#include <variant>
#include <vector>

namespace PiSubmarine::Video::Server::GStreamer
{
    struct AutoDetectSource
    {
        std::vector<std::string> PreferredElementNames;

        [[nodiscard]] bool operator==(const AutoDetectSource&) const = default;
    };

    struct ElementSource
    {
        std::string Description;

        [[nodiscard]] bool operator==(const ElementSource&) const = default;
    };

    using Source = std::variant<AutoDetectSource, ElementSource>;
}
