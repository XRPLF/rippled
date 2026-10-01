#pragma once

#include <xrpld/app/misc/detail/WorkBase.h>
#include <xrpld/core/Config.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/net/HTTPClientSSLContext.h>

#include <boost/asio/ssl.hpp>

#include <memory>
#include <string>

namespace xrpl::detail {

// Work over SSL
class WorkSSL : public WorkBase<WorkSSL>, public std::enable_shared_from_this<WorkSSL>
{
    friend class WorkBase<WorkSSL>;

private:
    using StreamType = boost::asio::ssl::stream<SocketType&>;

    HTTPClientSSLContext context_;
    StreamType stream_;

public:
    WorkSSL(
        std::string const& host,
        std::string const& path,
        std::string const& port,
        boost::asio::io_context& ios,
        beast::Journal j,
        Config const& config,
        EndpointType const& lastEndpoint,
        bool lastStatus,
        CallbackType cb);
    ~WorkSSL() override = default;

private:
    StreamType&
    stream()
    {
        return stream_;
    }

    void
    onConnect(ErrorCode const& ec);

    void
    onHandshake(ErrorCode const& ec);
};

}  // namespace xrpl::detail
