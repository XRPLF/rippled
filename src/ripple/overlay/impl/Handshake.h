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

#ifndef RIPPLE_OVERLAY_HANDSHAKE_H_INCLUDED
#define RIPPLE_OVERLAY_HANDSHAKE_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/protocol/BuildInfo.h>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>

#include <boost/asio/ssl.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/optional.hpp>
#include <utility>

namespace ripple {

using socket_type = boost::beast::tcp_stream;
using stream_type = boost::beast::ssl_stream<socket_type>;

/** Computes a shared value based on the SSL connection state.

    When there is no man in the middle, both sides will compute the same
    value. In the presence of an attacker, the computed values will be
    different.

    @param ssl the SSL/TLS connection state.
    @return A 256-bit value on success; an unseated optional otherwise.
*/
boost::optional<uint256>
makeSharedValue(stream_type& ssl, beast::Journal journal);

/** Insert fields headers necessary for upgrading the link to the peer protocol.
 */
void
buildHandshake(
    boost::beast::http::fields& h,
    uint256 const& sharedValue,
    boost::optional<std::uint32_t> networkID,
    beast::IP::Address public_ip,
    beast::IP::Address remote_ip,
    Application& app);

/** Validate header fields necessary for upgrading the link to the peer
   protocol.

    This performs critical security checks that ensure that prevent
    MITM attacks on our peer-to-peer links and that the remote peer
    has the private keys that correspond to the public identity it
    claims.

    @return The public key of the remote peer.
    @throw A class derived from std::exception.
*/
PublicKey
verifyHandshake(
    boost::beast::http::fields const& headers,
    uint256 const& sharedValue,
    boost::optional<std::uint32_t> networkID,
    beast::IP::Address public_ip,
    beast::IP::Address remote,
    Application& app);

}  // namespace ripple

#endif
