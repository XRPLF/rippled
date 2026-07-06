#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/server/detail/ServerImpl.h>

#include <boost/asio/io_context.hpp>

#include <memory>

namespace xrpl {

/** Create the HTTP server using the specified handler. */
template <class Handler>
std::unique_ptr<Server>
makeServer(Handler& handler, boost::asio::io_context& ioContext, beast::Journal journal)
{
    return std::make_unique<ServerImpl<Handler>>(handler, ioContext, journal);
}

}  // namespace xrpl
