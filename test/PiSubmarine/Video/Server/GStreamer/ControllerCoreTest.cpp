#include <gtest/gtest.h>
#include <memory>
#include <spdlog/logger.h>
#include <spdlog/sinks/null_sink.h>

#include "PiSubmarine/Lease/Api/ILeaseValidatorMock.h"
#include "PiSubmarine/Lease/Api/IResourceRegistryMock.h"
#include "PiSubmarine/Logging/Api/IFactory.h"
#include "PiSubmarine/Video/Server/GStreamer/ControllerCore.h"

namespace PiSubmarine::Video::Server::GStreamer
{
    namespace
    {
        class LoggerFactoryStub final : public Logging::Api::IFactory
        {
        public:
            [[nodiscard]] std::shared_ptr<spdlog::logger> CreateLogger(std::string_view name) override
            {
                return std::make_shared<spdlog::logger>(
                    std::string(name),
                    std::make_shared<spdlog::sinks::null_sink_mt>());
            }
        };

        class FakePipeline final : public IPipeline
        {
        public:
            Error::Api::Result<void> Apply(const PipelineState& state) override
            {
                AppliedStates.push_back(state);
                Running = true;
                return {};
            }

            Error::Api::Result<void> Stop() override
            {
                Running = false;
                ++StopCount;
                return {};
            }

            void PollBus() override
            {
            }

            [[nodiscard]] bool IsRunning() const noexcept override
            {
                return Running;
            }

            std::vector<PipelineState> AppliedStates;
            bool Running = false;
            int StopCount = 0;
        };
    }

    TEST(ControllerCoreTest, RegistersDefaultLeaseResource)
    {
        LoggerFactoryStub loggerFactory;
        testing::StrictMock<Lease::Api::IResourceRegistryMock> resourceRegistry;
        testing::StrictMock<Lease::Api::ILeaseValidatorMock> leaseValidator;

        EXPECT_CALL(resourceRegistry, RegisterResource(Lease::Api::ResourceDescriptor{
                        .Id = Lease::Api::ResourceId{.Value = "video-main"},
                        .Policy = Lease::Api::LeasePolicy{
                            .MaxLeases = std::nullopt,
                            .LeaseDuration = std::chrono::milliseconds(3000)}}))
            .WillOnce(testing::Return(Error::Api::Result<void>{}));

        auto pipeline = std::make_unique<FakePipeline>();
        ControllerCore controller(Config{}, loggerFactory, resourceRegistry, leaseValidator, std::move(pipeline));
    }

    TEST(ControllerCoreTest, DoesNotStartPipelineWhenOperatorDisabled)
    {
        LoggerFactoryStub loggerFactory;
        testing::StrictMock<Lease::Api::IResourceRegistryMock> resourceRegistry;
        testing::StrictMock<Lease::Api::ILeaseValidatorMock> leaseValidator;

        EXPECT_CALL(resourceRegistry, RegisterResource(testing::_))
            .WillOnce(testing::Return(Error::Api::Result<void>{}));
        EXPECT_CALL(leaseValidator, ValidateLease(
                        Lease::Api::LeaseId{.Value = "lease-1"},
                        Lease::Api::ResourceId{.Value = "video-main"}))
            .WillOnce(testing::Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})))
            .WillOnce(testing::Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})));

        auto pipeline = std::make_unique<FakePipeline>();
        auto* pipelinePtr = pipeline.get();
        ControllerCore controller(Config{}, loggerFactory, resourceRegistry, leaseValidator, std::move(pipeline));

        ASSERT_TRUE(controller.Subscribe(Subscription::Api::SubscribeRequest{
            .LeaseId = Lease::Api::LeaseId{.Value = "lease-1"},
            .Endpoint = Subscription::Api::Endpoint{.Host = "127.0.0.1", .Port = 5004}}).has_value());

        controller.Tick(std::chrono::seconds(1), std::chrono::milliseconds(10));
        EXPECT_TRUE(pipelinePtr->AppliedStates.empty());
    }

    TEST(ControllerCoreTest, StartsPipelineWhenOperatorEnablesAndSubscriberExists)
    {
        LoggerFactoryStub loggerFactory;
        testing::StrictMock<Lease::Api::IResourceRegistryMock> resourceRegistry;
        testing::StrictMock<Lease::Api::ILeaseValidatorMock> leaseValidator;

        EXPECT_CALL(resourceRegistry, RegisterResource(testing::_))
            .WillOnce(testing::Return(Error::Api::Result<void>{}));
        EXPECT_CALL(leaseValidator, ValidateLease(
                        Lease::Api::LeaseId{.Value = "lease-1"},
                        Lease::Api::ResourceId{.Value = "video-main"}))
            .WillOnce(testing::Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})))
            .WillOnce(testing::Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})));

        auto pipeline = std::make_unique<FakePipeline>();
        auto* pipelinePtr = pipeline.get();
        ControllerCore controller(Config{}, loggerFactory, resourceRegistry, leaseValidator, std::move(pipeline));

        ASSERT_TRUE(controller.SetTarget(Control::Video::Api::Command::Enable(
            Control::Video::Api::StreamProfile::LowLatency,
            Control::Video::Api::AutoFocus{})).has_value());
        ASSERT_TRUE(controller.Subscribe(Subscription::Api::SubscribeRequest{
            .LeaseId = Lease::Api::LeaseId{.Value = "lease-1"},
            .Endpoint = Subscription::Api::Endpoint{.Host = "127.0.0.1", .Port = 5004}}).has_value());

        controller.Tick(std::chrono::seconds(1), std::chrono::milliseconds(10));
        ASSERT_EQ(pipelinePtr->AppliedStates.size(), 1);
        EXPECT_EQ(pipelinePtr->AppliedStates.front().Endpoints.size(), 1);
    }

    TEST(ControllerCoreTest, StopsPipelineWhenLastSubscriberExpires)
    {
        LoggerFactoryStub loggerFactory;
        testing::StrictMock<Lease::Api::IResourceRegistryMock> resourceRegistry;
        testing::StrictMock<Lease::Api::ILeaseValidatorMock> leaseValidator;

        EXPECT_CALL(resourceRegistry, RegisterResource(testing::_))
            .WillOnce(testing::Return(Error::Api::Result<void>{}));
        EXPECT_CALL(leaseValidator, ValidateLease(
                        Lease::Api::LeaseId{.Value = "lease-1"},
                        Lease::Api::ResourceId{.Value = "video-main"}))
            .WillOnce(testing::Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})))
            .WillOnce(testing::Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})))
            .WillOnce(testing::Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = false})));

        auto pipeline = std::make_unique<FakePipeline>();
        auto* pipelinePtr = pipeline.get();
        ControllerCore controller(Config{}, loggerFactory, resourceRegistry, leaseValidator, std::move(pipeline));

        ASSERT_TRUE(controller.SetTarget(Control::Video::Api::Command::Enable(
            Control::Video::Api::StreamProfile::LowLatency,
            Control::Video::Api::AutoFocus{})).has_value());
        ASSERT_TRUE(controller.Subscribe(Subscription::Api::SubscribeRequest{
            .LeaseId = Lease::Api::LeaseId{.Value = "lease-1"},
            .Endpoint = Subscription::Api::Endpoint{.Host = "127.0.0.1", .Port = 5004}}).has_value());

        controller.Tick(std::chrono::seconds(1), std::chrono::milliseconds(10));
        controller.Tick(std::chrono::seconds(2), std::chrono::milliseconds(10));

        EXPECT_EQ(pipelinePtr->StopCount, 1);
    }
}
