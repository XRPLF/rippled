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
#include <ripple/overlay/impl/ProtocolVersion.h>
#include <ripple/protocol/BuildInfo.h>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>

#include <boost/asio/ssl.hpp>
#include <boost/beast/http/dynamic_body.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/fields.hpp>
#include <optional>
#include <utility>

namespace ripple {

using socket_type = boost::beast::tcp_stream;
using stream_type = boost::beast::ssl_stream<socket_type>;
using request_type =
    boost::beast::http::request<boost::beast::http::empty_body>;
using http_request_type =
    boost::beast::http::request<boost::beast::http::dynamic_body>;
using http_response_type =
    boost::beast::http::response<boost::beast::http::dynamic_body>;

/** Retrieves a value shared by the two endpoints of a TLS secured connection.

    This value plays an important role in detecting and preventing active
    MITM attacks.In the presence of an attacker, the two endpoints will produce
    different values.

    @param ssl the SSL/TLS connection state.
    @return On success, the 256-bit value this side believes both endpoints
            share; an unseated optional otherwise.

    @note If a value is returned, it must be verified.
 */
std::optional<uint256>
getSessionEKM(stream_type& ssl);

/** Computes a shared value based on the SSL connection state.

    When there is no man in the middle, both sides will compute the same
    value. In the presence of an attacker, the computed values will be
    different.

    @param ssl the SSL/TLS connection state.
    @return A 256-bit value on success; an unseated optional otherwise.
*/
std::optional<uint256>
makeSharedValue(stream_type& ssl, beast::Journal journal);

/** Populate header fields needed when upgrading the link to the peer protocol.

    Some of the fields are used in critical security checks that can prevent
    active MITM attacks and ensure that the remote peer has the private keys
    that correspond to the public identity it claims.

    @param h the list of HTTP headers fields to send.
    @param sharedValue a 256-bit value derived from the SSL session (legacy).
    @param ekm a 256-bit value derived from the SSL session.
    @param networkID the identifier of the network the server is configured for.
    @param public_ip The server's public IP.
    @param remote_ip The IP to which the server attempted to connect.
    @param app The main application object.

    @note The `sharedValue` parameter is deprecated and will be removed in a
          future version of the code. It is replaced by `ekm` which is derived
          in a more standardized function.

          \sa makeSharedValue, getSessionEKM
 */
void
buildHandshake(
    boost::beast::http::fields& h,
    uint256 const& sharedValue,
    uint256 const& ekm,
    std::optional<std::uint32_t> networkID,
    beast::IP::Address public_ip,
    beast::IP::Address remote_ip,
    Application& app);

/** Validate header fields needed when upgrading the link to the peer protocol.

    Some of the fields are used in critical security checks that can prevent
    active MITM attacks and ensure that the remote peer has the private keys
    that correspond to the public identity it claims.

    @param h the list of HTTP headers fields we received.
    @param sharedValue a 256-bit value derived from the SSL session (legacy).
    @param ekm a 256-bit value derived from the SSL session.
    @param networkID the identifier of the network the server is configured for.
    @param public_ip The server's public IP.
    @param remote_ip The IP to which the server attempted to connect.
    @param app The main application object.

    @return The public key of the remote peer on success. An exception
            otherwise.

    @throw A class derived from std::exception, with an appropriate error message.
*/
PublicKey
verifyHandshake(
    boost::beast::http::fields const& headers,
    uint256 const& sharedValue,
    uint256 const& ekm,
    std::optional<std::uint32_t> networkID,
    beast::IP::Address public_ip,
    beast::IP::Address remote,
    Application& app);

/** Make outbound http request

   @param crawlPublic if true then server's IP/Port are included in crawl
   @param comprEnabled if true then compression feature is enabled
   @param vpReduceRelayEnabled if true then reduce-relay feature is enabled
   @param ledgerReplayEnabled if true then ledger-replay feature is enabled
   @return http request with empty body
 */
request_type
makeRequest(
    bool crawlPublic,
    bool comprEnabled,
    bool vpReduceRelayEnabled,
    bool ledgerReplayEnabled);

/** Make http response

   @param crawlPublic if true then server's IP/Port are included in crawl
   @param req incoming http request
   @param public_ip server's public IP
   @param remote_ip peer's IP
   @param sharedValue shared value based on the SSL connection state
   @param ekm shared value based on the SSL connection state
   @param networkID specifies what network we intend to connect to
   @param version supported protocol version
   @param app Application's reference to access some common properties
   @return http response
 */
http_response_type
makeResponse(
    bool crawlPublic,
    http_request_type const& req,
    beast::IP::Address public_ip,
    beast::IP::Address remote_ip,
    uint256 const& sharedValue,
    uint256 const& ekm,
    std::optional<std::uint32_t> networkID,
    ProtocolVersion version,
    Application& app);

// Protocol features negotiated via HTTP handshake.
// The format is:
// X-Protocol-Ctl: feature1=value1[,value2]*[\s*;\s*feature2=value1[,value2]*]*
// value: \S+
static constexpr char FEATURE_COMPR[] = "compr";  // compression
static constexpr char FEATURE_VPRR[] =
    "vprr";  // validation/proposal reduce-relay
static constexpr char FEATURE_LEDGER_REPLAY[] =
    "ledgerreplay";  // ledger replay
static constexpr char DELIM_FEATURE[] = ";";
static constexpr char DELIM_VALUE[] = ",";

/** Get feature's header value
   @param headers request/response header
   @param feature name
   @return seated optional with feature's value if the feature
      is found in the header, unseated optional otherwise
 */
std::optional<std::string>
getFeatureValue(
    boost::beast::http::fields const& headers,
    std::string const& feature);

/** Check if a feature's value is equal to the specified value
   @param headers request/response header
   @param feature to check
   @param value of the feature to check, must be a single value; i.e. not
        value1,value2...
   @return true if the feature's value matches the specified value, false if
   doesn't match or the feature is not found in the header
 */
bool
isFeatureValue(
    boost::beast::http::fields const& headers,
    std::string const& feature,
    std::string const& value);

/** Check if a feature is enabled
   @param headers request/response header
   @param feature to check
   @return true if enabled
 */
bool
featureEnabled(
    boost::beast::http::fields const& headers,
    std::string const& feature);

/** Check if a feature should be enabled for a peer. The feature
    is enabled if its configured value is true and the http header
    has the specified feature value.
   @tparam headers request (inbound) or response (outbound) header
   @param request http headers
   @param feature to check
   @param config feature's configuration value
   @param value feature's value to check in the headers
   @return true if the feature is enabled
 */
template <typename headers>
bool
peerFeatureEnabled(
    headers const& request,
    std::string const& feature,
    std::string value,
    bool config)
{
    return config && isFeatureValue(request, feature, value);
}

/** Wrapper for enable(1)/disable type(0) of feature */
template <typename headers>
bool
peerFeatureEnabled(
    headers const& request,
    std::string const& feature,
    bool config)
{
    return config && peerFeatureEnabled(request, feature, "1", config);
}

/** Make request header X-Protocol-Ctl value with supported features
   @param comprEnabled if true then compression feature is enabled
   @param vpReduceRelayEnabled if true then reduce-relay feature is enabled
   @param ledgerReplayEnabled if true then ledger-replay feature is enabled
   @return X-Protocol-Ctl header value
 */
std::string
makeFeaturesRequestHeader(
    bool comprEnabled,
    bool vpReduceRelayEnabled,
    bool ledgerReplayEnabled);

/** Make response header X-Protocol-Ctl value with supported features.
    If the request has a feature that we support enabled
    and the feature's configuration is enabled then enable this feature in
    the response header.
   @param header request's header
   @param comprEnabled if true then compression feature is enabled
   @param vpReduceRelayEnabled if true then reduce-relay feature is enabled
   @param ledgerReplayEnabled if true then ledger-replay feature is enabled
   @return X-Protocol-Ctl header value
 */
std::string
makeFeaturesResponseHeader(
    http_request_type const& headers,
    bool comprEnabled,
    bool vpReduceRelayEnabled,
    bool ledgerReplayEnabled);

}  // namespace ripple

#endif
