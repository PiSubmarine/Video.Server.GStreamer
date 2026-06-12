#include "PiSubmarine/Video/Server/GStreamer/StaticPluginRegistration.h"

#include <mutex>

#include <gst/gst.h>
#include <spdlog/spdlog.h>

#if PISUBMARINE_GSTREAMER_HAS_STATIC_PLUGINS
extern "C"
{
GST_PLUGIN_STATIC_DECLARE(coreelements);
GST_PLUGIN_STATIC_DECLARE(autodetect);
GST_PLUGIN_STATIC_DECLARE(videoconvertscale);
GST_PLUGIN_STATIC_DECLARE(video4linux2);
GST_PLUGIN_STATIC_DECLARE(openh264);
GST_PLUGIN_STATIC_DECLARE(rtp);
GST_PLUGIN_STATIC_DECLARE(udp);
}
#endif

namespace PiSubmarine::Video::Server::GStreamer
{
    namespace
    {
        std::once_flag StaticPluginRegistrationFlag;
    }

    void RegisterStaticPlugins(const std::shared_ptr<spdlog::logger>& logger)
    {
        std::call_once(StaticPluginRegistrationFlag, [&logger]
        {
#if PISUBMARINE_GSTREAMER_HAS_STATIC_PLUGINS
            SPDLOG_LOGGER_INFO(logger, "Registering static GStreamer plugin 'coreelements'");
            GST_PLUGIN_STATIC_REGISTER(coreelements);
            SPDLOG_LOGGER_INFO(logger, "Registering static GStreamer plugin 'autodetect'");
            GST_PLUGIN_STATIC_REGISTER(autodetect);
            SPDLOG_LOGGER_INFO(logger, "Registering static GStreamer plugin 'videoconvertscale'");
            GST_PLUGIN_STATIC_REGISTER(videoconvertscale);
            SPDLOG_LOGGER_INFO(logger, "Registering static GStreamer plugin 'video4linux2'");
            GST_PLUGIN_STATIC_REGISTER(video4linux2);
            SPDLOG_LOGGER_INFO(logger, "Registering static GStreamer plugin 'openh264'");
            GST_PLUGIN_STATIC_REGISTER(openh264);
            SPDLOG_LOGGER_INFO(logger, "Registering static GStreamer plugin 'rtp'");
            GST_PLUGIN_STATIC_REGISTER(rtp);
            SPDLOG_LOGGER_INFO(logger, "Registering static GStreamer plugin 'udp'");
            GST_PLUGIN_STATIC_REGISTER(udp);
#else
            SPDLOG_LOGGER_INFO(logger, "GStreamer static plugin archives were not found; using dynamic plugin discovery");
#endif
        });
    }
}
