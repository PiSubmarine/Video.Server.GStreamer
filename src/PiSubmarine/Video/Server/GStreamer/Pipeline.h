#pragma once

#include <memory>
#include <vector>

#include "PiSubmarine/Control/Video/Api/Command.h"
#include "PiSubmarine/Error/Api/Result.h"
#include "PiSubmarine/Logging/Api/IFactory.h"
#include "PiSubmarine/Video/Server/GStreamer/Config.h"
#include "PiSubmarine/Video/Subscription/Api/Endpoint.h"

namespace PiSubmarine::Video::Server::GStreamer
{
    struct PipelineState
    {
        Config Config;
        Control::Video::Api::Command Command;
        std::vector<Subscription::Api::Endpoint> Endpoints;

        [[nodiscard]] bool operator==(const PipelineState&) const = default;
    };

    class IPipeline
    {
    public:
        virtual ~IPipeline() = default;

        [[nodiscard]] virtual Error::Api::Result<void> Apply(const PipelineState& state) = 0;
        [[nodiscard]] virtual Error::Api::Result<void> Stop() = 0;
        virtual void PollBus() = 0;
        [[nodiscard]] virtual bool IsRunning() const noexcept = 0;
    };

    [[nodiscard]] std::unique_ptr<IPipeline> CreateGstreamerPipeline(
        const Config& config,
        Logging::Api::IFactory& loggerFactory);
}
