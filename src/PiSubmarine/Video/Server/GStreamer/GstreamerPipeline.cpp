#include "PiSubmarine/Video/Server/GStreamer/GstreamerPipeline.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef _WIN32
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

#include <spdlog/spdlog.h>

#include "PiSubmarine/Error/Api/MakeError.h"
#include "PiSubmarine/Gstreamer/Build/Plugins.h"

namespace PiSubmarine::Video::Server::GStreamer
{
    namespace
    {
        [[nodiscard]] constexpr ::PiSubmarine::Video::Telemetry::Api::Faults NoVideoFaults()
        {
            return static_cast<::PiSubmarine::Video::Telemetry::Api::Faults>(0);
        }

        std::once_flag GstreamerInitFlag;
        std::once_flag GstreamerStaticPluginRegistrationFlag;
        std::once_flag GstreamerDebugFlag;
        std::once_flag GstreamerRegistryDiagnosticsFlag;
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
                return "mfvideosrc ! video/x-raw,width=1280,height=720,framerate=30/1";
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

        [[nodiscard]] std::vector<std::string> CollectMatchingFactories(const std::string_view needle)
        {
            std::vector<std::string> names;

            auto* registry = gst_registry_get();
            if (registry == nullptr)
            {
                return names;
            }

            GList* factories = gst_registry_get_feature_list(registry, GST_TYPE_ELEMENT_FACTORY);
            for (GList* current = factories; current != nullptr; current = current->next)
            {
                auto* feature = GST_PLUGIN_FEATURE(current->data);
                if (feature == nullptr)
                {
                    continue;
                }

                const auto* name = gst_plugin_feature_get_name(feature);
                if (name == nullptr)
                {
                    continue;
                }

                const std::string factoryName(name);
                if (factoryName.contains(needle))
                {
                    names.push_back(factoryName);
                }
            }

            g_list_free_full(factories, reinterpret_cast<GDestroyNotify>(&gst_object_unref));
            std::ranges::sort(names);
            names.erase(std::unique(names.begin(), names.end()), names.end());
            return names;
        }

        [[nodiscard]] std::string JoinNames(const std::vector<std::string>& names)
        {
            if (names.empty())
            {
                return "<none>";
            }

            std::string result;
            for (std::size_t index = 0; index < names.size(); ++index)
            {
                if (index > 0)
                {
                    result += ", ";
                }

                result += names[index];
            }

            return result;
        }

        [[nodiscard]] bool ShouldPreferMediaFoundationEncoder(const std::string_view sourceDescription)
        {
            return sourceDescription.contains("mfvideosrc");
        }

        [[nodiscard]] std::string JoinCommandForLog(
            const std::string_view executable,
            const std::vector<std::string>& arguments)
        {
            std::string command(executable);
            for (const auto& argument : arguments)
            {
                command += " ";
                command += argument;
            }

            return command;
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
        if (!InitializeGstreamer(m_Logger))
        {
            throw std::runtime_error("Failed to initialize GStreamer");
        }

        std::call_once(GstreamerDebugFlag, [this]
        {
            GstreamerDebugLogger = m_Logger;
            gst_debug_add_log_function(&GstDebugLogCallback, nullptr, nullptr);
        });

        std::call_once(GstreamerRegistryDiagnosticsFlag, [this]
        {
            LogRegistryDiagnostics(m_Logger);
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
            if (const auto* externalProcessHead = std::get_if<ExternalProcessHead>(&state.Configuration.VideoHead))
            {
                const auto startProcessResult = StartExternalProcess(*externalProcessHead);
                if (!startProcessResult.has_value())
                {
                    return startProcessResult;
                }
            }

            description = BuildPipelineDescription(state, m_ExternalProcessReadFd, m_Logger);
        }
        catch (const std::exception& exception)
        {
            SPDLOG_LOGGER_ERROR(m_Logger, "Failed to prepare GStreamer pipeline description: {}", exception.what());
            SetFault(ClassifyApplyFailure(exception.what()));
            StopExternalProcess();
            return std::unexpected(MakePipelineError());
        }

        if (!m_Pipeline)
        {
            SPDLOG_LOGGER_INFO(m_Logger, "Using GStreamer pipeline: {}", description);
            GError* error = nullptr;
            auto* pipeline = gst_parse_launch(description.c_str(), &error);
            if (error != nullptr)
            {
                SPDLOG_LOGGER_ERROR(m_Logger, "Failed to create GStreamer pipeline: {}", error->message);
                SetFault(ClassifyApplyFailure(error->message != nullptr ? error->message : description));
                g_error_free(error);
                if (pipeline != nullptr)
                {
                    gst_object_unref(GST_OBJECT(pipeline));
                }

                StopExternalProcess();
                return std::unexpected(MakePipelineError());
            }

            if (!pipeline)
            {
                SetFault(ClassifyApplyFailure(description));
                StopExternalProcess();
                return std::unexpected(MakePipelineError());
            }

            m_Pipeline.reset(GST_ELEMENT(pipeline));
            m_MultiSink = gst_bin_get_by_name(GST_BIN(m_Pipeline.get()), "subscription_sink");
            if (!m_MultiSink)
            {
                SPDLOG_LOGGER_ERROR(m_Logger, "Failed to find multiudpsink in GStreamer pipeline");
                SetFault(::PiSubmarine::Video::Telemetry::Api::Faults::NetworkError);
                m_Pipeline.reset();
                StopExternalProcess();
                return std::unexpected(MakePipelineError());
            }

            ApplyEndpoints(state.Endpoints);

            if (gst_element_set_state(m_Pipeline.get(), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
            {
                SPDLOG_LOGGER_ERROR(m_Logger, "Failed to start GStreamer pipeline");
                SetFault(ClassifyApplyFailure(description));
                m_MultiSink = nullptr;
                m_Pipeline.reset();
                StopExternalProcess();
                return std::unexpected(MakePipelineError());
            }

            ClearFaults();
            m_LastState = state;
            SPDLOG_LOGGER_INFO(m_Logger, "Started GStreamer video pipeline");
            return {};
        }

        if (m_Endpoints != state.Endpoints)
        {
            ApplyEndpoints(state.Endpoints);
        }

        ClearFaults();
        m_LastState = state;
        return {};
    }

    Error::Api::Result<void> GstreamerPipeline::Stop()
    {
        if (!m_Pipeline)
        {
            StopExternalProcess();
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
        StopExternalProcess();
        SPDLOG_LOGGER_INFO(m_Logger, "Stopped GStreamer video pipeline");
        return {};
    }

    void GstreamerPipeline::PollBus()
    {
        DrainBusMessages();
        PollExternalProcess();
    }

    bool GstreamerPipeline::IsRunning() const noexcept
    {
        return static_cast<bool>(m_Pipeline);
    }

    ::PiSubmarine::Video::Telemetry::Api::Faults GstreamerPipeline::GetFaults() const noexcept
    {
        return m_ActiveFaults;
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

    bool GstreamerPipeline::InitializeGstreamer(const std::shared_ptr<spdlog::logger>& logger)
    {
        bool initialized = true;
        std::call_once(GstreamerInitFlag, [&initialized, &logger]
        {
            GError* error = nullptr;
            initialized = gst_init_check(nullptr, nullptr, &error);
            if (!initialized && error != nullptr)
            {
                g_error_free(error);
                return;
            }
        });

        if (initialized)
        {
            std::call_once(GstreamerStaticPluginRegistrationFlag, [&logger]
            {
                ::PiSubmarine::Gstreamer::Build::Plugins::RegisterStatic(logger);
            });
        }

        return initialized;
    }

    void GstreamerPipeline::LogRegistryDiagnostics(const std::shared_ptr<spdlog::logger>& logger)
    {
        SPDLOG_LOGGER_INFO(
            logger,
            "GStreamer factory availability: libcamerasrc={}, v4l2src={}, mfvideosrc={}, ksvideosrc={}, autovideosrc={}, "
            "fdsrc={}, x264enc={}, openh264enc={}, h264parse={}, rtph264pay={}, multiudpsink={}",
            HasFactory("libcamerasrc"),
            HasFactory("v4l2src"),
            HasFactory("mfvideosrc"),
            HasFactory("ksvideosrc"),
            HasFactory("autovideosrc"),
            HasFactory("fdsrc"),
            HasFactory("x264enc"),
            HasFactory("openh264enc"),
            HasFactory("h264parse"),
            HasFactory("rtph264pay"),
            HasFactory("multiudpsink"));

        SPDLOG_LOGGER_INFO(
            logger,
            "GStreamer factories containing '264': {}",
            JoinNames(CollectMatchingFactories("264")));
        SPDLOG_LOGGER_INFO(
            logger,
            "GStreamer factories containing 'enc': {}",
            JoinNames(CollectMatchingFactories("enc")));
        SPDLOG_LOGGER_INFO(
            logger,
            "GStreamer factories containing 'video': {}",
            JoinNames(CollectMatchingFactories("video")));
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

    std::string GstreamerPipeline::BuildSourceDescription(
        const Source& source,
        const std::shared_ptr<spdlog::logger>& logger)
    {
        static_cast<void>(logger);

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

        throw std::runtime_error(
            std::format(
                "No suitable GStreamer video source element was found. Candidates: [{}]. Visible 'video' factories: {}",
                JoinNames(candidates),
                JoinNames(CollectMatchingFactories("video"))));
    }

    std::string GstreamerPipeline::BuildHeadDescription(
        const PipelineHead& head,
        const Control::Video::Api::StreamProfile profile,
        const std::optional<int>& externalProcessReadFd,
        const std::shared_ptr<spdlog::logger>& logger)
    {
        if (const auto* autoDetectHead = std::get_if<AutoDetectPipelineHead>(&head))
        {
            const auto sourceDescription = BuildSourceDescription(autoDetectHead->VideoSource, logger);
            return std::format(
                "{} ! queue leaky=downstream max-size-buffers=1 max-size-bytes=0 max-size-time=0 ! {}",
                sourceDescription,
                BuildEncoderDescription(sourceDescription, profile, logger));
        }

        const auto* externalProcessHead = std::get_if<ExternalProcessHead>(&head);
        if (!externalProcessHead)
        {
            throw std::runtime_error("Unsupported GStreamer pipeline head configuration");
        }

        if (!externalProcessReadFd.has_value())
        {
            throw std::runtime_error("External process pipeline head requires a readable stdout pipe");
        }

        return std::format("fdsrc fd={} do-timestamp=true", *externalProcessReadFd);
    }

    std::string GstreamerPipeline::BuildEncoderDescription(
        const std::string_view sourceDescription,
        const Control::Video::Api::StreamProfile profile,
        const std::shared_ptr<spdlog::logger>& logger)
    {
        static_cast<void>(logger);

        const auto mediaFoundationDescription = [profile]() -> std::string
        {
            switch (profile)
            {
            case Control::Video::Api::StreamProfile::LowLatency:
                return "videoconvert ! video/x-raw,format=NV12 ! "
                       "mfh264enc low-latency=true bitrate=1000";
            case Control::Video::Api::StreamProfile::Standard:
                return "videoconvert ! video/x-raw,format=NV12 ! "
                       "mfh264enc low-latency=true bitrate=2500";
            case Control::Video::Api::StreamProfile::HighQuality:
                return "videoconvert ! video/x-raw,format=NV12 ! "
                       "mfh264enc low-latency=true bitrate=5000";
            }

            return "videoconvert ! video/x-raw,format=NV12 ! "
                   "mfh264enc low-latency=true bitrate=2500";
        };

        const auto x264Description = [profile]() -> std::string
        {
            switch (profile)
            {
            case Control::Video::Api::StreamProfile::LowLatency:
                return "videoconvert ! video/x-raw,format=I420 ! "
                       "x264enc tune=zerolatency speed-preset=ultrafast bitrate=1000 key-int-max=15 "
                       "bframes=0 cabac=false byte-stream=true sliced-threads=true";
            case Control::Video::Api::StreamProfile::Standard:
                return "videoconvert ! video/x-raw,format=I420 ! "
                       "x264enc tune=zerolatency speed-preset=veryfast bitrate=2500 key-int-max=30 "
                       "bframes=0 byte-stream=true sliced-threads=true";
            case Control::Video::Api::StreamProfile::HighQuality:
                return "videoconvert ! video/x-raw,format=I420 ! "
                       "x264enc tune=zerolatency speed-preset=fast bitrate=5000 key-int-max=60 "
                       "bframes=0 byte-stream=true";
            }

            return "videoconvert ! video/x-raw,format=I420 ! "
                   "x264enc tune=zerolatency speed-preset=veryfast bitrate=2500 key-int-max=30 "
                   "bframes=0 byte-stream=true sliced-threads=true";
        };

        const auto openH264Description = [profile]() -> std::string
        {
            switch (profile)
            {
            case Control::Video::Api::StreamProfile::LowLatency:
                return "videoconvert ! video/x-raw,format=I420 ! "
                       "openh264enc bitrate=1000000 complexity=low usage-type=camera";
            case Control::Video::Api::StreamProfile::Standard:
                return "videoconvert ! video/x-raw,format=I420 ! "
                       "openh264enc bitrate=2500000 complexity=medium usage-type=camera";
            case Control::Video::Api::StreamProfile::HighQuality:
                return "videoconvert ! video/x-raw,format=I420 ! "
                       "openh264enc bitrate=5000000 complexity=high usage-type=camera";
            }

            return "videoconvert ! video/x-raw,format=I420 ! "
                   "openh264enc bitrate=2500000 complexity=medium usage-type=camera";
        };

        if (HasFactory("mfh264enc") && ShouldPreferMediaFoundationEncoder(sourceDescription))
        {
            return mediaFoundationDescription();
        }

        if (HasFactory("x264enc"))
        {
            return x264Description();
        }

        if (HasFactory("openh264enc"))
        {
            return openH264Description();
        }

        throw std::runtime_error(
            std::format(
                "No suitable GStreamer H.264 encoder element was found. x264enc={}, openh264enc={}. Visible '264' "
                "factories: {}. Visible 'enc' factories: {}",
                HasFactory("x264enc"),
                HasFactory("openh264enc"),
                JoinNames(CollectMatchingFactories("264")),
                JoinNames(CollectMatchingFactories("enc"))));
    }

    std::string GstreamerPipeline::BuildPayloaderDescription()
    {
        return "h264parse config-interval=-1 ! rtph264pay pt=96 config-interval=-1 aggregate-mode=none";
    }

    std::string GstreamerPipeline::BuildPipelineDescription(
        const PipelineState& state,
        const std::optional<int>& externalProcessReadFd,
        const std::shared_ptr<spdlog::logger>& logger)
    {
        const auto* enabledState = state.Command.TryGetEnabled();
        if (enabledState == nullptr)
        {
            throw std::runtime_error("Cannot build a running pipeline for a disabled video command");
        }

        return std::format(
            "{} ! {} ! "
            "multiudpsink name=subscription_sink sync=false async=false",
            BuildHeadDescription(state.Configuration.VideoHead, enabledState->Profile, externalProcessReadFd, logger),
            BuildPayloaderDescription());
    }

    Error::Api::Result<void> GstreamerPipeline::StartExternalProcess(const ExternalProcessHead& head)
    {
        if (m_ExternalProcessReadFd.has_value())
        {
            return {};
        }

        if (head.Executable.empty())
        {
            SPDLOG_LOGGER_ERROR(m_Logger, "External process pipeline head requires a non-empty executable");
            SetFault(::PiSubmarine::Video::Telemetry::Api::Faults::SourceError);
            return std::unexpected(MakePipelineError());
        }

#ifdef _WIN32
        SPDLOG_LOGGER_ERROR(m_Logger, "External process pipeline head is not implemented on Windows");
        SetFault(::PiSubmarine::Video::Telemetry::Api::Faults::SourceError);
        return std::unexpected(MakePipelineError());
#else
        int stdoutPipe[2]{-1, -1};
        if (pipe(stdoutPipe) != 0)
        {
            SPDLOG_LOGGER_ERROR(
                m_Logger,
                "Failed to create stdout pipe for external process '{}': {}",
                head.Executable,
                std::strerror(errno));
            SetFault(::PiSubmarine::Video::Telemetry::Api::Faults::SourceError);
            return std::unexpected(MakePipelineError());
        }

        posix_spawn_file_actions_t fileActions;
        posix_spawn_file_actions_init(&fileActions);
        posix_spawn_file_actions_adddup2(&fileActions, stdoutPipe[1], STDOUT_FILENO);
        posix_spawn_file_actions_addclose(&fileActions, stdoutPipe[0]);
        posix_spawn_file_actions_addclose(&fileActions, stdoutPipe[1]);

        std::vector<std::string> ownedArguments;
        ownedArguments.reserve(head.Arguments.size() + 1);
        ownedArguments.push_back(head.Executable);
        ownedArguments.insert(ownedArguments.end(), head.Arguments.begin(), head.Arguments.end());

        std::vector<char*> argv;
        argv.reserve(ownedArguments.size() + 1);
        for (auto& argument : ownedArguments)
        {
            argv.push_back(argument.data());
        }

        argv.push_back(nullptr);

        pid_t processId = -1;
        const int spawnResult = posix_spawnp(
            &processId,
            head.Executable.c_str(),
            &fileActions,
            nullptr,
            argv.data(),
            environ);

        posix_spawn_file_actions_destroy(&fileActions);
        close(stdoutPipe[1]);

        if (spawnResult != 0)
        {
            close(stdoutPipe[0]);
            SPDLOG_LOGGER_ERROR(
                m_Logger,
                "Failed to start external process '{}': {}",
                JoinCommandForLog(head.Executable, head.Arguments),
                std::strerror(spawnResult));
            SetFault(::PiSubmarine::Video::Telemetry::Api::Faults::SourceError);
            return std::unexpected(MakePipelineError());
        }

        m_ExternalProcessId = static_cast<int>(processId);
        m_ExternalProcessReadFd = stdoutPipe[0];
        SPDLOG_LOGGER_INFO(
            m_Logger,
            "Started external video process '{}' with pid {}",
            JoinCommandForLog(head.Executable, head.Arguments),
            m_ExternalProcessId);
        return {};
#endif
    }

    void GstreamerPipeline::StopExternalProcess() noexcept
    {
#ifndef _WIN32
        if (m_ExternalProcessReadFd.has_value())
        {
            close(*m_ExternalProcessReadFd);
            m_ExternalProcessReadFd.reset();
        }

        if (m_ExternalProcessId > 0)
        {
            kill(m_ExternalProcessId, SIGTERM);

            int status = 0;
            if (waitpid(m_ExternalProcessId, &status, WNOHANG) == 0)
            {
                waitpid(m_ExternalProcessId, &status, 0);
            }

            m_ExternalProcessId = -1;
        }
#endif
    }

    void GstreamerPipeline::PollExternalProcess()
    {
#ifndef _WIN32
        if (m_ExternalProcessId <= 0)
        {
            return;
        }

        int status = 0;
        const auto waitResult = waitpid(m_ExternalProcessId, &status, WNOHANG);
        if (waitResult <= 0)
        {
            return;
        }

        if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        {
            SPDLOG_LOGGER_INFO(m_Logger, "External video process exited normally");
        }
        else if (WIFEXITED(status))
        {
            SPDLOG_LOGGER_ERROR(m_Logger, "External video process exited with code {}", WEXITSTATUS(status));
            SetFault(::PiSubmarine::Video::Telemetry::Api::Faults::SourceError);
        }
        else if (WIFSIGNALED(status))
        {
            SPDLOG_LOGGER_ERROR(m_Logger, "External video process terminated by signal {}", WTERMSIG(status));
            SetFault(::PiSubmarine::Video::Telemetry::Api::Faults::SourceError);
        }

        m_ExternalProcessId = -1;
        if (m_ExternalProcessReadFd.has_value())
        {
            close(*m_ExternalProcessReadFd);
            m_ExternalProcessReadFd.reset();
        }

        if (m_Pipeline)
        {
            static_cast<void>(Stop());
        }
#endif
    }

    void GstreamerPipeline::ApplyEndpoints(const std::vector<Subscription::Api::Endpoint>& endpoints)
    {
        if (m_MultiSink == nullptr)
        {
            return;
        }

        g_signal_emit_by_name(m_MultiSink, "clear");

        for (const auto& endpoint : endpoints)
        {
            g_signal_emit_by_name(
                m_MultiSink,
                "add",
                endpoint.Host.c_str(),
                static_cast<gint>(endpoint.Port));
            SPDLOG_LOGGER_INFO(
                m_Logger,
                "Added multiudpsink client {}:{}",
                endpoint.Host,
                endpoint.Port);
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
                SetFault(ClassifyBusError(error));
                static_cast<void>(Stop());
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

    void GstreamerPipeline::SetFault(const ::PiSubmarine::Video::Telemetry::Api::Faults fault) noexcept
    {
        m_ActiveFaults = static_cast<::PiSubmarine::Video::Telemetry::Api::Faults>(
            static_cast<uint32_t>(m_ActiveFaults) | static_cast<uint32_t>(fault));
    }

    void GstreamerPipeline::ClearFaults() noexcept
    {
        m_ActiveFaults = NoVideoFaults();
    }

    ::PiSubmarine::Video::Telemetry::Api::Faults GstreamerPipeline::ClassifyApplyFailure(
        const std::string_view diagnostic) const noexcept
    {
        if (diagnostic.contains("source") || diagnostic.contains("camera") || diagnostic.contains("device"))
        {
            return ::PiSubmarine::Video::Telemetry::Api::Faults::SourceError;
        }

        return ::PiSubmarine::Video::Telemetry::Api::Faults::ConfigError;
    }

    ::PiSubmarine::Video::Telemetry::Api::Faults GstreamerPipeline::ClassifyBusError(const GError* error) noexcept
    {
        if (error == nullptr || error->message == nullptr)
        {
            return ::PiSubmarine::Video::Telemetry::Api::Faults::ConfigError;
        }

        const std::string_view message(error->message);
        if (message.contains("udp") || message.contains("UDP") || message.contains("network"))
        {
            return ::PiSubmarine::Video::Telemetry::Api::Faults::NetworkError;
        }

        if (message.contains("source") || message.contains("device") || message.contains("camera"))
        {
            return ::PiSubmarine::Video::Telemetry::Api::Faults::SourceError;
        }

        return ::PiSubmarine::Video::Telemetry::Api::Faults::ConfigError;
    }
}
