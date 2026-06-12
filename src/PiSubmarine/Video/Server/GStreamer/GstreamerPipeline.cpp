#include "PiSubmarine/Video/Server/GStreamer/GstreamerPipeline.h"

#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>

#include <spdlog/spdlog.h>

#include "PiSubmarine/Error/Api/MakeError.h"

namespace PiSubmarine::Video::Server::GStreamer
{
    namespace
    {
        std::once_flag GstreamerInitFlag;
        std::once_flag GstreamerDebugFlag;
        std::shared_ptr<spdlog::logger> GstreamerDebugLogger;

        [[nodiscard]] Error::Api::Error MakePipelineError()
        {
            return Error::Api::MakeError(Error::Api::ErrorCondition::DeviceError);
        }

        void GstDebugLogCallback(
            GstDebugCategory* category,
            GstDebugLevel level,
            const gchar* file,
            const gchar* function,
            gint line,
            GObject* object,
            GstDebugMessage* message,
            gpointer userData)
        {
            static_cast<void>(object);
            static_cast<void>(userData);

            if (!GstreamerDebugLogger)
            {
                return;
            }

            const auto payload =
                std::string("[") + gst_debug_category_get_name(category) + "] " +
                (file != nullptr ? file : "unknown") + ":" + std::to_string(line) + " " +
                (function != nullptr ? function : "unknown") + " - " +
                gst_debug_message_get(message);

            switch (level)
            {
            case GST_LEVEL_ERROR:
                SPDLOG_LOGGER_ERROR(GstreamerDebugLogger, "{}", payload);
                break;
            case GST_LEVEL_WARNING:
                SPDLOG_LOGGER_WARN(GstreamerDebugLogger, "{}", payload);
                break;
            case GST_LEVEL_INFO:
                SPDLOG_LOGGER_INFO(GstreamerDebugLogger, "{}", payload);
                break;
            default:
                SPDLOG_LOGGER_DEBUG(GstreamerDebugLogger, "{}", payload);
                break;
            }
        }

        [[nodiscard]] std::string DefaultSourceDescription(const std::string_view elementName)
        {
            if (elementName == "libcamerasrc")
            {
                return "libcamerasrc";
            }

            if (elementName == "v4l2src")
            {
                return "v4l2src do-timestamp=true";
            }

            if (elementName == "mfvideosrc")
            {
                return "mfvideosrc";
            }

            if (elementName == "ksvideosrc")
            {
                return "ksvideosrc";
            }

            if (elementName == "autovideosrc")
            {
                return "autovideosrc";
            }

            return std::string(elementName);
        }
    }

    std::unique_ptr<IPipeline> CreateGstreamerPipeline(
        const Config& config,
        Logging::Api::IFactory& loggerFactory)
    {
        auto logger = loggerFactory.CreateLogger("Video.Server.GStreamer.Gst");
        if (!logger)
        {
            throw std::invalid_argument("Video.Server.GStreamer requires a GStreamer logger");
        }

        return std::make_unique<GstreamerPipeline>(config, std::move(logger));
    }

    GstreamerPipeline::GstreamerPipeline(Config config, std::shared_ptr<spdlog::logger> logger)
        : m_Config(std::move(config))
        , m_Logger(std::move(logger))
    {
        if (!InitializeGstreamer())
        {
            throw std::runtime_error("Failed to initialize GStreamer");
        }

        std::call_once(GstreamerDebugFlag, [this]
        {
            GstreamerDebugLogger = m_Logger;
            gst_debug_add_log_function(&GstDebugLogCallback, nullptr, nullptr);
        });
    }

    GstreamerPipeline::~GstreamerPipeline()
    {
        static_cast<void>(Stop());
    }

    Error::Api::Result<void> GstreamerPipeline::Apply(const PipelineState& state)
    {
        if (m_LastState.has_value() &&
            m_Pipeline &&
            (m_LastState->Configuration != state.Configuration || m_LastState->Command != state.Command))
        {
            const auto stopResult = Stop();
            if (!stopResult.has_value())
            {
                return stopResult;
            }
        }

        std::string description;
        try
        {
            description = BuildPipelineDescription(state);
        }
        catch (const std::exception& exception)
        {
            SPDLOG_LOGGER_ERROR(m_Logger, "Failed to prepare GStreamer pipeline description: {}", exception.what());
            return std::unexpected(MakePipelineError());
        }

        if (!m_Pipeline)
        {
            GError* error = nullptr;
            auto* pipeline = gst_parse_launch(description.c_str(), &error);
            if (!pipeline)
            {
                if (error != nullptr)
                {
                    SPDLOG_LOGGER_ERROR(m_Logger, "Failed to create GStreamer pipeline: {}", error->message);
                    g_error_free(error);
                }

                return std::unexpected(MakePipelineError());
            }

            m_Pipeline.reset(GST_ELEMENT(pipeline));
            m_MultiSink = gst_bin_get_by_name(GST_BIN(m_Pipeline.get()), "subscription_sink");
            if (!m_MultiSink)
            {
                SPDLOG_LOGGER_ERROR(m_Logger, "Failed to find multiudpsink in GStreamer pipeline");
                m_Pipeline.reset();
                return std::unexpected(MakePipelineError());
            }

            ApplyEndpoints(state.Endpoints);

            if (gst_element_set_state(m_Pipeline.get(), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
            {
                SPDLOG_LOGGER_ERROR(m_Logger, "Failed to start GStreamer pipeline");
                m_MultiSink = nullptr;
                m_Pipeline.reset();
                return std::unexpected(MakePipelineError());
            }

            m_LastState = state;
            SPDLOG_LOGGER_INFO(m_Logger, "Started GStreamer video pipeline");
            return {};
        }

        if (m_Endpoints != state.Endpoints)
        {
            ApplyEndpoints(state.Endpoints);
        }

        m_LastState = state;
        return {};
    }

    Error::Api::Result<void> GstreamerPipeline::Stop()
    {
        if (!m_Pipeline)
        {
            return {};
        }

        gst_element_set_state(m_Pipeline.get(), GST_STATE_NULL);
        if (m_MultiSink != nullptr)
        {
            gst_object_unref(GST_OBJECT(m_MultiSink));
            m_MultiSink = nullptr;
        }

        m_Endpoints.clear();
        m_LastState.reset();
        m_Pipeline.reset();
        SPDLOG_LOGGER_INFO(m_Logger, "Stopped GStreamer video pipeline");
        return {};
    }

    void GstreamerPipeline::PollBus()
    {
        DrainBusMessages();
    }

    bool GstreamerPipeline::IsRunning() const noexcept
    {
        return static_cast<bool>(m_Pipeline);
    }

    void GstreamerPipeline::GstObjectDeleter::operator()(GstObject* object) const noexcept
    {
        if (object != nullptr)
        {
            gst_object_unref(object);
        }
    }

    void GstreamerPipeline::GstElementDeleter::operator()(GstElement* element) const noexcept
    {
        if (element != nullptr)
        {
            gst_object_unref(GST_OBJECT(element));
        }
    }

    bool GstreamerPipeline::InitializeGstreamer()
    {
        bool initialized = true;
        std::call_once(GstreamerInitFlag, [&initialized]
        {
            GError* error = nullptr;
            initialized = gst_init_check(nullptr, nullptr, &error);
            if (!initialized && error != nullptr)
            {
                g_error_free(error);
            }
        });

        return initialized;
    }

    bool GstreamerPipeline::HasFactory(const char* name)
    {
        auto* factory = gst_element_factory_find(name);
        if (!factory)
        {
            return false;
        }

        gst_object_unref(factory);
        return true;
    }

    std::string GstreamerPipeline::BuildSourceDescription(const Source& source)
    {
        if (const auto* elementSource = std::get_if<ElementSource>(&source))
        {
            return elementSource->Description;
        }

        const auto* autoDetect = std::get_if<AutoDetectSource>(&source);
        const auto candidates = autoDetect->PreferredElementNames.empty()
            ? std::vector<std::string>{"libcamerasrc", "v4l2src", "mfvideosrc", "ksvideosrc", "autovideosrc"}
            : autoDetect->PreferredElementNames;

        for (const auto& candidate : candidates)
        {
            if (HasFactory(candidate.c_str()))
            {
                return DefaultSourceDescription(candidate);
            }
        }

        throw std::runtime_error("No suitable GStreamer video source element was found");
    }

    std::string GstreamerPipeline::BuildEncoderDescription(const Control::Video::Api::StreamProfile profile)
    {
        const auto x264Description = [profile]() -> std::string
        {
            switch (profile)
            {
            case Control::Video::Api::StreamProfile::LowLatency:
                return "x264enc tune=zerolatency speed-preset=ultrafast bitrate=1000 key-int-max=15";
            case Control::Video::Api::StreamProfile::Standard:
                return "x264enc tune=zerolatency speed-preset=veryfast bitrate=2500 key-int-max=30";
            case Control::Video::Api::StreamProfile::HighQuality:
                return "x264enc tune=zerolatency speed-preset=fast bitrate=5000 key-int-max=60";
            }

            return "x264enc tune=zerolatency speed-preset=veryfast bitrate=2500 key-int-max=30";
        };

        const auto openH264Description = [profile]() -> std::string
        {
            switch (profile)
            {
            case Control::Video::Api::StreamProfile::LowLatency:
                return "openh264enc bitrate=1000000 complexity=low";
            case Control::Video::Api::StreamProfile::Standard:
                return "openh264enc bitrate=2500000 complexity=medium";
            case Control::Video::Api::StreamProfile::HighQuality:
                return "openh264enc bitrate=5000000 complexity=high";
            }

            return "openh264enc bitrate=2500000 complexity=medium";
        };

        if (HasFactory("x264enc"))
        {
            return x264Description();
        }

        if (HasFactory("openh264enc"))
        {
            return openH264Description();
        }

        throw std::runtime_error("No suitable GStreamer H.264 encoder element was found");
    }

    std::string GstreamerPipeline::BuildPipelineDescription(const PipelineState& state)
    {
        const auto* enabledState = state.Command.TryGetEnabled();
        if (enabledState == nullptr)
        {
            throw std::runtime_error("Cannot build a running pipeline for a disabled video command");
        }

        return std::format(
            "{} ! queue leaky=downstream max-size-buffers=1 ! videoconvert ! {} ! rtph264pay pt=96 config-interval=1 ! "
            "multiudpsink name=subscription_sink sync=false async=false",
            BuildSourceDescription(state.Configuration.VideoSource),
            BuildEncoderDescription(enabledState->Profile));
    }

    void GstreamerPipeline::ApplyEndpoints(const std::vector<Subscription::Api::Endpoint>& endpoints)
    {
        if (m_MultiSink == nullptr)
        {
            return;
        }

        for (const auto& endpoint : m_Endpoints)
        {
            g_signal_emit_by_name(
                m_MultiSink,
                "remove",
                endpoint.Host.c_str(),
                static_cast<gint>(endpoint.Port));
        }

        for (const auto& endpoint : endpoints)
        {
            g_signal_emit_by_name(
                m_MultiSink,
                "add",
                endpoint.Host.c_str(),
                static_cast<gint>(endpoint.Port));
        }

        m_Endpoints = endpoints;
    }

    void GstreamerPipeline::DrainBusMessages()
    {
        if (!m_Pipeline)
        {
            return;
        }

        GstObjectPtr bus(GST_OBJECT(gst_element_get_bus(m_Pipeline.get())));
        if (!bus)
        {
            return;
        }

        while (auto* message = gst_bus_pop(GST_BUS(bus.get())))
        {
            switch (GST_MESSAGE_TYPE(message))
            {
            case GST_MESSAGE_ERROR:
            {
                GError* error = nullptr;
                gchar* debug = nullptr;
                gst_message_parse_error(message, &error, &debug);
                SPDLOG_LOGGER_ERROR(
                    m_Logger,
                    "GStreamer pipeline error: {} ({})",
                    error != nullptr ? error->message : "unknown",
                    debug != nullptr ? debug : "no debug info");
                if (error != nullptr)
                {
                    g_error_free(error);
                }

                if (debug != nullptr)
                {
                    g_free(debug);
                }

                break;
            }
            case GST_MESSAGE_WARNING:
            {
                GError* error = nullptr;
                gchar* debug = nullptr;
                gst_message_parse_warning(message, &error, &debug);
                SPDLOG_LOGGER_WARN(
                    m_Logger,
                    "GStreamer pipeline warning: {} ({})",
                    error != nullptr ? error->message : "unknown",
                    debug != nullptr ? debug : "no debug info");
                if (error != nullptr)
                {
                    g_error_free(error);
                }

                if (debug != nullptr)
                {
                    g_free(debug);
                }

                break;
            }
            case GST_MESSAGE_INFO:
            {
                GError* error = nullptr;
                gchar* debug = nullptr;
                gst_message_parse_info(message, &error, &debug);
                SPDLOG_LOGGER_INFO(
                    m_Logger,
                    "GStreamer pipeline info: {} ({})",
                    error != nullptr ? error->message : "unknown",
                    debug != nullptr ? debug : "no debug info");
                if (error != nullptr)
                {
                    g_error_free(error);
                }

                if (debug != nullptr)
                {
                    g_free(debug);
                }

                break;
            }
            default:
                break;
            }

            gst_message_unref(message);
        }
    }
}
