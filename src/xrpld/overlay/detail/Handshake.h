/** @file
 *  Interface contract for the XRPL peer-to-peer overlay handshake.
 *
 *  A new TCP connection is not promoted to the live XRPL protocol stream
 *  immediately; it goes through a two-phase sequence: first TLS setup, then
 *  an HTTP/1.1 Upgrade exchange that carries cryptographic identity proof and
 *  capability advertisement. Every function declared here belongs to one of
 *  those phases; `Handshake.cpp` is the sole implementation site.
 *
 *  The file also pins the canonical network type aliases used across the
 *  entire overlay subsystem, and defines the `X-Protocol-Ctl` feature names
 *  and delimiters for optional capability negotiation.
 */
#pragma once

#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/detail/ProtocolVersion.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/BuildInfo.h>

#include <boost/asio/ssl.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/dynamic_body.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>

#include <optional>
#include <utility>

namespace xrpl {

/** Underlying TCP stream used for all peer connections. */
using socket_type = boost::beast::tcp_stream;

/** TLS stream wrapping the TCP socket; used for all encrypted peer I/O. */
using stream_type = boost::beast::ssl_stream<socket_type>;

/** Outbound HTTP/1.1 Upgrade request (empty body); built by `makeRequest`. */
using request_type = boost::beast::http::request<boost::beast::http::empty_body>;

/** Inbound HTTP/1.1 Upgrade request (dynamic body); received from the peer. */
using http_request_type = boost::beast::http::request<boost::beast::http::dynamic_body>;

/** HTTP/1.1 101 Switching Protocols response (dynamic body); built by `makeResponse`. */
using http_response_type = boost::beast::http::response<boost::beast::http::dynamic_body>;

/** Derive a 256-bit value cryptographically bound to this TLS session.
 *
 *  XORs the SHA-512 hashes of both TLS Finished messages (`SSL_get_finished`
 *  and `SSL_get_peer_finished`), then reduces the 512-bit XOR to 256 bits
 *  via `sha512Half`. Both endpoints compute identical values only when they
 *  share the same direct TLS session; a man-in-the-middle who terminates two
 *  separate sessions will produce a different value on each side, causing
 *  `verifyHandshake` to reject the `Session-Signature`.
 *
 *  A degenerate case — both Finished hashes XORing to zero — is treated as a
 *  hard failure to prevent a trivially forgeable shared secret.
 *
 *  @param ssl     The fully established TLS stream.
 *  @param journal Used to log errors when finished messages are unavailable
 *      or the zero-XOR degenerate case is detected.
 *  @return The 256-bit shared value, or `std::nullopt` on any failure.
 *  @note This technique is non-standard; TLS channel binding via Finished
 *      messages is fragile with session resumption. See OpenSSL issue #5509
 *      and XRPLF/rippled #2413.
 */
std::optional<uint256>
makeSharedValue(stream_type& ssl, beast::Journal journal);

/** Populate HTTP upgrade headers with node identity, authentication, and hints.
 *
 *  Inserts the following fields into `h`:
 *  - `Public-Key` — base58-encoded secp256k1 node identity key.
 *  - `Session-Signature` — `sharedValue` signed by the node's private key,
 *    base64-encoded. Proves key ownership and binds identity to this TLS
 *    session; verified by the peer in `verifyHandshake`.
 *  - `Network-ID` — optional numeric tag for early cross-network detection.
 *  - `Network-Time` — sender's XRPL clock value; receiver enforces ±20 s.
 *  - `Instance-Cookie` — per-process unique ID for reconnection detection.
 *  - `Local-IP` / `Remote-IP` — public IPs for NAT diagnostics (omitted if
 *    unknown or private).
 *  - `Closed-Ledger` / `Previous-Ledger` — hex hashes of the most recently
 *    closed ledger, if available.
 *
 *  @param h           HTTP fields container to populate (request or response).
 *  @param sharedValue TLS-channel-bound value from `makeSharedValue`.
 *  @param networkID   Optional network identifier from configuration.
 *  @param publicIp    Our public IP address (may be unspecified).
 *  @param remoteIp    The peer's IP address as observed on our socket.
 *  @param app         Application reference for clock, identity, and config.
 */
void
buildHandshake(
    boost::beast::http::fields& h,
    uint256 const& sharedValue,
    std::optional<std::uint32_t> networkID,
    beast::IP::Address publicIp,
    beast::IP::Address remoteIp,
    Application& app);

/** Validate identity and authentication headers from a peer upgrade exchange.
 *
 *  Performs layered checks, cheapest first, so expensive cryptography is only
 *  reached if the network and clock pre-checks pass:
 *  1. `Network-ID` — must match our configured identifier if both sides
 *     supply one; mismatch causes early rejection before crypto work.
 *  2. `Network-Time` — must be within ±20 s of our local XRPL clock.
 *  3. `Public-Key` — must parse as a valid secp256k1 node public key.
 *  4. `Session-Signature` — the peer's signature of `sharedValue` under its
 *     claimed public key. Simultaneously proves key ownership and end-to-end
 *     TLS (a MITM would produce a different `sharedValue` and fail here).
 *  5. Self-connection guard — rejects connections from our own node key.
 *  6. `Local-IP` cross-check — peer's claimed public IP must match the
 *     address from which we see the connection arriving (NAT sanity check).
 *  7. `Remote-IP` cross-check — peer's reported view of our IP must match
 *     our known public IP, if both are available.
 *
 *  @param headers     HTTP fields from the upgrade request or 101 response.
 *  @param sharedValue TLS-channel-bound value from `makeSharedValue`.
 *  @param networkID   Our configured network identifier, if any.
 *  @param publicIp    Our public IP address (may be unspecified).
 *  @param remote      The peer's IP address as observed on our socket.
 *  @param app         Application reference for clock, identity, and config.
 *  @return The peer's authenticated public key; callers use this to identify
 *      the connection going forward.
 *  @throws std::runtime_error on any validation failure. Callers in
 *      `ConnectAttempt` and `PeerImp` catch this and tear down the connection.
 */
PublicKey
verifyHandshake(
    boost::beast::http::fields const& headers,
    uint256 const& sharedValue,
    std::optional<std::uint32_t> networkID,
    beast::IP::Address publicIp,
    beast::IP::Address remote,
    Application& app);

/** Build the outbound HTTP/1.1 upgrade request that initiates a peer connection.
 *
 *  Sets `Connection: Upgrade`, `Upgrade: <supported protocol versions>`, and
 *  `Connect-As: Peer`. Populates `X-Protocol-Ctl` with all locally enabled
 *  features via `makeFeaturesRequestHeader`; the responder echoes back only
 *  the intersection. Identity headers (`Public-Key`, `Session-Signature`, etc.)
 *  are NOT included here — the caller adds them via a separate `buildHandshake`
 *  call on the returned fields.
 *
 *  @param crawlPublic           If `true`, advertise `Crawl: public` so the
 *      node's IP is included in peer-crawler responses.
 *  @param comprEnabled          Advertise LZ4 message compression support.
 *  @param ledgerReplayEnabled   Advertise ledger-replay subsystem support.
 *  @param txReduceRelayEnabled  Advertise TX reduce-relay support.
 *  @param vpReduceRelayEnabled  Advertise validation/proposal reduce-relay
 *      support.
 *  @return An HTTP request with empty body, ready for async write.
 */
request_type
makeRequest(
    bool crawlPublic,
    bool comprEnabled,
    bool ledgerReplayEnabled,
    bool txReduceRelayEnabled,
    bool vpReduceRelayEnabled);

/** Build the HTTP/1.1 101 Switching Protocols response that accepts a peer.
 *
 *  Sets the agreed protocol version in the `Upgrade` header (narrowing from
 *  the list the initiator offered), echoes back only the mutually supported
 *  features via `makeFeaturesResponseHeader`, and then calls `buildHandshake`
 *  to append all identity and authentication headers in one shot.
 *
 *  @param crawlPublic  If `true`, advertise `Crawl: public`.
 *  @param req          The incoming HTTP upgrade request from the peer.
 *  @param publicIp     Our public IP address (may be unspecified).
 *  @param remoteIp     The peer's IP address as observed on our socket.
 *  @param sharedValue  TLS-channel-bound value from `makeSharedValue`.
 *  @param networkID    Our configured network identifier, if any.
 *  @param version      The single agreed protocol version to echo back.
 *  @param app          Application reference for config, clock, and identity.
 *  @return A complete HTTP response with identity headers, ready for async write.
 */
http_response_type
makeResponse(
    bool crawlPublic,
    http_request_type const& req,
    beast::IP::Address publicIp,
    beast::IP::Address remoteIp,
    uint256 const& sharedValue,
    std::optional<std::uint32_t> networkID,
    ProtocolVersion version,
    Application& app);

// --- X-Protocol-Ctl feature negotiation ---
// Wire format:
//   X-Protocol-Ctl: feature1=value1[,value2]*[; feature2=value1[,value2]*]*
// Requesters advertise all locally enabled features; responders echo back
// only those they also support (AND-gate, single round-trip negotiation).

/** Wire name for the LZ4 message compression feature (`compr=lz4`). */
static constexpr char kFEATURE_COMPR[] = "compr";

/** Wire name for the validation/proposal reduce-relay (squelch) feature (`vprr=1`). */
static constexpr char kFEATURE_VPRR[] = "vprr";

/** Wire name for the transaction reduce-relay feature (`txrr=1`). */
static constexpr char kFEATURE_TXRR[] = "txrr";

/** Wire name for the ledger-replay subsystem feature (`ledgerreplay=1`). */
static constexpr char kFEATURE_LEDGER_REPLAY[] = "ledgerreplay";

/** Delimiter separating distinct features in the `X-Protocol-Ctl` header. */
static constexpr char kDELIM_FEATURE[] = ";";

/** Delimiter separating multiple values for a single feature. */
static constexpr char kDELIM_VALUE[] = ",";

/** Extract the raw value string for a named feature from `X-Protocol-Ctl`.
 *
 *  Uses a regex search (`feature=<value>`, stopping at `;` or whitespace)
 *  and returns the first match. The returned string may itself be a
 *  comma-separated list (e.g. `"lz4,zstd"`); use `isFeatureValue` to test
 *  membership within that list.
 *
 *  @param headers HTTP fields to search (request or response).
 *  @param feature Feature name to look up (e.g. `"compr"`, `"txrr"`).
 *  @return The feature's value string if found; `std::nullopt` if the
 *      `X-Protocol-Ctl` header is absent or the feature is not present.
 */
std::optional<std::string>
getFeatureValue(boost::beast::http::fields const& headers, std::string const& feature);

/** Check whether a feature's value list contains a specific token.
 *
 *  Uses RFC 2616 token-list semantics via `beast::rfc2616::tokenInList`, so
 *  a feature advertised as `compr=lz4,zstd` will match both `"lz4"` and
 *  `"zstd"` as individual tokens.
 *
 *  @param headers HTTP fields to inspect (request or response).
 *  @param feature Feature name to look up.
 *  @param value   Single token to match; must not itself be a comma list.
 *  @return `true` if the feature is present and its value list contains
 *      `value`; `false` if absent or no match.
 */
bool
isFeatureValue(
    boost::beast::http::fields const& headers,
    std::string const& feature,
    std::string const& value);

/** Return `true` if the named feature is present with value `"1"`.
 *
 *  Convenience wrapper over `isFeatureValue(..., "1")` — the conventional
 *  boolean-enable sentinel used for non-compression features in
 *  `X-Protocol-Ctl`.
 *
 *  @param headers HTTP fields to inspect (request or response).
 *  @param feature Feature name to look up.
 *  @return `true` if the feature is present and its value is `"1"`.
 */
bool
featureEnabled(boost::beast::http::fields const& headers, std::string const& feature);

/** Decide whether to activate a feature for a peer connection.
 *
 *  A feature is active only when the local configuration enables it AND the
 *  peer's HTTP headers carry the expected feature value. This is the final
 *  predicate called by `PeerImp` after the handshake completes.
 *
 *  @tparam Headers HTTP fields type (request for inbound, response for outbound).
 *  @param request  The peer's HTTP headers to inspect.
 *  @param feature  Feature name (e.g. `kFEATURE_COMPR`).
 *  @param value    Required value token (e.g. `"lz4"`).
 *  @param config   Local configuration flag; if `false` the feature is
 *      disabled regardless of what the peer advertises.
 *  @return `true` only when both `config` is `true` and `isFeatureValue`
 *      confirms the peer's header contains `value`.
 */
template <typename Headers>
bool
peerFeatureEnabled(
    Headers const& request,
    std::string const& feature,
    std::string value,
    bool config)
{
    return config && isFeatureValue(request, feature, value);
}

/** Decide whether to activate a boolean (enable/disable) feature for a peer.
 *
 *  Convenience overload for features that use `"1"` as their enable sentinel.
 *  Equivalent to `peerFeatureEnabled(request, feature, "1", config)`.
 *
 *  @tparam Headers HTTP fields type (request for inbound, response for outbound).
 *  @param request  The peer's HTTP headers to inspect.
 *  @param feature  Feature name (e.g. `kFEATURE_TXRR`).
 *  @param config   Local configuration flag.
 *  @return `true` only when both `config` is `true` and the peer's header
 *      has the feature with value `"1"`.
 */
template <typename Headers>
bool
peerFeatureEnabled(Headers const& request, std::string const& feature, bool config)
{
    return config && peerFeatureEnabled(request, feature, "1", config);
}

/** Build the `X-Protocol-Ctl` value for an outbound connection request.
 *
 *  The initiator unconditionally advertises every locally enabled feature.
 *  The responder will echo back only those it also supports (see
 *  `makeFeaturesResponseHeader`), achieving single-round-trip negotiation.
 *  Compression is advertised as `compr=lz4`; all other features use `=1`.
 *
 *  @param comprEnabled          Advertise LZ4 compression (`compr=lz4`).
 *  @param ledgerReplayEnabled   Advertise ledger-replay (`ledgerreplay=1`).
 *  @param txReduceRelayEnabled  Advertise TX reduce-relay (`txrr=1`).
 *  @param vpReduceRelayEnabled  Advertise VP reduce-relay (`vprr=1`).
 *  @return Semicolon-delimited feature string, or an empty string if no
 *      features are enabled.
 */
std::string
makeFeaturesRequestHeader(
    bool comprEnabled,
    bool ledgerReplayEnabled,
    bool txReduceRelayEnabled,
    bool vpReduceRelayEnabled);

/** Build the `X-Protocol-Ctl` value for a 101 Switching Protocols response.
 *
 *  A feature is included in the response only when the local configuration
 *  enables it AND the peer's request header already declares it. This AND-gate
 *  ensures both sides converge on the same enabled feature set without an
 *  additional round-trip.
 *
 *  @param headers               The incoming HTTP upgrade request headers.
 *  @param comprEnabled          Accept LZ4 compression if peer requested it.
 *  @param ledgerReplayEnabled   Accept ledger-replay if peer requested it.
 *  @param txReduceRelayEnabled  Accept TX reduce-relay if peer requested it.
 *  @param vpReduceRelayEnabled  Accept VP reduce-relay if peer requested it.
 *  @return Semicolon-delimited feature string containing only mutually agreed
 *      features, or an empty string if none are agreed.
 */
std::string
makeFeaturesResponseHeader(
    http_request_type const& headers,
    bool comprEnabled,
    bool ledgerReplayEnabled,
    bool txReduceRelayEnabled,
    bool vpReduceRelayEnabled);

}  // namespace xrpl
