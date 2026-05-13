/** @file
 *  Application-layer handshake exchanged immediately after TLS connection.
 *
 *  Responsibilities of this translation unit:
 *  - Derive a TLS-channel-bound shared value (`makeSharedValue`) that ties
 *    the node-identity proof to the specific TLS session, preventing MITM
 *    attacks even though TLS certificate verification is disabled.
 *  - Build (`buildHandshake`) and verify (`verifyHandshake`) the HTTP upgrade
 *    headers carrying node public keys, signatures, clock values, and IP
 *    cross-checks.
 *  - Negotiate optional protocol features (LZ4 compression, ledger replay,
 *    TX reduce-relay, VP reduce-relay) via `X-Protocol-Ctl` headers.
 *  - Assemble the outbound HTTP upgrade request (`makeRequest`) and the 101
 *    Switching Protocols response (`makeResponse`).
 *
 *  Neither side begins exchanging XRPL protocol messages until
 *  `verifyHandshake` succeeds.
 */
#include <xrpld/overlay/detail/Handshake.h>

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/detail/ProtocolVersion.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base64.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/beast/net/IPAddress.h>
#include <xrpl/beast/rfc2616.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/BuildInfo.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/tokens.h>

#include <boost/asio/ip/address.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/regex/v5/regex.hpp>
#include <boost/regex/v5/regex_search.hpp>
#include <boost/system/detail/error_code.hpp>

#include <openssl/crypto.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

// VFALCO Shouldn't we have to include the OpenSSL
// headers or something for SSL_get_finished?

namespace xrpl {

/** Extract the raw value string for a named feature from `X-Protocol-Ctl`.
 *
 *  Searches the `X-Protocol-Ctl` header for a `feature=<value>` token using
 *  a regex that stops at `;` or whitespace, matching only the first occurrence.
 *
 *  @param headers HTTP headers to search.
 *  @param feature Feature name (e.g. `"compr"`, `"txrr"`).
 *  @return The value string if found; `std::nullopt` if the header is absent
 *      or the feature is not present.
 */
std::optional<std::string>
getFeatureValue(boost::beast::http::fields const& headers, std::string const& feature)
{
    auto const header = headers.find("X-Protocol-Ctl");
    if (header == headers.end())
        return {};
    boost::smatch match;
    boost::regex const rx(feature + "=([^;\\s]+)");
    std::string const allFeatures(header->value());
    if (boost::regex_search(allFeatures, match, rx))
        return {match[1]};
    return {};
}

/** Check whether a feature's value matches a given string using RFC 2616
 *  token-list semantics.
 *
 *  Delegates to `beast::rfc2616::tokenInList`, which correctly handles
 *  comma-separated value lists (e.g. `compr=lz4,zstd`).
 *
 *  @param headers HTTP headers to inspect.
 *  @param feature Feature name to look up.
 *  @param value   Single token to match against (not a list itself).
 *  @return `true` if the feature is present and its value list contains
 *      `value`; `false` if absent or no match.
 */
bool
isFeatureValue(
    boost::beast::http::fields const& headers,
    std::string const& feature,
    std::string const& value)
{
    if (auto const fvalue = getFeatureValue(headers, feature))
        return beast::rfc2616::tokenInList(fvalue.value(), value);

    return false;
}

/** Return `true` if the named feature is present with value `"1"`.
 *
 *  Thin wrapper over `isFeatureValue(..., "1")` — the conventional
 *  boolean-enable sentinel used in `X-Protocol-Ctl`.
 *
 *  @param headers HTTP headers to inspect.
 *  @param feature Feature name to look up.
 */
bool
featureEnabled(boost::beast::http::fields const& headers, std::string const& feature)
{
    return isFeatureValue(headers, feature, "1");
}

/** Build the `X-Protocol-Ctl` value for an outbound connection request.
 *
 *  The initiator unconditionally advertises every locally enabled feature.
 *  The responder will echo back only those it also supports (see
 *  `makeFeaturesResponseHeader`), achieving single-round-trip negotiation.
 *
 *  @param comprEnabled          Advertise LZ4 compression (`compr=lz4`).
 *  @param ledgerReplayEnabled   Advertise ledger-replay (`ledgerreplay=1`).
 *  @param txReduceRelayEnabled  Advertise TX reduce-relay (`txrr=1`).
 *  @param vpReduceRelayEnabled  Advertise VP reduce-relay (`vprr=1`).
 *  @return Semicolon-delimited feature string, empty if no features enabled.
 */
std::string
makeFeaturesRequestHeader(
    bool comprEnabled,
    bool ledgerReplayEnabled,
    bool txReduceRelayEnabled,
    bool vpReduceRelayEnabled)
{
    std::stringstream str;
    if (comprEnabled)
        str << kFEATURE_COMPR << "=lz4" << kDELIM_FEATURE;
    if (ledgerReplayEnabled)
        str << kFEATURE_LEDGER_REPLAY << "=1" << kDELIM_FEATURE;
    if (txReduceRelayEnabled)
        str << kFEATURE_TXRR << "=1" << kDELIM_FEATURE;
    if (vpReduceRelayEnabled)
        str << kFEATURE_VPRR << "=1" << kDELIM_FEATURE;
    return str.str();
}

/** Build the `X-Protocol-Ctl` value for a 101 Switching Protocols response.
 *
 *  A feature is echoed back only when it is both locally configured *and*
 *  present in the peer's request header. This AND-gate ensures both sides
 *  converge on the same enabled feature set without an extra round-trip.
 *
 *  @param headers               The incoming HTTP upgrade request headers.
 *  @param comprEnabled          Accept LZ4 compression if peer requested it.
 *  @param ledgerReplayEnabled   Accept ledger-replay if peer requested it.
 *  @param txReduceRelayEnabled  Accept TX reduce-relay if peer requested it.
 *  @param vpReduceRelayEnabled  Accept VP reduce-relay if peer requested it.
 *  @return Semicolon-delimited feature string, empty if no features agreed.
 */
std::string
makeFeaturesResponseHeader(
    http_request_type const& headers,
    bool comprEnabled,
    bool ledgerReplayEnabled,
    bool txReduceRelayEnabled,
    bool vpReduceRelayEnabled)
{
    std::stringstream str;
    if (comprEnabled && isFeatureValue(headers, kFEATURE_COMPR, "lz4"))
        str << kFEATURE_COMPR << "=lz4" << kDELIM_FEATURE;
    if (ledgerReplayEnabled && featureEnabled(headers, kFEATURE_LEDGER_REPLAY))
        str << kFEATURE_LEDGER_REPLAY << "=1" << kDELIM_FEATURE;
    if (txReduceRelayEnabled && featureEnabled(headers, kFEATURE_TXRR))
        str << kFEATURE_TXRR << "=1" << kDELIM_FEATURE;
    if (vpReduceRelayEnabled && featureEnabled(headers, kFEATURE_VPRR))
        str << kFEATURE_VPRR << "=1" << kDELIM_FEATURE;
    return str.str();
}

/** Retrieve and SHA-512 hash a TLS finished message.
 *
 *  Calls `get` (either `SSL_get_finished` or `SSL_get_peer_finished`) to
 *  copy the raw TLS finished message into a stack buffer, then computes its
 *  SHA-512 digest. The finished message is derived from the full TLS
 *  handshake transcript, so it is unique to this specific TLS session.
 *
 *  Returns `std::nullopt` if the finished message is shorter than the
 *  RFC-mandated minimum (12 bytes), which indicates the TLS handshake has
 *  not yet completed on that side.
 *
 *  @param ssl The SSL session to query.
 *  @param get Function pointer — either `SSL_get_finished` (local side) or
 *      `SSL_get_peer_finished` (remote side).
 *  @return 512-bit digest of the finished message, or `std::nullopt` if the
 *      handshake is not yet complete.
 *
 *  @note This approach is non-standard. For alternatives and discussion, see
 *      https://github.com/openssl/openssl/issues/5509 and
 *      https://github.com/XRPLF/rippled/issues/2413.
 */
static std::optional<BaseUInt<512>>
hashLastMessage(SSL const* ssl, size_t (*get)(const SSL*, void*, size_t))
{
    constexpr std::size_t kSSL_MINIMUM_FINISHED_LENGTH = 12;

    unsigned char buf[1024];
    size_t const len = get(ssl, buf, sizeof(buf));

    if (len < kSSL_MINIMUM_FINISHED_LENGTH)
        return std::nullopt;

    sha512_hasher const h;

    BaseUInt<512> cookie;
    SHA512(buf, len, cookie.data());
    return cookie;
}

/** Derive a 256-bit value that is cryptographically bound to this TLS session.
 *
 *  Algorithm:
 *  1. SHA-512 hash our own TLS finished message (`SSL_get_finished`).
 *  2. SHA-512 hash the peer's TLS finished message (`SSL_get_peer_finished`).
 *  3. XOR the two 512-bit digests.
 *  4. Reduce to 256 bits via `sha512Half`.
 *
 *  Because TLS finished messages are derived from a transcript of the entire
 *  TLS handshake, both endpoints compute the same value only when they share
 *  the same session. A man-in-the-middle terminates two separate TLS sessions,
 *  producing different finished messages and therefore a different shared
 *  value — which causes `verifyHandshake` to reject the signature.
 *
 *  A degenerate edge case — both finished messages hashing to the same
 *  512-bit value, yielding an all-zero XOR — is treated as a hard failure to
 *  avoid a trivially forgeable shared value.
 *
 *  @param ssl     The established TLS stream whose finished messages are read.
 *  @param journal For error logging when either finished message is unavailable
 *      or the degenerate zero case is detected.
 *  @return The 256-bit shared value, or `std::nullopt` on any failure.
 */
std::optional<uint256>
makeSharedValue(stream_type& ssl, beast::Journal journal)
{
    auto const cookie1 = hashLastMessage(ssl.native_handle(), SSL_get_finished);
    if (!cookie1)
    {
        JLOG(journal.error()) << "Cookie generation: local setup not complete";
        return std::nullopt;
    }

    auto const cookie2 = hashLastMessage(ssl.native_handle(), SSL_get_peer_finished);
    if (!cookie2)
    {
        JLOG(journal.error()) << "Cookie generation: peer setup not complete";
        return std::nullopt;
    }

    auto const result = (*cookie1 ^ *cookie2);

    if (result == beast::kZERO)
    {
        JLOG(journal.error()) << "Cookie generation: identical finished messages";
        return std::nullopt;
    }

    return sha512Half(Slice(result.data(), result.size()));
}

/** Populate HTTP upgrade headers with node identity, authentication, and hints.
 *
 *  Inserts the following fields:
 *  - `Network-ID` — if configured, allows early cross-network detection
 *    before spending resources on full negotiation.
 *  - `Network-Time` — local XRPL clock value; recipient enforces ±20 s
 *    tolerance to prevent replay and clock-skew attacks.
 *  - `Public-Key` — base58-encoded secp256k1 node identity key.
 *  - `Session-Signature` — `sharedValue` signed by the node private key,
 *    base64-encoded. Proves key possession and binds identity to this TLS
 *    session (see `verifyHandshake`).
 *  - `Instance-Cookie` — runtime-unique identifier for duplicate-connection
 *    detection.
 *  - `Server-Domain` — optional TOML domain hint (omitted if unconfigured).
 *  - `Remote-IP` — peer's observed IP, if public; aids NAT diagnostics.
 *  - `Local-IP` — our public IP, if known; aids NAT diagnostics.
 *  - `Closed-Ledger` / `Previous-Ledger` — hex hashes of the most recent
 *    closed ledger header, if available.
 *
 *  @param h           Header fields container to populate (request or response).
 *  @param sharedValue TLS-channel-bound value from `makeSharedValue`.
 *  @param networkID   Optional network identifier from configuration.
 *  @param publicIp    Our public IP address (may be unspecified).
 *  @param remoteIp    The peer's IP address as seen from our socket.
 *  @param app         Application reference for clock, identity, and config.
 */
void
buildHandshake(
    boost::beast::http::fields& h,
    xrpl::uint256 const& sharedValue,
    std::optional<std::uint32_t> networkID,
    beast::IP::Address publicIp,
    beast::IP::Address remoteIp,
    Application& app)
{
    if (networkID)
    {
        h.insert("Network-ID", std::to_string(*networkID));
    }

    h.insert("Network-Time", std::to_string(app.getTimeKeeper().now().time_since_epoch().count()));

    h.insert("Public-Key", toBase58(TokenType::NodePublic, app.nodeIdentity().first));

    {
        auto const sig =
            signDigest(app.nodeIdentity().first, app.nodeIdentity().second, sharedValue);
        h.insert("Session-Signature", base64Encode(sig.data(), sig.size()));
    }

    h.insert("Instance-Cookie", std::to_string(app.instanceID()));

    if (!app.config().SERVER_DOMAIN.empty())
        h.insert("Server-Domain", app.config().SERVER_DOMAIN);

    if (beast::IP::isPublic(remoteIp))
        h.insert("Remote-IP", remoteIp.to_string());

    if (!publicIp.is_unspecified())
        h.insert("Local-IP", publicIp.to_string());

    if (auto const cl = app.getLedgerMaster().getClosedLedger())
    {
        h.insert("Closed-Ledger", strHex(cl->header().hash));
        h.insert("Previous-Ledger", strHex(cl->header().parentHash));
    }
}

/** Validate peer identity headers and return the peer's public key.
 *
 *  Performs layered checks, cheapest first:
 *  1. `Server-Domain` — must be a well-formed TOML domain if present.
 *  2. `Network-ID` — must match our configured network identifier if both
 *     sides supply one; mismatch is an early reject before crypto work.
 *  3. `Network-Time` — must be within ±20 s of our local XRPL clock.
 *  4. `Public-Key` — must parse as a valid secp256k1 node public key.
 *  5. `Session-Signature` — the peer's signature of `sharedValue` under
 *     its claimed public key. This check simultaneously proves:
 *     (a) the peer holds the private key matching the claimed identity, and
 *     (b) the TLS session is end-to-end with that node — a MITM terminating
 *         two separate TLS sessions would produce a different `sharedValue`
 *         and therefore an invalid signature.
 *  6. Self-connection guard — rejects a connection to our own node key.
 *  7. `Local-IP` cross-check — if the peer's observed public IP is known,
 *     it must match what the peer claims as its own local IP.
 *  8. `Remote-IP` cross-check — if both our public IP and the peer's public
 *     address are known, the peer's reported remote IP must match ours.
 *
 *  @param headers     HTTP headers from the upgrade request or response.
 *  @param sharedValue TLS-channel-bound value from `makeSharedValue`.
 *  @param networkID   Our configured network identifier, if any.
 *  @param publicIp    Our public IP address (may be unspecified).
 *  @param remote      The peer's IP address as seen from our socket.
 *  @param app         Application reference for clock, identity, and config.
 *  @return The peer's authenticated public key.
 *  @throws std::runtime_error on any validation failure; callers should
 *      catch and tear down the connection.
 */
PublicKey
verifyHandshake(
    boost::beast::http::fields const& headers,
    xrpl::uint256 const& sharedValue,
    std::optional<std::uint32_t> networkID,
    beast::IP::Address publicIp,
    beast::IP::Address remote,
    Application& app)
{
    if (auto const iter = headers.find("Server-Domain"); iter != headers.end())
    {
        if (!isProperlyFormedTomlDomain(iter->value()))
            throw std::runtime_error("Invalid server domain");
    }

    if (auto const iter = headers.find("Network-ID"); iter != headers.end())
    {
        std::uint32_t nid = 0;

        if (!beast::lexicalCastChecked(nid, iter->value()))
            throw std::runtime_error("Invalid peer network identifier");

        if (networkID && nid != *networkID)
            throw std::runtime_error("Peer is on a different network");
    }

    if (auto const iter = headers.find("Network-Time"); iter != headers.end())
    {
        auto const netTime = [str = iter->value()]() -> TimeKeeper::time_point {
            TimeKeeper::duration::rep val = 0;

            if (beast::lexicalCastChecked(val, str))
                return TimeKeeper::time_point{TimeKeeper::duration{val}};

            // It's not an error for the header field to not be present but if
            // it is present and it contains junk data, that is an error.
            throw std::runtime_error("Invalid peer clock timestamp");
        }();

        using namespace std::chrono;

        auto const ourTime = app.getTimeKeeper().now();
        auto const tolerance = 20s;

        // We can't blindly "return a-b;" because TimeKeeper::time_point
        // uses an unsigned integer for representing durations, which is
        // a problem when trying to subtract time points.
        auto calculateOffset = [](TimeKeeper::time_point a, TimeKeeper::time_point b) {
            if (a > b)
                return duration_cast<std::chrono::seconds>(a - b);
            return -duration_cast<std::chrono::seconds>(b - a);
        };

        auto const offset = calculateOffset(netTime, ourTime);

        if (abs(offset) > tolerance)
            throw std::runtime_error("Peer clock is too far off");
    }

    PublicKey const publicKey = [&headers] {
        if (auto const iter = headers.find("Public-Key"); iter != headers.end())
        {
            auto pk = parseBase58<PublicKey>(TokenType::NodePublic, iter->value());

            if (pk)
            {
                if (publicKeyType(*pk) != KeyType::Secp256k1)
                    throw std::runtime_error("Unsupported public key type");

                return *pk;
            }
        }

        throw std::runtime_error("Bad node public key");
    }();

    {
        auto const iter = headers.find("Session-Signature");

        if (iter == headers.end())
            throw std::runtime_error("No session signature specified");

        auto sig = base64Decode(iter->value());

        if (!verifyDigest(publicKey, sharedValue, makeSlice(sig), false))
            throw std::runtime_error("Failed to verify session");
    }

    if (publicKey == app.nodeIdentity().first)
        throw std::runtime_error("Self connection");

    if (auto const iter = headers.find("Local-IP"); iter != headers.end())
    {
        boost::system::error_code ec;
        auto const localIp = boost::asio::ip::make_address(std::string_view(iter->value()), ec);

        if (ec)
            throw std::runtime_error("Invalid Local-IP");

        if (beast::IP::isPublic(remote) && remote != localIp)
        {
            throw std::runtime_error(
                "Incorrect Local-IP: " + remote.to_string() + " instead of " + localIp.to_string());
        }
    }

    if (auto const iter = headers.find("Remote-IP"); iter != headers.end())
    {
        boost::system::error_code ec;
        auto const remoteIp = boost::asio::ip::make_address(std::string_view(iter->value()), ec);

        if (ec)
            throw std::runtime_error("Invalid Remote-IP");

        if (beast::IP::isPublic(remote) && !beast::IP::isUnspecified(publicIp))
        {
            if (remoteIp != publicIp)
            {
                throw std::runtime_error(
                    "Incorrect Remote-IP: " + publicIp.to_string() + " instead of " +
                    remoteIp.to_string());
            }
        }
    }

    return publicKey;
}

/** Build the outbound HTTP/1.1 upgrade request that initiates a peer connection.
 *
 *  Uses the WebSocket-style protocol upgrade pattern so the handshake looks
 *  like a standard HTTP upgrade to any intermediate infrastructure:
 *  `Connection: Upgrade`, `Upgrade: <supported protocol versions>`,
 *  `Connect-As: Peer`. The `X-Protocol-Ctl` header is populated with all
 *  locally enabled features via `makeFeaturesRequestHeader` — the responder
 *  will echo back only the intersection.
 *
 *  @param crawlPublic           If `true`, advertise `Crawl: public`.
 *  @param comprEnabled          Advertise LZ4 compression support.
 *  @param ledgerReplayEnabled   Advertise ledger-replay support.
 *  @param txReduceRelayEnabled  Advertise TX reduce-relay support.
 *  @param vpReduceRelayEnabled  Advertise VP reduce-relay support.
 *  @return An HTTP request with empty body ready for async write.
 */
auto
makeRequest(
    bool crawlPublic,
    bool comprEnabled,
    bool ledgerReplayEnabled,
    bool txReduceRelayEnabled,
    bool vpReduceRelayEnabled) -> request_type
{
    request_type m;
    m.method(boost::beast::http::verb::get);
    m.target("/");
    m.version(11);
    m.insert("User-Agent", BuildInfo::getFullVersionString());
    m.insert("Upgrade", supportedProtocolVersions());
    m.insert("Connection", "Upgrade");
    m.insert("Connect-As", "Peer");
    m.insert("Crawl", crawlPublic ? "public" : "private");
    m.insert(
        "X-Protocol-Ctl",
        makeFeaturesRequestHeader(
            comprEnabled, ledgerReplayEnabled, txReduceRelayEnabled, vpReduceRelayEnabled));
    return m;
}

/** Build the 101 Switching Protocols response that accepts a peer connection.
 *
 *  Sets the agreed `ProtocolVersion` in the `Upgrade` header (narrowing from
 *  the list of versions the initiator offered), echoes only the mutually
 *  supported features via `makeFeaturesResponseHeader`, and then populates
 *  all identity and authentication fields via `buildHandshake`.
 *
 *  @param crawlPublic  If `true`, advertise `Crawl: public`.
 *  @param req          The incoming HTTP upgrade request.
 *  @param publicIp     Our public IP address (may be unspecified).
 *  @param remoteIp     The peer's IP address as seen from our socket.
 *  @param sharedValue  TLS-channel-bound value from `makeSharedValue`.
 *  @param networkID    Our configured network identifier, if any.
 *  @param protocol     The single agreed protocol version to echo back.
 *  @param app          Application reference for config, clock, and identity.
 *  @return A complete HTTP response ready for async write.
 */
http_response_type
makeResponse(
    bool crawlPublic,
    http_request_type const& req,
    beast::IP::Address publicIp,
    beast::IP::Address remoteIp,
    uint256 const& sharedValue,
    std::optional<std::uint32_t> networkID,
    ProtocolVersion protocol,
    Application& app)
{
    http_response_type resp;
    resp.result(boost::beast::http::status::switching_protocols);
    resp.version(req.version());
    resp.insert("Connection", "Upgrade");
    resp.insert("Upgrade", to_string(protocol));
    resp.insert("Connect-As", "Peer");
    resp.insert("Server", BuildInfo::getFullVersionString());
    resp.insert("Crawl", crawlPublic ? "public" : "private");
    resp.insert(
        "X-Protocol-Ctl",
        makeFeaturesResponseHeader(
            req,
            app.config().COMPRESSION,
            app.config().LEDGER_REPLAY,
            app.config().TX_REDUCE_RELAY_ENABLE,
            app.config().VP_REDUCE_RELAY_BASE_SQUELCH_ENABLE));

    buildHandshake(resp, sharedValue, networkID, publicIp, remoteIp, app);

    return resp;
}

}  // namespace xrpl
