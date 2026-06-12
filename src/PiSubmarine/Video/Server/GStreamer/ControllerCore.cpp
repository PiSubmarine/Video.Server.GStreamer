#include "PiSubmarine/Video/Server/GStreamer/ControllerCore.h"

#include <algorithm>
#include <stdexcept>
#include <string_view>

#include <spdlog/spdlog.h>

#include "PiSubmarine/Error/Api/MakeError.h"
#include "PiSubmarine/Lease/Api/ErrorCode.h"

namespace PiSubmarine::Video::Server::GStreamer
{
    namespace
    {
        constexpr std::chrono::milliseconds LeaseDuration{3000};
        constexpr std::chrono::milliseconds RetryInterval{1000};

        [[nodiscard]] std::shared_ptr<spdlog::logger> CreateLogger(Logging::Api::IFactory& loggerFactory)
        {
            auto logger = loggerFactory.CreateLogger("Video.Server.GStreamer");
            if (!logger)
            {
                throw std::invalid_argument("Video.Server.GStreamer requires a logger factory that returns a logger");
            }

            return logger;
        }

        [[nodiscard]] bool IsAlreadyRegisteredError(const Error::Api::Error& error)
        {
            return std::string_view(error.Cause.category().name()) == "PiSubmarine.Lease.Api" &&
                   error.Cause == Lease::Api::make_error_code(Lease::Api::ErrorCode::ResourceAlreadyRegistered);
        }
    }

    ControllerCore::ControllerCore(
        Config config,
        Logging::Api::IFactory& loggerFactory,
        Lease::Api::IResourceRegistry& resourceRegistry,
        const Lease::Api::ILeaseValidator& leaseValidator,
        std::unique_ptr<IPipeline> pipeline)
        : m_Config(std::move(config))
        , m_Logger(CreateLogger(loggerFactory))
        , m_ResourceRegistry(resourceRegistry)
        , m_LeaseValidator(leaseValidator)
        , m_Pipeline(std::move(pipeline))
    {
        RegisterResource();
    }

    Error::Api::Result<void> ControllerCore::SetTarget(const Control::Video::Api::Command& target)
    {
        if (m_Target == target)
        {
            return {};
        }

        if (const auto* enabledState = target.TryGetEnabled();
            enabledState != nullptr &&
            std::holds_alternative<Control::Video::Api::ManualFocus>(enabledState->Focus))
        {
            SPDLOG_LOGGER_WARN(
                m_Logger,
                "Manual focus was requested, but generic GStreamer camera control does not currently apply focus settings");
        }

        m_Target = target;
        m_IsDirty = true;
        return {};
    }

    Error::Api::Result<void> ControllerCore::Subscribe(const Subscription::Api::SubscribeRequest& request)
    {
        if (const auto validationResult = ValidateLeaseForSubscription(request.LeaseId); !validationResult)
        {
            return std::unexpected(validationResult.error());
        }

        m_Subscribers[request.LeaseId.Value] = Subscriber{
            .LeaseId = request.LeaseId,
            .Endpoint = request.Endpoint};
        m_IsDirty = true;

        SPDLOG_LOGGER_INFO(
            m_Logger,
            "Registered video subscriber for lease '{}' at '{}:{}'",
            request.LeaseId.Value,
            request.Endpoint.Host,
            request.Endpoint.Port);
        return {};
    }

    Error::Api::Result<void> ControllerCore::Unsubscribe(const Subscription::Api::UnsubscribeRequest& request)
    {
        if (m_Subscribers.erase(request.LeaseId.Value) > 0)
        {
            SPDLOG_LOGGER_INFO(m_Logger, "Removed video subscriber for lease '{}'", request.LeaseId.Value);
            m_IsDirty = true;
        }

        return {};
    }

    void ControllerCore::Tick(const std::chrono::nanoseconds& uptime, const std::chrono::nanoseconds& deltaTime)
    {
        static_cast<void>(deltaTime);

        m_Pipeline->PollBus();
        RemoveExpiredSubscribers();
        ReconcilePipeline(uptime);
    }

    Lease::Api::LeasePolicy ControllerCore::MakeLeasePolicy()
    {
        return Lease::Api::LeasePolicy{
            .MaxLeases = std::nullopt,
            .LeaseDuration = LeaseDuration};
    }

    void ControllerCore::RegisterResource()
    {
        const Lease::Api::ResourceDescriptor expectedDescriptor{
            .Id = m_Config.ResourceId,
            .Policy = MakeLeasePolicy()};

        const auto registerResult = m_ResourceRegistry.RegisterResource(expectedDescriptor);
        if (registerResult.has_value())
        {
            return;
        }

        if (!IsAlreadyRegisteredError(registerResult.error()))
        {
            throw std::runtime_error("Failed to register video lease resource");
        }

        const auto existingResourceResult = m_ResourceRegistry.GetResource(m_Config.ResourceId);
        if (!existingResourceResult.has_value() || existingResourceResult.value() != expectedDescriptor)
        {
            throw std::runtime_error("Video lease resource already exists with incompatible configuration");
        }
    }

    void ControllerCore::RemoveExpiredSubscribers()
    {
        for (auto iterator = m_Subscribers.begin(); iterator != m_Subscribers.end();)
        {
            const auto validationResult = m_LeaseValidator.ValidateLease(iterator->second.LeaseId, m_Config.ResourceId);
            if (!validationResult.has_value() || !validationResult->IsValid)
            {
                SPDLOG_LOGGER_INFO(
                    m_Logger,
                    "Removing expired or invalid video subscriber for lease '{}'",
                    iterator->second.LeaseId.Value);
                iterator = m_Subscribers.erase(iterator);
                m_IsDirty = true;
                continue;
            }

            ++iterator;
        }
    }

    void ControllerCore::ReconcilePipeline(const std::chrono::nanoseconds& uptime)
    {
        const auto shouldRun = ShouldRun();
        if (!shouldRun)
        {
            if (m_Pipeline->IsRunning())
            {
                const auto stopResult = m_Pipeline->Stop();
                if (!stopResult.has_value())
                {
                    SPDLOG_LOGGER_WARN(m_Logger, "Failed to stop video pipeline: {}", stopResult.error().Cause.message());
                }
            }

            m_LastAppliedState.reset();
            return;
        }

        if (uptime < m_NextRetryTime)
        {
            return;
        }

        const PipelineState state{
            .Config = m_Config,
            .Command = m_Target,
            .Endpoints = CollectEndpoints()};

        if (!m_IsDirty && m_LastAppliedState.has_value() && *m_LastAppliedState == state)
        {
            return;
        }

        const auto applyResult = m_Pipeline->Apply(state);
        if (!applyResult.has_value())
        {
            SPDLOG_LOGGER_WARN(m_Logger, "Failed to apply video pipeline state: {}", applyResult.error().Cause.message());
            m_NextRetryTime = uptime + std::chrono::duration_cast<std::chrono::nanoseconds>(RetryInterval);
            return;
        }

        m_LastAppliedState = state;
        m_IsDirty = false;
        m_NextRetryTime = uptime;
    }

    bool ControllerCore::ShouldRun() const noexcept
    {
        return m_Target.IsEnabled() && !m_Subscribers.empty();
    }

    std::vector<Subscription::Api::Endpoint> ControllerCore::CollectEndpoints() const
    {
        std::vector<Subscription::Api::Endpoint> endpoints;
        endpoints.reserve(m_Subscribers.size());
        for (const auto& [leaseId, subscriber] : m_Subscribers)
        {
            static_cast<void>(leaseId);
            endpoints.push_back(subscriber.Endpoint);
        }

        std::ranges::sort(endpoints, [](const auto& left, const auto& right)
        {
            if (left.Host != right.Host)
            {
                return left.Host < right.Host;
            }

            return left.Port < right.Port;
        });
        return endpoints;
    }

    Error::Api::Result<void> ControllerCore::ValidateLeaseForSubscription(const Lease::Api::LeaseId& leaseId) const
    {
        const auto validationResult = m_LeaseValidator.ValidateLease(leaseId, m_Config.ResourceId);
        if (!validationResult.has_value())
        {
            return std::unexpected(validationResult.error());
        }

        if (!validationResult->IsValid)
        {
            return std::unexpected(Error::Api::MakeError(Error::Api::ErrorCondition::ContractError));
        }

        return {};
    }
}
