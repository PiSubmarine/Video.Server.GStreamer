#include "PiSubmarine/Video/Server/GStreamer/Controller.h"

#include <utility>

#include "PiSubmarine/Video/Server/GStreamer/ControllerCore.h"

namespace PiSubmarine::Video::Server::GStreamer
{
    class Controller::Impl final
    {
    public:
        Impl(
            Config config,
            Logging::Api::IFactory& loggerFactory,
            Lease::Api::IResourceRegistry& resourceRegistry,
            const Lease::Api::ILeaseValidator& leaseValidator)
            : Core(CreateCore(std::move(config), loggerFactory, resourceRegistry, leaseValidator))
        {
        }

        static ControllerCore CreateCore(
            Config config,
            Logging::Api::IFactory& loggerFactory,
            Lease::Api::IResourceRegistry& resourceRegistry,
            const Lease::Api::ILeaseValidator& leaseValidator)
        {
            auto pipeline = CreateGstreamerPipeline(config, loggerFactory);
            return ControllerCore(std::move(config), loggerFactory, resourceRegistry, leaseValidator, std::move(pipeline));
        }

        ControllerCore Core;
    };

    Controller::Controller(
        Config config,
        Logging::Api::IFactory& loggerFactory,
        Lease::Api::IResourceRegistry& resourceRegistry,
        const Lease::Api::ILeaseValidator& leaseValidator)
        : m_Impl(std::make_unique<Impl>(std::move(config), loggerFactory, resourceRegistry, leaseValidator))
    {
    }

    Controller::~Controller() = default;

    Error::Api::Result<void> Controller::SetTarget(const Control::Video::Api::Command& target)
    {
        return m_Impl->Core.SetTarget(target);
    }

    Error::Api::Result<void> Controller::Subscribe(const Subscription::Api::SubscribeRequest& request)
    {
        return m_Impl->Core.Subscribe(request);
    }

    Error::Api::Result<void> Controller::Unsubscribe(const Subscription::Api::UnsubscribeRequest& request)
    {
        return m_Impl->Core.Unsubscribe(request);
    }

    void Controller::Tick(const std::chrono::nanoseconds& uptime, const std::chrono::nanoseconds& deltaTime)
    {
        m_Impl->Core.Tick(uptime, deltaTime);
    }
}
