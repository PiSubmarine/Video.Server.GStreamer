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
        [[nodiscard]] ::PiSubmarine::Video::Telemetry::Api::Faults GetFaults() const noexcept override;

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

        [[nodiscard]] static bool InitializeGstreamer(const std::shared_ptr<spdlog::logger>& logger);
        static void LogRegistryDiagnostics(const std::shared_ptr<spdlog::logger>& logger);
        [[nodiscard]] static bool HasFactory(const char* name);
        [[nodiscard]] static std::string BuildSourceDescription(
            const Source& source,
            const std::shared_ptr<spdlog::logger>& logger);
        [[nodiscard]] static std::string BuildHeadDescription(
            const PipelineHead& head,
            Control::Video::Api::StreamProfile profile,
            const std::optional<int>& externalProcessReadFd,
            const std::shared_ptr<spdlog::logger>& logger);
        [[nodiscard]] static std::string BuildEncoderDescription(
            const std::string_view sourceDescription,
            Control::Video::Api::StreamProfile profile,
            const std::shared_ptr<spdlog::logger>& logger);
        [[nodiscard]] static std::string BuildPayloaderDescription();
        [[nodiscard]] static std::string BuildPipelineDescription(
            const PipelineState& state,
            const std::optional<int>& externalProcessReadFd,
            const std::shared_ptr<spdlog::logger>& logger);
        [[nodiscard]] Error::Api::Result<void> StartExternalProcess(const ExternalProcessHead& head);
        void StopExternalProcess() noexcept;
        void PollExternalProcess();
        void ApplyEndpoints(const std::vector<Subscription::Api::Endpoint>& endpoints);
        void DrainBusMessages();
        void SetFault(::PiSubmarine::Video::Telemetry::Api::Faults fault) noexcept;
        void ClearFaults() noexcept;
        [[nodiscard]] ::PiSubmarine::Video::Telemetry::Api::Faults ClassifyApplyFailure(const std::string_view diagnostic) const noexcept;
        [[nodiscard]] static ::PiSubmarine::Video::Telemetry::Api::Faults ClassifyBusError(const GError* error) noexcept;

        Config m_Config;
        std::shared_ptr<spdlog::logger> m_Logger;
        GstElementPtr m_Pipeline;
        GstElement* m_MultiSink = nullptr;
        std::vector<Subscription::Api::Endpoint> m_Endpoints;
        std::optional<PipelineState> m_LastState;
        ::PiSubmarine::Video::Telemetry::Api::Faults m_ActiveFaults{};
        std::optional<int> m_ExternalProcessReadFd;
#ifdef _WIN32
        void* m_ExternalProcessHandle = nullptr;
#else
        int m_ExternalProcessId = -1;
#endif
    };
}
