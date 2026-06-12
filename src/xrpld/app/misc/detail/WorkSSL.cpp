#include <xrpld/app/misc/detail/WorkSSL.h>

#include <xrpld/app/misc/detail/WorkBase.h>
#include <xrpld/core/Config.h>

#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Journal.h>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/format/free_funcs.hpp>

#include <functional>
#include <stdexcept>
#include <string>

namespace xrpl::detail {

WorkSSL::WorkSSL(
    std::string const& host,
    std::string const& path,
    std::string const& port,
    boost::asio::io_context& ios,
    beast::Journal j,
    Config const& config,
    endpoint_type const& lastEndpoint,
    bool lastStatus,
    callback_type cb)
    : WorkBase(host, path, port, ios, lastEndpoint, lastStatus, cb)
    , context_(
          config.sslVerifyDir,
          config.sslVerifyFile,
          config.sslVerify,
          j,
          boost::asio::ssl::context::tlsv12_client)
    , stream_(socket_, context_.context())
{
    auto ec = context_.preConnectVerify(stream_, host_);
    if (ec)
        Throw<std::runtime_error>(boost::str(boost::format("preConnectVerify: %s") % ec.message()));
}

void
WorkSSL::onConnect(error_code const& ec)
{
    auto err = ec ? ec : context_.postConnectVerify(stream_, host_);
    if (err)
    {
        fail(err);
        return;
    }

    stream_.async_handshake(
        boost::asio::ssl::stream_base::client,
        boost::asio::bind_executor(
            strand_, std::bind(&WorkSSL::onHandshake, shared_from_this(), std::placeholders::_1)));
}

void
WorkSSL::onHandshake(error_code const& ec)
{
    if (ec)
    {
        fail(ec);
        return;
    }

    onStart();
}

}  // namespace xrpl::detail
