#ifndef XRPL_NET_REGISTER_SSL_CERTS_H_INCLUDED
#define XRPL_NET_REGISTER_SSL_CERTS_H_INCLUDED

#include <xrpl/basics/Log.h>

#include <boost/asio/ssl/context.hpp>

namespace ripple {
/** Register default SSL certificates.

    Register the system default SSL root certificates. On linux/mac,
    this just calls asio's `set_default_verify_paths` to look in standard
    operating system locations. On windows, it uses the OS certificate
    store accessible via CryptoAPI.
*/
void
registerSSLCerts(
    boost::asio::ssl::context&,
    boost::system::error_code&,
    beast::Journal j);

}  // namespace ripple

#endif
