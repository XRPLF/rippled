#pragma once

#include <xrpld/overlay/Overlay.h>
#include <xrpld/rpc/ServerHandler.h>

#include <xrpl/basics/Resolver.h>

#include <boost/asio/io_context.hpp>

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
