#pragma once

#include <memory>

#include <spdlog/logger.h>

namespace PiSubmarine::Video::Server::GStreamer
{
    void RegisterStaticPlugins(const std::shared_ptr<spdlog::logger>& logger);
}
