#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <unordered_map>
#include <PiSubmarine/Video/Subscription/Api/SubscribeRequest.h>
#include <PiSubmarine/Video/Subscription/Api/UnsubscribeRequest.h>

#include "PiSubmarine/Lease/Api/ILeaseValidator.h"
#include "PiSubmarine/Lease/Api/IResourceRegistry.h"
#include "PiSubmarine/Logging/Api/IFactory.h"
#include "PiSubmarine/Video/Server/GStreamer/Pipeline.h"
#include "PiSubmarine/Video/Telemetry/Api/Status.h"

namespace PiSubmarine::Video::Server::GStreamer
{
    class ControllerCore final
    {
    public:
        ControllerCore(
            Config config,
            Logging::Api::IFactory& loggerFactory,
            Lease::Api::IResourceRegistry& resourceRegistry,
            const Lease::Api::ILeaseValidator& leaseValidator,
            std::unique_ptr<IPipeline> pipeline);

        [[nodiscard]] Error::Api::Result<void> SetTarget(const Control::Video::Api::Command& target);
        [[nodiscard]] Error::Api::Result<::PiSubmarine::Video::Telemetry::Api::Status> GetStatus() const;
        [[nodiscard]] Error::Api::Result<void> Subscribe(const Subscription::Api::SubscribeRequest& request);
        [[nodiscard]] Error::Api::Result<void> Unsubscribe(const Subscription::Api::UnsubscribeRequest& request);
        void Tick(const std::chrono::nanoseconds& uptime, const std::chrono::nanoseconds& deltaTime);

    private:
        struct Subscriber
        {
            Lease::Api::LeaseId LeaseId;
            Subscription::Api::Endpoint Endpoint;
        };

        [[nodiscard]] static Lease::Api::LeasePolicy MakeLeasePolicy();
        void RegisterResource();
        void RemoveExpiredSubscribers();
        void ReconcilePipeline(const std::chrono::nanoseconds& uptime);
        [[nodiscard]] bool ShouldRun() const noexcept;
        [[nodiscard]] std::vector<Subscription::Api::Endpoint> CollectEndpoints() const;
        [[nodiscard]] Error::Api::Result<void> ValidateLeaseForSubscription(const Lease::Api::LeaseId& leaseId) const;

        Config m_Config;
        std::shared_ptr<spdlog::logger> m_Logger;
        Lease::Api::IResourceRegistry& m_ResourceRegistry;
        const Lease::Api::ILeaseValidator& m_LeaseValidator;
        std::unique_ptr<IPipeline> m_Pipeline;
        std::unordered_map<std::string, Subscriber> m_Subscribers;
        Control::Video::Api::Command m_Target = Control::Video::Api::Command::Disable();
        std::optional<PipelineState> m_LastAppliedState;
        std::chrono::nanoseconds m_NextRetryTime{0};
        bool m_IsDirty = true;
    };
}
