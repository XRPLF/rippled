#include <xrpld/app/misc/detail/WorkSSL.h>

namespace ripple {
namespace detail {

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
          config.SSL_VERIFY_DIR,
          config.SSL_VERIFY_FILE,
          config.SSL_VERIFY,
          j,
          boost::asio::ssl::context::tlsv12_client)
    , stream_(socket_, context_.context())
{
    auto ec = context_.preConnectVerify(stream_, host_);
    if (ec)
        Throw<std::runtime_error>(
            boost::str(boost::format("preConnectVerify: %s") % ec.message()));
}

void
WorkSSL::onConnect(error_code const& ec)
{
    auto err = ec ? ec : context_.postConnectVerify(stream_, host_);
    if (err)
        return fail(err);

    stream_.async_handshake(
        boost::asio::ssl::stream_base::client,
        boost::asio::bind_executor(
            strand_,
            std::bind(
                &WorkSSL::onHandshake,
                shared_from_this(),
                std::placeholders::_1)));
}

void
WorkSSL::onHandshake(error_code const& ec)
{
    if (ec)
        return fail(ec);

    onStart();
}

}  // namespace detail

}  // namespace ripple
