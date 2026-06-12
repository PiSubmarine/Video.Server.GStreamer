#pragma once

#include <memory>
#include <optional>
#include <string>

#include <gst/gst.h>

#include "PiSubmarine/Video/Server/GStreamer/Pipeline.h"

namespace PiSubmarine::Video::Server::GStreamer
{
    class GstreamerPipeline final : public IPipeline
    {
    public:
        GstreamerPipeline(Config config, std::shared_ptr<spdlog::logger> logger);
        ~GstreamerPipeline() override;

        [[nodiscard]] Error::Api::Result<void> Apply(const PipelineState& state) override;
        [[nodiscard]] Error::Api::Result<void> Stop() override;
        void PollBus() override;
        [[nodiscard]] bool IsRunning() const noexcept override;

    private:
        struct GstObjectDeleter
        {
            void operator()(GstObject* object) const noexcept;
        };

        struct GstElementDeleter
        {
            void operator()(GstElement* element) const noexcept;
        };

        using GstObjectPtr = std::unique_ptr<GstObject, GstObjectDeleter>;
        using GstElementPtr = std::unique_ptr<GstElement, GstElementDeleter>;

        [[nodiscard]] static bool InitializeGstreamer();
        [[nodiscard]] static bool HasFactory(const char* name);
        [[nodiscard]] static std::string BuildSourceDescription(const Source& source);
        [[nodiscard]] static std::string BuildEncoderDescription(Control::Video::Api::StreamProfile profile);
        [[nodiscard]] static std::string BuildPipelineDescription(const PipelineState& state);
        void ApplyEndpoints(const std::vector<Subscription::Api::Endpoint>& endpoints);
        void DrainBusMessages();

        Config m_Config;
        std::shared_ptr<spdlog::logger> m_Logger;
        GstElementPtr m_Pipeline;
        GstElement* m_MultiSink = nullptr;
        std::vector<Subscription::Api::Endpoint> m_Endpoints;
        std::optional<PipelineState> m_LastState;
    };
}
