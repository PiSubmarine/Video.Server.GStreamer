#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include <CLI/CLI.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "PiSubmarine/Error/Api/MakeError.h"
#include "PiSubmarine/Lease/Api/ErrorCode.h"
#include "PiSubmarine/Lease/Api/ILeaseValidator.h"
#include "PiSubmarine/Lease/Api/IResourceRegistry.h"
#include "PiSubmarine/Logging/Api/IFactory.h"
#include "PiSubmarine/Video/Server/GStreamer/Controller.h"
#include "PiSubmarine/Video/Server/GStreamer/Source.h"
#include "PiSubmarine/Video/Subscription/Api/SubscribeRequest.h"

namespace PiSubmarine::Video::Server::GStreamer
{
    namespace Detail
    {
        [[nodiscard]] std::shared_ptr<spdlog::logger> GetOrCreateAppLogger()
        {
            auto logger = spdlog::get("Video.Server.GStreamer.App");
            if (!logger)
            {
                logger = spdlog::stdout_color_mt("Video.Server.GStreamer.App");
                logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%-40n] [%-8l] %v");
                logger->set_level(spdlog::level::debug);
            }

            return logger;
        }

        std::atomic_bool KeepRunning = true;

        void HandleSignal(int)
        {
            KeepRunning = false;
        }

        class LoggerFactory final : public Logging::Api::IFactory
        {
        public:
            [[nodiscard]] std::shared_ptr<spdlog::logger> CreateLogger(std::string_view name) override
            {
                auto existingLogger = spdlog::get(std::string(name));
                if (existingLogger)
                {
                    return existingLogger;
                }

                auto logger = spdlog::stdout_color_mt(std::string(name));
                logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%-40n] [%-8l] %v");
                logger->set_level(spdlog::level::debug);
                return logger;
            }
        };

        class ResourceRegistry final : public Lease::Api::IResourceRegistry
        {
        public:
            [[nodiscard]] Error::Api::Result<void> RegisterResource(const Lease::Api::ResourceDescriptor& resource) override
            {
                const auto [iterator, inserted] = m_Resources.emplace(resource.Id.Value, resource);
                if (inserted || iterator->second == resource)
                {
                    return {};
                }

                return std::unexpected(Error::Api::MakeError(
                    Error::Api::ErrorCondition::ContractError,
                    Lease::Api::make_error_code(Lease::Api::ErrorCode::ResourceAlreadyRegistered)));
            }

            [[nodiscard]] Error::Api::Result<Lease::Api::ResourceDescriptor> GetResource(
                const Lease::Api::ResourceId& resourceId) const override
            {
                const auto iterator = m_Resources.find(resourceId.Value);
                if (iterator == m_Resources.end())
                {
                    return std::unexpected(Error::Api::MakeError(
                        Error::Api::ErrorCondition::ContractError,
                        Lease::Api::make_error_code(Lease::Api::ErrorCode::ResourceNotFound)));
                }

                return iterator->second;
            }

        private:
            std::unordered_map<std::string, Lease::Api::ResourceDescriptor> m_Resources;
        };

        class AlwaysTrueLeaseValidator final : public Lease::Api::ILeaseValidator
        {
        public:
            [[nodiscard]] Error::Api::Result<Lease::Api::LeaseValidation> ValidateLease(
                const Lease::Api::LeaseId& leaseId,
                const Lease::Api::ResourceId& resourceId) const override
            {
                static_cast<void>(leaseId);
                static_cast<void>(resourceId);
                return Lease::Api::LeaseValidation{.IsValid = true};
            }
        };

        [[nodiscard]] std::optional<Subscription::Api::Endpoint> ParseEndpoint(const std::string& value)
        {
            const auto separator = value.rfind(':');
            if (separator == std::string::npos)
            {
                return std::nullopt;
            }

            const auto host = value.substr(0, separator);
            if (host.empty())
            {
                return std::nullopt;
            }

            const auto portValue = value.substr(separator + 1);
            if (portValue.empty())
            {
                return std::nullopt;
            }

            char* parseEnd = nullptr;
            const auto parsedPort = std::strtoul(portValue.c_str(), &parseEnd, 10);
            if (parseEnd == nullptr || *parseEnd != '\0' || parsedPort > 65535)
            {
                return std::nullopt;
            }

            return Subscription::Api::Endpoint{
                .Host = host,
                .Port = static_cast<std::uint16_t>(parsedPort)};
        }

        [[nodiscard]] Control::Video::Api::StreamProfile ParseProfile(const std::string& value)
        {
            if (value == "low-latency")
            {
                return Control::Video::Api::StreamProfile::LowLatency;
            }

            if (value == "standard")
            {
                return Control::Video::Api::StreamProfile::Standard;
            }

            if (value == "high-quality")
            {
                return Control::Video::Api::StreamProfile::HighQuality;
            }

            throw std::invalid_argument("Unsupported profile value: " + value);
        }
    }
}

int main(int argc, char** argv)
{
    using namespace PiSubmarine;

    std::string endpoint = "127.0.0.1:5004";
    std::string profile = "low-latency";
    std::string sourceDescription;
    std::string leaseId = "test-lease";
    std::string resourceId = "video-main";
    int tickPeriodMilliseconds = 15;

    CLI::App app{"Minimal PiSubmarine.Video.Server.GStreamer pipeline runner"};
    app.add_option("--endpoint", endpoint, "Single RTP destination in host:port format")->default_val(endpoint);
    app.add_option("--profile", profile, "Video profile: low-latency, standard, high-quality")->default_val(profile);
    app.add_option("--source", sourceDescription, "Optional explicit GStreamer source description");
    app.add_option("--lease-id", leaseId, "Lease id used for the subscription")->default_val(leaseId);
    app.add_option("--resource-id", resourceId, "Resource id registered by the controller")->default_val(resourceId);
    app.add_option("--tick-period-ms", tickPeriodMilliseconds, "Tick period in milliseconds")
        ->default_val(tickPeriodMilliseconds);

    try
    {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError& error)
    {
        return app.exit(error);
    }

    try
    {
        const auto parsedEndpoint = Video::Server::GStreamer::Detail::ParseEndpoint(endpoint);
        if (!parsedEndpoint.has_value())
        {
            auto logger = Video::Server::GStreamer::Detail::GetOrCreateAppLogger();
            SPDLOG_LOGGER_ERROR(logger, "Invalid --endpoint value '{}'. Expected host:port.", endpoint);
            return 2;
        }

        Video::Server::GStreamer::Detail::LoggerFactory loggerFactory;
        auto logger = loggerFactory.CreateLogger("Video.Server.GStreamer.App");

        Video::Server::GStreamer::Detail::ResourceRegistry resourceRegistry;
        Video::Server::GStreamer::Detail::AlwaysTrueLeaseValidator leaseValidator;

        Video::Server::GStreamer::Config config;
        config.ResourceId = Lease::Api::ResourceId{.Value = resourceId};
        if (!sourceDescription.empty())
        {
            config.VideoSource = Video::Server::GStreamer::ElementSource{.Description = sourceDescription};
        }

        Video::Server::GStreamer::Controller controller(config, loggerFactory, resourceRegistry, leaseValidator);

        const auto subscribeResult = controller.Subscribe(Video::Subscription::Api::SubscribeRequest{
            .LeaseId = Lease::Api::LeaseId{.Value = leaseId},
            .ClientEndpoint = *parsedEndpoint});
        if (!subscribeResult.has_value())
        {
            SPDLOG_LOGGER_ERROR(logger, "Failed to subscribe endpoint: {}", subscribeResult.error().Cause.message());
            return 3;
        }

        const auto targetResult = controller.SetTarget(Control::Video::Api::Command::Enable(
            Video::Server::GStreamer::Detail::ParseProfile(profile),
            Control::Video::Api::AutoFocus{}));
        if (!targetResult.has_value())
        {
            SPDLOG_LOGGER_ERROR(logger, "Failed to enable video target: {}", targetResult.error().Cause.message());
            return 4;
        }

        SPDLOG_LOGGER_INFO(
            logger,
            "Streaming to {}:{} with lease '{}' and resource '{}'",
            parsedEndpoint->Host,
            parsedEndpoint->Port,
            leaseId,
            resourceId);

        std::signal(SIGINT, &Video::Server::GStreamer::Detail::HandleSignal);
        std::signal(SIGTERM, &Video::Server::GStreamer::Detail::HandleSignal);

        const auto startTime = std::chrono::steady_clock::now();
        auto previousTime = startTime;
        const auto tickPeriod = std::chrono::milliseconds(tickPeriodMilliseconds);

        while (Video::Server::GStreamer::Detail::KeepRunning)
        {
            const auto now = std::chrono::steady_clock::now();
            controller.Tick(
                std::chrono::duration_cast<std::chrono::nanoseconds>(now - startTime),
                std::chrono::duration_cast<std::chrono::nanoseconds>(now - previousTime));
            previousTime = now;
            std::this_thread::sleep_for(tickPeriod);
        }

        const auto disableResult = controller.SetTarget(Control::Video::Api::Command::Disable());
        if (!disableResult.has_value())
        {
            SPDLOG_LOGGER_WARN(logger, "Failed to disable video target cleanly: {}", disableResult.error().Cause.message());
        }

        controller.Tick(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - startTime),
            std::chrono::nanoseconds::zero());
    }
    catch (const std::exception& exception)
    {
        auto logger = Video::Server::GStreamer::Detail::GetOrCreateAppLogger();
        SPDLOG_LOGGER_ERROR(logger, "Application failed: {}", exception.what());
        return 5;
    }

    return 0;
}
