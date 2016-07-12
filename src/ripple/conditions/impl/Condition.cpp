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
#include <ripple/conditions/Condition.h>
#include <ripple/conditions/Fulfillment.h>
#include <ripple/conditions/impl/utils.h>
#include <boost/regex.hpp>
#include <boost/optional.hpp>
#include <vector>
#include <iostream>

namespace ripple {
namespace cryptoconditions {

boost::optional<Condition>
loadCondition(std::string const& s)
{
    static boost::regex const re_current (
            "^"                            // start of line
            "cc:"                          // 'cc' for cryptocondition
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
        Condition c;

        c.type = parse_hexadecimal<std::uint16_t> (match[1]);

        if (!isCondition (c.type))
            return boost::none;

        c.featureBitmask = parse_hexadecimal<std::uint32_t>(match[2]);
        c.maxFulfillmentLength = parse_decimal<std::uint16_t>(match[4]);

        if (c.maxFulfillmentLength > maxSupportedFulfillmentLength)
            return boost::none;

        // TODO: Avoid copying by decoding directly
        // into the condition's buffer
        auto fingerprint = base64url_decode(match[3]);

        if (fingerprint.size() != c.fingerprint.size())
            return boost::none;

        std::memcpy(
            c.fingerprint.data(),
            fingerprint.data(),
            fingerprint.size());

        return c;
    }
    catch (std::exception const&)
    {
        return boost::none;
    }
}

boost::optional<Condition>
loadCondition(Slice s)
{
    if (s.empty())
        return boost::none;

    try
    {
        auto start = s.data();
        auto finish = s.data() + s.size();

        Condition c;

        std::tie (start, c.type) =
            oer::decode_integer<std::uint16_t> (
                start, finish);

        if (!isCondition (c.type))
            return boost::none;

        std::tie (start, c.featureBitmask) =
            oer::decode_varuint<std::uint32_t> (
                start, finish);

        {
            std::size_t len;

            std::tie (start, len) =
                oer::decode_length (start, finish);

            // Incorrect signature length
            if (len != c.fingerprint.size())
                return boost::none;

            // Short buffer
            if (std::distance (start, finish) < len)
                return boost::none;

            auto p = c.fingerprint.data();

            while (len--)
                *p++ = *start++;
        }

        if (start == finish)
            return boost::none;

        std::tie (start, c.maxFulfillmentLength) =
            oer::decode_varuint<std::uint16_t> (
                start, finish);

        // The maximum supported length of a fulfillment is
        // the largest allowable value, so checking here is
        // not helpful.
        return c;
    }
    catch (std::exception const&)
    {
        return boost::none;
    }
}

std::string
to_string (Condition const& c)
{
    return std::string("cc:") +
        to_hex (c.type) + ":" +
        to_hex (c.featureBitmask) + ":" +
        base64url_encode(c.fingerprint) + ":" +
        to_dec (c.maxFulfillmentLength);
}

std::vector<std::uint8_t>
to_blob (Condition const& c)
{
    // TODO: optimize this
    std::vector<std::uint8_t> v;
    v.reserve (48);

    oer::encode_integer (
        c.type,
        std::back_inserter(v));

    oer::encode_varuint (
        c.featureBitmask,
        std::back_inserter(v));

    oer::encode_octetstring (
        c.fingerprint.size(),
        c.fingerprint.begin(),
        c.fingerprint.end(),
        std::back_inserter(v));

    oer::encode_varuint (
        c.maxFulfillmentLength,
        std::back_inserter(v));

    return v;
}

}

}
