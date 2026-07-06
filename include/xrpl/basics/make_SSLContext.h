#pragma once

#include <boost/asio/ssl/context.hpp>

#include <memory>
#include <string>

namespace xrpl {

/** Create a self-signed SSL context that allows anonymous Diffie Hellman. */
std::shared_ptr<boost::asio::ssl::context>
makeSslContext(std::string const& cipherList);

/** Create an authenticated SSL context using the specified files. */
std::shared_ptr<boost::asio::ssl::context>
makeSslContextAuthed(
    std::string const& keyFile,
    std::string const& certFile,
    std::string const& chainFile,
    std::string const& cipherList);

}  // namespace xrpl
