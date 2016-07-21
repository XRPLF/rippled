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

#include <ripple/basics/contract.h>
#include <ripple/conditions/condition.h>
#include <ripple/conditions/fulfillment.h>
#include <ripple/conditions/hashlock.h>
#include <ripple/conditions/impl/utils.h>
#include <boost/regex.hpp>
#include <boost/optional.hpp>
#include <vector>

namespace ripple {
namespace cryptoconditions {

boost::optional<condition_t>
load_condition(std::string const& s)
{
    // CHECKME: custom parser maybe? probably faster but
    // more work and probability of error.

    // TODO: use two regex: one that accepts anything the
    // standard supports and one which accepts only what
    // we support. Parse with both for improved errors?
    static boost::regex const re_current (
            "^"                            // start of line
            "cc\\:"                        // 'cc' for cryptocondition
            "([1-9a-f][0-9a-f]{0,3}|0):"   // type (hexadecimal)
            "([1-9a-f][0-9a-f]{0,15}):"    // feature bitmask (hexadecimal)
            "([a-zA-Z0-9_-]{0,86}):"       // fingerprint (base64url)
            "([1-9][0-9]{0,17}|0)"         // fulfillment length (decimal)
            "$"                            // end of line
        , boost::regex_constants::optimize
    );

    boost::smatch match;

    if (!boost::regex_match (s, match, re_current))
        return boost::none;

    try
    {
        condition_t c;

        c.type = parse_hexadecimal<std::uint16_t> (match[1]);
        c.requires = parse_hexadecimal<std::uint32_t>(match[2]);
        c.fulfillment_length = parse_decimal<std::uint16_t>(match[4]);

        // NIKB-TODO: Avoid copying by decoding directly
        // into the condition's buffer
        auto fingerprint = base64url_decode(match[3]);

        if (fingerprint.size() != c.fingerprint.size())
            throw std::length_error ("Incorrect condition fingerprint length");

        std::copy_n(
            fingerprint.data(),
            fingerprint.size(),
            c.fingerprint.data());

        if (validate (c))
            return c;

        return boost::none;
    }
    catch (std::exception const& x)
    {
        return boost::none;
    }
}

std::string
to_string (condition_t const& c)
{
    return std::string("cc:") +
        to_hex (c.type) + ":" +
        to_hex (c.requires) + ":" +
        base64url_encode(c.fingerprint) + ":" +
        to_dec (c.fulfillment_length);
}

std::vector<std::uint8_t>
to_blob (condition_t const& c)
{
    // NIKB TODO write this
    return { };
}

}

}
