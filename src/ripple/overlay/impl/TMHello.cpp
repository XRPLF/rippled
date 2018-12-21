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

#include <ripple/overlay/impl/TMHello.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/basics/base64.h>
#include <ripple/basics/safe_cast.h>
#include <ripple/beast/rfc2616.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/protocol/digest.h>
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
    if (result == beast::zero)
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
            TokenType::NodePublic,
            app.nodeIdentity().first));
    h.set_nodeproof (sig.data(), sig.size());
    h.set_testnet (false);

    if (beast::IP::is_public (remote))
    {
        // Connection is to a public IP
        h.set_remote_ip_str (remote.to_string());
        if (! public_ip.is_unspecified())
            h.set_local_ip_str (public_ip.to_string());
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
appendHello (boost::beast::http::fields& h,
    protocol::TMHello const& hello)
{
    //h.append ("Protocol-Versions",...

    h.insert ("Public-Key", hello.nodepublic());

    h.insert ("Session-Signature", base64_encode (
        hello.nodeproof()));

    if (hello.has_nettime())
        h.insert ("Network-Time", std::to_string (hello.nettime()));

    if (hello.has_ledgerindex())
        h.insert ("Ledger", std::to_string (hello.ledgerindex()));

    if (hello.has_ledgerclosed())
        h.insert ("Closed-Ledger", base64_encode (
            hello.ledgerclosed()));

    if (hello.has_ledgerprevious())
        h.insert ("Previous-Ledger", base64_encode (
            hello.ledgerprevious()));

    if (hello.has_local_ip_str())
        h.insert ("Local-IP", hello.local_ip_str());

    if (hello.has_remote_ip())
        h.insert ("Remote-IP", hello.remote_ip_str());
}

std::vector<ProtocolVersion>
parse_ProtocolVersions(boost::beast::string_view const& value)
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
parseHello (bool request, boost::beast::http::fields const& h, beast::Journal journal)
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
            (safe_cast<std::uint32_t>(versions.back().first) << 16) |
            (safe_cast<std::uint32_t>(versions.back().second)));
        hello.set_protoversionmin(
            (safe_cast<std::uint32_t>(versions.front().first) << 16) |
            (safe_cast<std::uint32_t>(versions.front().second)));
    }

    {
        // Required
        auto const iter = h.find ("Public-Key");
        if (iter == h.end())
            return boost::none;
        auto const pk = parseBase58<PublicKey>(
            TokenType::NodePublic, iter->value().to_string());
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
        hello.set_nodeproof (base64_decode (iter->value().to_string()));
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
            hello.set_ledgerclosed (base64_decode (iter->value().to_string()));
    }

    {
        auto const iter = h.find ("Previous-Ledger");
        if (iter != h.end())
            hello.set_ledgerprevious (base64_decode (iter->value().to_string()));
    }

    {
        auto const iter = h.find ("Local-IP");
        if (iter != h.end())
        {
            boost::system::error_code ec;
            auto address =
                beast::IP::Address::from_string (iter->value().to_string(), ec);
            if (ec)
            {
                JLOG(journal.warn()) << "invalid Local-IP: "
                    << iter->value().to_string();
                return boost::none;
            }
            hello.set_local_ip_str(address.to_string());
        }
    }

    {
        auto const iter = h.find ("Remote-IP");
        if (iter != h.end())
        {
            boost::system::error_code ec;
            auto address =
                beast::IP::Address::from_string (iter->value().to_string(), ec);
            if (ec)
            {
                JLOG(journal.warn()) << "invalid Remote-IP: "
                    << iter->value().to_string();
                return boost::none;
            }
            hello.set_remote_ip_str(address.to_string());
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
            safe_cast<std::int64_t>(ourTime) - h.nettime();
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
        TokenType::NodePublic, h.nodepublic());

    if (! publicKey)
    {
        JLOG(journal.info()) <<
            "Hello: Disconnect: Bad node public key.";
        return boost::none;
    }

    if (publicKeyType(*publicKey) != KeyType::secp256k1)
    {
        JLOG(journal.info()) <<
            "Hello: Disconnect: Unsupported public key type.";
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

    if (h.has_local_ip_str () &&
        is_public (remote))
    {
        boost::system::error_code ec;
        auto local_ip =
            beast::IP::Address::from_string (h.local_ip_str(), ec);
        if (ec)
        {
            JLOG(journal.warn()) << "invalid local-ip: " << h.local_ip_str();
            return boost::none;
        }

        if (remote.address() != local_ip)
        {
            // Remote asked us to confirm connection is from correct IP
            JLOG(journal.info()) <<
                "Hello: Disconnect: Peer IP is " << remote.address().to_string()
                << " not " << local_ip.to_string();
            return boost::none;
        }
    }

    if (h.has_remote_ip_str () &&
        is_public (remote) &&
        (! beast::IP::is_unspecified(public_ip)))
    {
        boost::system::error_code ec;
        auto remote_ip =
            beast::IP::Address::from_string (h.remote_ip_str(), ec);
        if (ec)
        {
            JLOG(journal.warn()) << "invalid remote-ip: " << h.remote_ip_str();
            return boost::none;
        }

        if (remote_ip != public_ip)
        {
            // We know our public IP and peer reports connection from some
            // other IP
            JLOG(journal.info()) <<
                "Hello: Disconnect: Our IP is " << public_ip.to_string()
                << " not " << remote_ip.to_string();
            return boost::none;
        }
    }

    return publicKey;
}

}
