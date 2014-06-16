//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_COMMON_SSLCONTEXT_H_INCLUDED
#define RIPPLE_COMMON_SSLCONTEXT_H_INCLUDED

#include <beast/module/asio/basics/SSLContext.h>

#include <string>

namespace ripple {

/** The SSL contexts used by Ripple.

    This is what Ripple uses for its secure connections. The ECDSA curve
    parameters are predefined and verified to be secure. The context is set to
    sslv23, Transport Layer Security / General. This is primarily used for peer to peer servers that don't care
    about certificates or identity verification.
*/
class RippleSSLContext : public beast::asio::SSLContext
{
public:
    /** Retrieve raw DH parameters.
        This is in the format expected by the OpenSSL function d2i_DHparams.
        The vector is binary. An empty vector means the key size is unsupported.
        @note The string may contain nulls in the middle. Use size() to
              determine the actual size.
    */
    static std::string getRawDHParams (int keySize);

    /** Creates a bare SSL context with just sslv23 set.
        This is used for RPC connections.
    */
    static RippleSSLContext* createBare ();

    /** Creates a suitable for WebSocket without authentication.
        This is for WebSocket connections that don't use certificates.
    */
    static RippleSSLContext* createWebSocket ();

    /** Create a context that allows anonymous connections.
        No certificates are required. Peers use this context.
        If the cipher list is invalid, a fatal error is raised.
    */
    static RippleSSLContext* createAnonymous (std::string const& cipherList);

    /** Create a context with authentication requirements.
        This is used for WebSocket connections.
        The authentication credentials are loaded from the files with
        the specified names. If an error occurs, a fatal error is raised.
    */
    static RippleSSLContext* createAuthenticated (
        std::string key_file, std::string cert_file, std::string chain_file);

protected:
    explicit RippleSSLContext (ContextType& context);
};

}

#endif
