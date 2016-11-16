//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#include <ripple/conditions/Condition.h>
#include <ripple/conditions/Fulfillment.h>
#include <ripple/conditions/PreimageSha256.h>
#include <ripple/conditions/PrefixSha256.h>
#include <ripple/conditions/RsaSha256.h>
#include <ripple/conditions/Ed25519.h>
#include <ripple/conditions/impl/utils.h>
#include <boost/regex.hpp>
#include <boost/optional.hpp>
#include <type_traits>
#include <vector>

namespace ripple {
namespace cryptoconditions {

bool
fulfills (
    Fulfillment const& f,
    Condition const& c)
{
    // Fast check: the fulfillment's type must match the
    // conditions's type:
    if (f.type() != c.type)
        return false;

    // Ensure that the condition is well-formed
    if (!validate (c))
        return false;

    // The fulfillment payload can be no larger than the
    // what the condition allows.
    if (f.payloadSize() > c.maxFulfillmentLength)
        return false;

    return f.condition() == c;
}

bool
validate (
    Fulfillment const& f,
    Condition const& c,
    Slice m)
{
    return fulfills (f, c) && f.validate (m);
}

bool
validateTrigger (
    Fulfillment const& f,
    Condition const& c)
{
    return validate (f, c, {});
}

std::unique_ptr<Fulfillment>
loadFulfillment (std::uint16_t type, Slice payload)
{
    std::unique_ptr<Fulfillment> p;

    switch (type)
    {
    case condition_hashlock:
        p = std::make_unique<PreimageSha256>();
        break;

    case condition_prefix_sha256:
        p = std::make_unique<PrefixSha256>();
        break;

    case condition_rsa_sha256:
        p = std::make_unique<RsaSha256>();
        break;

    case condition_ed25519:
        p = std::make_unique<Ed25519>();
        break;

    default:
        throw std::domain_error (
            "Unknown cryptocondition type " +
                std::to_string (type));
    }

    // If the payload can't be parsed, the load should
    // fail.
    if (p && !p->parsePayload(payload))
        p.reset();

    return p;
}

// Parse a condition from its string form
std::unique_ptr<Fulfillment>
loadFulfillment (std::string const& s)
{
    // CHECKME: custom parser maybe? probably faster but
    // more work and probability of error.

    // TODO: use two regex: one that accepts anything the
    // standard supports and one which accepts only what
    // we support. Parse with both for improved errors?
    static boost::regex const re_current (
            "^"                            // start of line
            "cf:"                          // 'cf' for fulfillment
            "([1-9a-f][0-9a-f]{0,3}|0):"   // type
            "([a-zA-Z0-9_-]*)"             // fulfillment payload (base64url)
            "$"                            // end of line
        , boost::regex_constants::optimize
    );

    try
    {
        boost::smatch match;

        if (!boost::regex_match (s, match, re_current))
            return nullptr;

        std::uint16_t const type =
            parse_hexadecimal<std::uint16_t>(match[1]);

        auto payload = base64url_decode (match[2]);

        if (payload.size() > maxSupportedFulfillmentLength)
            return nullptr;

        return loadFulfillment (type, makeSlice (payload));
    }
    catch (std::exception const&)
    {
        return nullptr;
    }
}

std::unique_ptr<Fulfillment>
loadFulfillment (Slice s)
{
    if (s.empty())
        return nullptr;

    try
    {
        auto start = s.data();
        auto finish = s.data() + s.size();

        std::uint16_t type;
        std::size_t len;

        std::tie (start, type) =
            oer::decode_integer<std::uint16_t> (
                start, finish);

        if (!isCondition (type))
            return nullptr;

        if (start == finish)
            return nullptr;

        std::tie (start, len) =
            oer::decode_length(
                start, finish);

        if (len)
        {
            if (len > maxSupportedFulfillmentLength)
                return nullptr;

            if (std::distance (start, finish) < len)
                return nullptr;
        }

        return loadFulfillment (type, Slice{ start, len });
    }
    catch (std::exception const&)
    {
        return nullptr;
    }
}

std::string
to_string (Fulfillment const& f)
{
    return std::string("cf:") + to_hex(f.type())
        + ":" + base64url_encode (f.payload());
}

std::vector<std::uint8_t>
to_blob (Fulfillment const& f)
{
    // NIKB TODO optimize this
    std::vector<std::uint8_t> v;

    auto const p = f.payload();

    oer::encode_integer (
        f.type(),
        std::back_inserter(v));

    oer::encode_length (
        p.size(), std::back_inserter(v));

    oer::encode_octetstring (
        p.data(),
        p.data() + p.size(),
        std::back_inserter(v));

    return v;
}

}
}
