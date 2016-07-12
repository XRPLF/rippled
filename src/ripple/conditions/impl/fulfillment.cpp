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

#include <ripple/conditions/condition.h>
#include <ripple/conditions/fulfillment.h>
#include <ripple/conditions/hashlock.h>
#include <ripple/conditions/ed25519.h>
#include <ripple/conditions/impl/utils.h>
#include <boost/regex.hpp>
#include <boost/optional.hpp>
#include <vector>

namespace ripple {
namespace cryptoconditions {

bool
validate (
    fulfillment_t const& f,
    condition_t const& c,
    Slice const& m)
{
    // Fast check: the fulfillment's type must match the
    // conditions's type:
    if (f.type() != c.type)
        return false;

    // The fulfillment must generate the same condition
    // as the one we are checking:
    if (f.condition() != c)
        return false;

    // And the message must validate the fulfillment:
    return f.validate (m);
}

bool
validate (
    fulfillment_t const& f,
    Slice const& m)
{
    return validate (f, f.condition(), m);
}

// Parse a condition from its string form
std::unique_ptr<fulfillment_t>
load_fulfillment (std::string s)
{
    // CHECKME: custom parser maybe? probably faster but
    // more work and probability of error.

    // TODO: use two regex: one that accepts anything the
    // standard supports and one which accepts only what
    // we support. Parse with both for improved errors?
    static boost::regex const re_current (
            "^"                            // start of line
            "cf\\:"                        // 'cf' for fulfillment
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

        if (payload.size() > maxFulfillmentLength)
            throw std::length_error ("The maximum length of a hashlock condition is 65535");

        switch (type)
        {
        case condition_hashlock:
            return std::make_unique<hashlock_t>(payload);

        case condition_ed25519:
            return std::make_unique<ed25519_t>(payload);
        }

        throw std::domain_error (
            "Unknown cryptocondition type " + std::to_string (type));
    }
    catch (std::exception const& x)
    {
        return nullptr;
    }
}

std::unique_ptr<fulfillment_t>
load_fulfillment (Slice const& s)
{
    return nullptr;
}

std::string
to_string (fulfillment_t const& f)
{
    // NIKB TODO: attempt to optimize f.payload()
    return std::string("cf:") + std::to_string(f.type())
        + ":" + base64url_encode (f.payload());
}

Buffer
to_blob (fulfillment_t const& f)
{
    // NIKB TODO optimize this
    std::vector<std::uint8_t> v;

    auto const data = f.payload();

    auto const type = oer::encode_integer<std::uint16_t> (f.type());
    auto const size = oer::encode_length (data.size());

    v.insert (v.end(), type.begin(), type.end());
    v.insert (v.end(), size.begin(), size.end());
    v.insert (v.end(), data.begin(), data.end());

    return { v.data(), v.size() };
}

}
}
