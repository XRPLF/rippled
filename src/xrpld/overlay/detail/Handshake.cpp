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

bool
featureEnabled(boost::beast::http::fields const& headers, std::string const& feature)
{
    return isFeatureValue(headers, feature, "1");
}

std::string
makeFeaturesRequestHeader(
    bool comprEnabled,
    bool ledgerReplayEnabled,
    bool txReduceRelayEnabled,
    bool vpReduceRelayEnabled)
{
    std::stringstream str;
    if (comprEnabled)
        str << kFeatureCompr << "=lz4" << kDelimFeature;
    if (ledgerReplayEnabled)
        str << kFeatureLedgerReplay << "=1" << kDelimFeature;
    if (txReduceRelayEnabled)
        str << kFeatureTxrr << "=1" << kDelimFeature;
    if (vpReduceRelayEnabled)
        str << kFeatureVprr << "=1" << kDelimFeature;
    return str.str();
}

std::string
makeFeaturesResponseHeader(
    http_request_type const& headers,
    bool comprEnabled,
    bool ledgerReplayEnabled,
    bool txReduceRelayEnabled,
    bool vpReduceRelayEnabled)
{
    std::stringstream str;
    if (comprEnabled && isFeatureValue(headers, kFeatureCompr, "lz4"))
        str << kFeatureCompr << "=lz4" << kDelimFeature;
    if (ledgerReplayEnabled && featureEnabled(headers, kFeatureLedgerReplay))
        str << kFeatureLedgerReplay << "=1" << kDelimFeature;
    if (txReduceRelayEnabled && featureEnabled(headers, kFeatureTxrr))
        str << kFeatureTxrr << "=1" << kDelimFeature;
    if (vpReduceRelayEnabled && featureEnabled(headers, kFeatureVprr))
        str << kFeatureVprr << "=1" << kDelimFeature;
    return str.str();
}

/** Hashes the latest finished message from an SSL stream.

    @param ssl the session to get the message from.
    @param get a pointer to the function to call to retrieve the finished
               message. This can be either:
               - `SSL_get_finished` or
               - `SSL_get_peer_finished`.
    @return `true` if successful, `false` otherwise.

    @note This construct is non-standard. There are potential "standard"
          alternatives that should be considered. For a discussion, on
          this topic, see https://github.com/openssl/openssl/issues/5509 and
          https://github.com/XRPLF/rippled/issues/2413.
*/
static std::optional<BaseUInt<512>>
hashLastMessage(SSL const* ssl, size_t (*get)(const SSL*, void*, size_t))
{
    static constexpr std::size_t kSslMinimumFinishedLength = 12;

    unsigned char buf[1024];
    size_t const len = get(ssl, buf, sizeof(buf));

    if (len < kSslMinimumFinishedLength)
        return std::nullopt;

    sha512_hasher const h;

    BaseUInt<512> cookie;
    SHA512(buf, len, cookie.data());
    return cookie;
}

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

    // Both messages hash to the same value and the cookie
    // is 0. Don't allow this.
    if (result == beast::kZero)
    {
        JLOG(journal.error()) << "Cookie generation: identical finished messages";
        return std::nullopt;
    }

    return sha512Half(Slice(result.data(), result.size()));
}

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
        // The network identifier, if configured, can be used to specify
        // what network we intend to connect to and detect if the remote
        // end connects to the same network.
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

    if (!app.config().serverDomain.empty())
        h.insert("Server-Domain", app.config().serverDomain);

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

    // This check gets two birds with one stone:
    //
    // 1) it verifies that the node we are talking to has access to the
    //    private key corresponding to the public node identity it claims.
    // 2) it verifies that our SSL session is end-to-end with that node
    //    and not through a proxy that establishes two separate sessions.
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
            // We know our public IP and peer reports our connection came
            // from some other IP.
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
            app.config().compression,
            app.config().ledgerReplay,
            app.config().txReduceRelayEnable,
            app.config().vpReduceRelayBaseSquelchEnable));

    buildHandshake(resp, sharedValue, networkID, publicIp, remoteIp, app);

    return resp;
}

}  // namespace xrpl
