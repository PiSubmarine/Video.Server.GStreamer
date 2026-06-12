#pragma once

#include <chrono>
#include <memory>

#include "PiSubmarine/Control/Video/Api/IController.h"
#include "PiSubmarine/Lease/Api/ILeaseValidator.h"
#include "PiSubmarine/Lease/Api/IResourceRegistry.h"
#include "PiSubmarine/Logging/Api/IFactory.h"
#include "PiSubmarine/Time/ITickable.h"
#include "PiSubmarine/Video/Server/GStreamer/Config.h"
#include "PiSubmarine/Video/Subscription/Api/IService.h"

namespace PiSubmarine::Video::Server::GStreamer
{
    class Controller final
        : public Control::Video::Api::IController
        , public ::PiSubmarine::Video::Subscription::Api::IService
        , public Time::ITickable
    {
    public:
        Controller(
            Config config,
            Logging::Api::IFactory& loggerFactory,
            Lease::Api::IResourceRegistry& resourceRegistry,
            const Lease::Api::ILeaseValidator& leaseValidator);
        ~Controller() override;

        [[nodiscard]] Error::Api::Result<void> SetTarget(const Control::Video::Api::Command& target) override;
        [[nodiscard]] Error::Api::Result<void> Subscribe(
            const ::PiSubmarine::Video::Subscription::Api::SubscribeRequest& request) override;
        [[nodiscard]] Error::Api::Result<void> Unsubscribe(
            const ::PiSubmarine::Video::Subscription::Api::UnsubscribeRequest& request) override;
        void Tick(const std::chrono::nanoseconds& uptime, const std::chrono::nanoseconds& deltaTime) override;

    private:
        class Impl;

        std::unique_ptr<Impl> m_Impl;
    };
}
