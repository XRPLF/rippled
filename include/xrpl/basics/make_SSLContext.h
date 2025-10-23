#ifndef XRPL_BASICS_MAKE_SSLCONTEXT_H_INCLUDED
#define XRPL_BASICS_MAKE_SSLCONTEXT_H_INCLUDED

#include <boost/asio/ssl/context.hpp>

#include <string>

namespace ripple {

/** Create a self-signed SSL context that allows anonymous Diffie Hellman. */
std::shared_ptr<boost::asio::ssl::context>
make_SSLContext(std::string const& cipherList);

/** Create an authenticated SSL context using the specified files. */
std::shared_ptr<boost::asio::ssl::context>
make_SSLContextAuthed(
    std::string const& keyFile,
    std::string const& certFile,
    std::string const& chainFile,
    std::string const& cipherList);

}  // namespace ripple

#endif
