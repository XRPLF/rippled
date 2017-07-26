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

#include <BeastConfig.h>
#include <ripple/overlay/impl/TMHello.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/beast/rfc2616.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/protocol/digest.h>
#include <beast/core/detail/base64.hpp>
#include <boost/regex.hpp>
#include <algorithm>

// VFALCO Shouldn't we have to include the OpenSSL
// headers or something for SSL_get_finished?

namespace ripple {

/** Hashes the latest finished message from an SSL stream
    @param sslSession the session to get the message from.
    @param hash       the buffer into which the hash of the retrieved
                        message will be saved. The buffer MUST be at least
                        64 bytes long.
    @param getMessage a pointer to the function to call to retrieve the
                        finished message. This be either:
                        `SSL_get_finished` or
                        `SSL_get_peer_finished`.
    @return `true` if successful, `false` otherwise.
*/
static
boost::optional<base_uint<512>>
hashLastMessage (SSL const* ssl,
    size_t (*get)(const SSL *, void *buf, size_t))
{
    enum
    {
        sslMinimumFinishedLength = 12
    };
    unsigned char buf[1024];
    size_t len = get (ssl, buf, sizeof (buf));
    if(len < sslMinimumFinishedLength)
        return boost::none;
    base_uint<512> cookie;
    SHA512 (buf, len, cookie.data());
    return cookie;
}

boost::optional<uint256>
makeSharedValue (SSL* ssl, beast::Journal journal)
{
    auto const cookie1 = hashLastMessage(ssl, SSL_get_finished);
    if (!cookie1)
    {
        JLOG (journal.error()) << "Cookie generation: local setup not complete";
        return boost::none;
    }

    auto const cookie2 = hashLastMessage(ssl, SSL_get_peer_finished);
    if (!cookie2)
    {
        JLOG (journal.error()) << "Cookie generation: peer setup not complete";
        return boost::none;
    }

    auto const result = (*cookie1 ^ *cookie2);

    // Both messages hash to the same value and the cookie
    // is 0. Don't allow this.
    if (result == zero)
    {
        JLOG(journal.error()) << "Cookie generation: identical finished messages";
        return boost::none;
    }

    return sha512Half (Slice (result.data(), result.size()));
}

protocol::TMHello
buildHello (
    uint256 const& sharedValue,
    beast::IP::Address public_ip,
    beast::IP::Endpoint remote,
    Application& app)
{
    protocol::TMHello h;

    auto const sig = signDigest (
        app.nodeIdentity().first,
        app.nodeIdentity().second,
        sharedValue);

    h.set_protoversion (to_packed (BuildInfo::getCurrentProtocol()));
    h.set_protoversionmin (to_packed (BuildInfo::getMinimumProtocol()));
    h.set_fullversion (BuildInfo::getFullVersionString ());
    h.set_nettime (app.timeKeeper().now().time_since_epoch().count());
    h.set_nodepublic (
        toBase58 (
            TokenType::TOKEN_NODE_PUBLIC,
            app.nodeIdentity().first));
    h.set_nodeproof (sig.data(), sig.size());
    // h.set_ipv4port (portNumber); // ignored now
    h.set_testnet (false);

    if (remote.is_v4())
    {
        auto addr = remote.to_v4 ();
        if (is_public (addr))
        {
            // Connection is to a public IP
            h.set_remote_ip (addr.value);
            if (public_ip != beast::IP::Address())
                h.set_local_ip (public_ip.to_v4().value);
        }
    }

    // We always advertise ourselves as private in the HELLO message. This
    // suppresses the old peer advertising code and allows PeerFinder to
    // take over the functionality.
    h.set_nodeprivate (true);

    auto const closedLedger = app.getLedgerMaster().getClosedLedger();

    assert(! closedLedger->open());
    // VFALCO There should ALWAYS be a closed ledger
    if (closedLedger)
    {
        uint256 hash = closedLedger->info().hash;
        h.set_ledgerclosed (hash.begin (), hash.size ());
        hash = closedLedger->info().parentHash;
        h.set_ledgerprevious (hash.begin (), hash.size ());
    }

    return h;
}

void
appendHello (beast::http::fields& h,
    protocol::TMHello const& hello)
{
    //h.append ("Protocol-Versions",...

    h.insert ("Public-Key", hello.nodepublic());

    h.insert ("Session-Signature", beast::detail::base64_encode (
        hello.nodeproof()));

    if (hello.has_nettime())
        h.insert ("Network-Time", std::to_string (hello.nettime()));

    if (hello.has_ledgerindex())
        h.insert ("Ledger", std::to_string (hello.ledgerindex()));

    if (hello.has_ledgerclosed())
        h.insert ("Closed-Ledger", beast::detail::base64_encode (
            hello.ledgerclosed()));

    if (hello.has_ledgerprevious())
        h.insert ("Previous-Ledger", beast::detail::base64_encode (
            hello.ledgerprevious()));

    if (hello.has_local_ip())
        h.insert ("Local-IP", beast::IP::to_string (
            beast::IP::AddressV4(hello.local_ip())));

    if (hello.has_remote_ip())
        h.insert ("Remote-IP", beast::IP::to_string (
            beast::IP::AddressV4(hello.remote_ip())));
}

std::vector<ProtocolVersion>
parse_ProtocolVersions(beast::string_view const& value)
{
    static boost::regex re (
        "^"                  // start of line
        "RTXP/"              // the string "RTXP/"
        "([1-9][0-9]*)"      // a number (non-zero and with no leading zeroes)
        "\\."                // a period
        "(0|[1-9][0-9]*)"    // a number (no leading zeroes unless exactly zero)
        "$"                  // The end of the string
        , boost::regex_constants::optimize);

    auto const list = beast::rfc2616::split_commas(value);
    std::vector<ProtocolVersion> result;
    for (auto const& s : list)
    {
        boost::smatch m;
        if (! boost::regex_match (s, m, re))
            continue;
        int major;
        int minor;
        if (! beast::lexicalCastChecked (
                major, std::string (m[1])))
            continue;
        if (! beast::lexicalCastChecked (
                minor, std::string (m[2])))
            continue;
        result.push_back (std::make_pair (major, minor));
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique (result.begin(), result.end()), result.end());
    return result;
}

boost::optional<protocol::TMHello>
parseHello (bool request, beast::http::fields const& h, beast::Journal journal)
{
    // protocol version in TMHello is obsolete,
    // it is supplanted by the values in the headers.
    protocol::TMHello hello;

    {
        // Required
        auto const iter = h.find ("Upgrade");
        if (iter == h.end())
            return boost::none;
        auto const versions = parse_ProtocolVersions(iter->value().to_string());
        if (versions.empty())
            return boost::none;
        hello.set_protoversion(
            (static_cast<std::uint32_t>(versions.back().first) << 16) |
            (static_cast<std::uint32_t>(versions.back().second)));
        hello.set_protoversionmin(
            (static_cast<std::uint32_t>(versions.front().first) << 16) |
            (static_cast<std::uint32_t>(versions.front().second)));
    }

    {
        // Required
        auto const iter = h.find ("Public-Key");
        if (iter == h.end())
            return boost::none;
        auto const pk = parseBase58<PublicKey>(
            TokenType::TOKEN_NODE_PUBLIC, iter->value().to_string());
        if (!pk)
            return boost::none;
        hello.set_nodepublic (iter->value().to_string());
    }

    {
        // Required
        auto const iter = h.find ("Session-Signature");
        if (iter == h.end())
            return boost::none;
        // TODO Security Review
        hello.set_nodeproof (beast::detail::base64_decode (iter->value().to_string()));
    }

    {
        auto const iter = h.find (request ?
            "User-Agent" : "Server");
        if (iter != h.end())
            hello.set_fullversion (iter->value().to_string());
    }

    {
        auto const iter = h.find ("Network-Time");
        if (iter != h.end())
        {
            std::uint64_t nettime;
            if (! beast::lexicalCastChecked(nettime, iter->value().to_string()))
                return boost::none;
            hello.set_nettime (nettime);
        }
    }

    {
        auto const iter = h.find ("Ledger");
        if (iter != h.end())
        {
            LedgerIndex ledgerIndex;
            if (! beast::lexicalCastChecked(ledgerIndex, iter->value().to_string()))
                return boost::none;
            hello.set_ledgerindex (ledgerIndex);
        }
    }

    {
        auto const iter = h.find ("Closed-Ledger");
        if (iter != h.end())
            hello.set_ledgerclosed (beast::detail::base64_decode (iter->value().to_string()));
    }

    {
        auto const iter = h.find ("Previous-Ledger");
        if (iter != h.end())
            hello.set_ledgerprevious (beast::detail::base64_decode (iter->value().to_string()));
    }

    {
        auto const iter = h.find ("Local-IP");
        if (iter != h.end())
        {
            bool valid;
            beast::IP::Address address;
            std::tie (address, valid) =
                beast::IP::Address::from_string (iter->value().to_string());
            if (!valid)
                return boost::none;
            if (address.is_v4())
                hello.set_local_ip(address.to_v4().value);
        }
    }

    {
        auto const iter = h.find ("Remote-IP");
        if (iter != h.end())
        {
            bool valid;
            beast::IP::Address address;
            std::tie (address, valid) =
                beast::IP::Address::from_string (iter->value().to_string());
            if (!valid)
                return boost::none;
            if (address.is_v4())
                hello.set_remote_ip(address.to_v4().value);
        }
    }

    return hello;
}

boost::optional<PublicKey>
verifyHello (protocol::TMHello const& h,
    uint256 const& sharedValue,
    beast::IP::Address public_ip,
    beast::IP::Endpoint remote,
    beast::Journal journal,
    Application& app)
{
    if (h.has_nettime ())
    {
        auto const ourTime = app.timeKeeper().now().time_since_epoch().count();
        auto const minTime = ourTime - clockToleranceDeltaSeconds;
        auto const maxTime = ourTime + clockToleranceDeltaSeconds;

        if (h.nettime () > maxTime)
        {
            JLOG(journal.info()) <<
                "Clock for is off by +" << h.nettime() - ourTime;
            return boost::none;
        }

        if (h.nettime () < minTime)
        {
            JLOG(journal.info()) <<
                "Clock is off by -" << ourTime - h.nettime();
            return boost::none;
        }

        JLOG(journal.trace()) <<
            "Connect: time offset " <<
            static_cast<std::int64_t>(ourTime) - h.nettime();
    }

    if (h.protoversionmin () > to_packed (BuildInfo::getCurrentProtocol()))
    {
        JLOG(journal.info()) <<
            "Hello: Disconnect: Protocol mismatch [" <<
            "Peer expects " << to_string (
                BuildInfo::make_protocol(h.protoversion())) <<
            " and we run " << to_string (
                BuildInfo::getCurrentProtocol()) << "]";
        return boost::none;
    }

    auto const publicKey = parseBase58<PublicKey>(
        TokenType::TOKEN_NODE_PUBLIC, h.nodepublic());

    if (! publicKey)
    {
        JLOG(journal.info()) <<
            "Hello: Disconnect: Bad node public key.";
        return boost::none;
    }

    if (*publicKey == app.nodeIdentity().first)
    {
        JLOG(journal.info()) <<
            "Hello: Disconnect: Self connection.";
        return boost::none;
    }

    if (! verifyDigest (*publicKey, sharedValue,
        makeSlice (h.nodeproof()), false))
    {
        // Unable to verify they have private key for claimed public key.
        JLOG(journal.info()) <<
            "Hello: Disconnect: Failed to verify session.";
        return boost::none;
    }

    if (h.has_local_ip () &&
        is_public (remote) &&
        remote.is_v4 () &&
        (remote.to_v4().value != h.local_ip ()))
    {
        // Remote asked us to confirm connection is from
        // correct IP
        JLOG(journal.info()) <<
            "Hello: Disconnect: Peer IP is " <<
            beast::IP::to_string (remote.to_v4())
            << " not " <<
            beast::IP::to_string (beast::IP::AddressV4 (h.local_ip()));
        return boost::none;
    }

    if (h.has_remote_ip() && is_public (remote) &&
        (public_ip != beast::IP::Address()) &&
        (h.remote_ip() != public_ip.to_v4().value))
    {
        // We know our public IP and peer reports connection
        // from some other IP
        JLOG(journal.info()) <<
            "Hello: Disconnect: Our IP is " <<
            beast::IP::to_string (public_ip.to_v4())
            << " not " <<
            beast::IP::to_string (beast::IP::AddressV4 (h.remote_ip()));
        return boost::none;
    }

    return publicKey;
}

}
