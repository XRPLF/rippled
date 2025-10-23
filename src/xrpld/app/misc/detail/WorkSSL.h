#ifndef XRPL_APP_MISC_DETAIL_WORKSSL_H_INCLUDED
#define XRPL_APP_MISC_DETAIL_WORKSSL_H_INCLUDED

#include <xrpld/app/misc/detail/WorkBase.h>
#include <xrpld/core/Config.h>

#include <xrpl/basics/contract.h>
#include <xrpl/net/HTTPClientSSLContext.h>

#include <boost/asio/ssl.hpp>
#include <boost/format.hpp>

#include <functional>

namespace ripple {

namespace detail {

// Work over SSL
class WorkSSL : public WorkBase<WorkSSL>,
                public std::enable_shared_from_this<WorkSSL>
{
    friend class WorkBase<WorkSSL>;

private:
    using stream_type = boost::asio::ssl::stream<socket_type&>;

    HTTPClientSSLContext context_;
    stream_type stream_;

public:
    WorkSSL(
        std::string const& host,
        std::string const& path,
        std::string const& port,
        boost::asio::io_context& ios,
        beast::Journal j,
        Config const& config,
        endpoint_type const& lastEndpoint,
        bool lastStatus,
        callback_type cb);
    ~WorkSSL() = default;

private:
    stream_type&
    stream()
    {
        return stream_;
    }

    void
    onConnect(error_code const& ec);

    void
    onHandshake(error_code const& ec);
};

}  // namespace detail

}  // namespace ripple

#endif
