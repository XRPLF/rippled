#ifndef XRPL_OVERLAY_MAKE_OVERLAY_H_INCLUDED
#define XRPL_OVERLAY_MAKE_OVERLAY_H_INCLUDED

#include <xrpld/overlay/Overlay.h>
#include <xrpld/rpc/ServerHandler.h>

#include <xrpl/basics/Resolver.h>

#include <boost/asio/io_context.hpp>

namespace ripple {

Overlay::Setup
setup_Overlay(BasicConfig const& config);

/** Creates the implementation of Overlay. */
std::unique_ptr<Overlay>
make_Overlay(
    Application& app,
    Overlay::Setup const& setup,
    ServerHandler& serverHandler,
    Resource::Manager& resourceManager,
    Resolver& resolver,
    boost::asio::io_context& io_context,
    BasicConfig const& config,
    beast::insight::Collector::ptr const& collector);

}  // namespace ripple

#endif
