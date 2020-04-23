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

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/basics/base64.h>
#include <ripple/basics/safe_cast.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/beast/rfc2616.h>
#include <ripple/overlay/impl/Handshake.h>
#include <ripple/protocol/digest.h>
#include <boost/regex.hpp>
#include <algorithm>
#include <chrono>

// VFALCO Shouldn't we have to include the OpenSSL
// headers or something for SSL_get_finished?

namespace ripple {

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
          https://github.com/ripple/rippled/issues/2413.
*/
static boost::optional<base_uint<512>>
hashLastMessage(SSL const* ssl, size_t (*get)(const SSL*, void*, size_t))
{
    constexpr std::size_t sslMinimumFinishedLength = 12;

    unsigned char buf[1024];
    size_t len = get(ssl, buf, sizeof(buf));

    if (len < sslMinimumFinishedLength)
        return boost::none;

    sha512_hasher h;

    base_uint<512> cookie;
    SHA512(buf, len, cookie.data());
    return cookie;
}

boost::optional<uint256>
makeSharedValue(stream_type& ssl, beast::Journal journal)
{
    auto const cookie1 = hashLastMessage(ssl.native_handle(), SSL_get_finished);
    if (!cookie1)
    {
        JLOG(journal.error()) << "Cookie generation: local setup not complete";
        return boost::none;
    }

    auto const cookie2 =
        hashLastMessage(ssl.native_handle(), SSL_get_peer_finished);
    if (!cookie2)
    {
        JLOG(journal.error()) << "Cookie generation: peer setup not complete";
        return boost::none;
    }

    auto const result = (*cookie1 ^ *cookie2);

    // Both messages hash to the same value and the cookie
    // is 0. Don't allow this.
    if (result == beast::zero)
    {
        JLOG(journal.error())
            << "Cookie generation: identical finished messages";
        return boost::none;
    }

    return sha512Half(Slice(result.data(), result.size()));
}

void
buildHandshake(
    boost::beast::http::fields& h,
    ripple::uint256 const& sharedValue,
    boost::optional<std::uint32_t> networkID,
    beast::IP::Address public_ip,
    beast::IP::Address remote_ip,
    Application& app)
{
    if (networkID)
    {
        // The network identifier, if configured, can be used to specify
        // what network we intend to connect to and detect if the remote
        // end connects to the same network.
        h.insert("Network-ID", std::to_string(*networkID));
    }

    h.insert(
        "Network-Time",
        std::to_string(app.timeKeeper().now().time_since_epoch().count()));

    h.insert(
        "Public-Key",
        toBase58(TokenType::NodePublic, app.nodeIdentity().first));

    {
        auto const sig = signDigest(
            app.nodeIdentity().first, app.nodeIdentity().second, sharedValue);
        h.insert("Session-Signature", base64_encode(sig.data(), sig.size()));
    }

    if (beast::IP::is_public(remote_ip))
        h.insert("Remote-IP", remote_ip.to_string());

    if (!public_ip.is_unspecified())
        h.insert("Local-IP", public_ip.to_string());

    if (auto const cl = app.getLedgerMaster().getClosedLedger())
    {
        // TODO: Use hex for these
        h.insert(
            "Closed-Ledger",
            base64_encode(cl->info().hash.begin(), cl->info().hash.size()));
        h.insert(
            "Previous-Ledger",
            base64_encode(
                cl->info().parentHash.begin(), cl->info().parentHash.size()));
    }
}

PublicKey
verifyHandshake(
    boost::beast::http::fields const& headers,
    ripple::uint256 const& sharedValue,
    boost::optional<std::uint32_t> networkID,
    beast::IP::Address public_ip,
    beast::IP::Address remote,
    Application& app)
{
    if (networkID)
    {
        if (auto const iter = headers.find("Network-ID"); iter != headers.end())
        {
            std::uint32_t nid;

            if (!beast::lexicalCastChecked(nid, iter->value().to_string()))
                throw std::runtime_error("Invalid peer network identifier");

            if (nid != *networkID)
                throw std::runtime_error("Peer is on a different network");
        }
    }

    if (auto const iter = headers.find("Network-Time"); iter != headers.end())
    {
        auto const netTime =
            [str = iter->value().to_string()]() -> TimeKeeper::time_point {
            TimeKeeper::duration::rep val;

            if (beast::lexicalCastChecked(val, str))
                return TimeKeeper::time_point{TimeKeeper::duration{val}};

            // It's not an error for the header field to not be present but if
            // it is present and it contains junk data, that is an error.
            throw std::runtime_error("Invalid peer clock timestamp");
        }();

        using namespace std::chrono;

        auto const ourTime = app.timeKeeper().now();
        auto const tolerance = 20s;

        // We can't blindly "return a-b;" because TimeKeeper::time_point
        // uses an unsigned integer for representing durations, which is
        // a problem when trying to subtract time points.
        // FIXME: @HowardHinnant, should we migrate to using std::int64_t?
        auto calculateOffset = [](TimeKeeper::time_point a,
                                  TimeKeeper::time_point b) {
            if (a > b)
                return duration_cast<std::chrono::seconds>(a - b);
            return -duration_cast<std::chrono::seconds>(b - a);
        };

        auto const offset = calculateOffset(netTime, ourTime);

        if (date::abs(offset) > tolerance)
            throw std::runtime_error("Peer clock is too far off");
    }

    PublicKey const publicKey = [&headers] {
        if (auto const iter = headers.find("Public-Key"); iter != headers.end())
        {
            auto pk = parseBase58<PublicKey>(
                TokenType::NodePublic, iter->value().to_string());

            if (pk)
            {
                if (publicKeyType(*pk) != KeyType::secp256k1)
                    throw std::runtime_error("Unsupported public key type");

                return *pk;
            }
        }

        throw std::runtime_error("Bad node public key");
    }();

    if (publicKey == app.nodeIdentity().first)
        throw std::runtime_error("Self connection");

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

        auto sig = base64_decode(iter->value().to_string());

        if (!verifyDigest(publicKey, sharedValue, makeSlice(sig), false))
            throw std::runtime_error("Failed to verify session");
    }

    if (auto const iter = headers.find("Local-IP"); iter != headers.end())
    {
        boost::system::error_code ec;
        auto const local_ip = boost::asio::ip::address::from_string(
            iter->value().to_string(), ec);

        if (ec)
            throw std::runtime_error("Invalid Local-IP");

        if (beast::IP::is_public(remote) && remote != local_ip)
            throw std::runtime_error(
                "Incorrect Local-IP: " + remote.to_string() + " instead of " +
                local_ip.to_string());
    }

    if (auto const iter = headers.find("Remote-IP"); iter != headers.end())
    {
        boost::system::error_code ec;
        auto const remote_ip = boost::asio::ip::address::from_string(
            iter->value().to_string(), ec);

        if (ec)
            throw std::runtime_error("Invalid Remote-IP");

        if (beast::IP::is_public(remote) &&
            !beast::IP::is_unspecified(public_ip))
        {
            // We know our public IP and peer reports our connection came
            // from some other IP.
            if (remote_ip != public_ip)
                throw std::runtime_error(
                    "Incorrect Remote-IP: " + public_ip.to_string() +
                    " instead of " + remote_ip.to_string());
        }
    }

    return publicKey;
}

}  // namespace ripple
