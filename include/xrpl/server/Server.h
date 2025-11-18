#ifndef XRPL_SERVER_SERVER_H_INCLUDED
#define XRPL_SERVER_SERVER_H_INCLUDED

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/PropertyStream.h>
#include <xrpl/server/Port.h>
#include <xrpl/server/detail/ServerImpl.h>

#include <boost/asio/io_context.hpp>

namespace ripple {

/** Create the HTTP server using the specified handler. */
template <class Handler>
std::unique_ptr<Server>
make_Server(
    Handler& handler,
    boost::asio::io_context& io_context,
    beast::Journal journal)
{
    return std::make_unique<ServerImpl<Handler>>(handler, io_context, journal);
}

}  // namespace ripple

#endif
