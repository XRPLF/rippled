#pragma once

#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Overlay.h>
#include <xrpld/rpc/ServerHandler.h>

#include <xrpl/basics/Resolver.h>
#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/resource/ResourceManager.h>

#include <boost/asio/io_context.hpp>

#include <memory>

namespace xrpl {

Overlay::Setup
setupOverlay(BasicConfig const& config, beast::Journal j);

/** Creates the implementation of Overlay. */
std::unique_ptr<Overlay>
makeOverlay(
    Application& app,
    Overlay::Setup const& setup,
    ServerHandler& serverHandler,
    Resource::Manager& resourceManager,
    Resolver& resolver,
    boost::asio::io_context& ioContext,
    BasicConfig const& config,
    beast::insight::Collector::ptr const& collector);

}  // namespace xrpl
