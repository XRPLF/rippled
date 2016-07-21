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

#ifndef RIPLPE_CONDITIONS_UTILS_H
#define RIPLPE_CONDITIONS_UTILS_H

#include <ripple/basics/strHex.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <iomanip>
#include <sstream>
#include <utility>

namespace ripple {
namespace cryptoconditions {

inline
std::string
hexstr (std::vector<std::uint8_t> const& data)
{
    std::string s;
    s.reserve (data.size() * 2);

    for (auto d : data)
    {
        s.push_back (charHex (d >> 4));
        s.push_back (charHex (d & 15));
    }

    return s;
}

inline
std::vector<std::uint8_t>
hexblob (std::string const& s)
{
    std::vector<std::uint8_t> tmp;
    tmp.reserve (1 + (s.size () / 2));

    auto iter = s.cbegin ();

    if (s.size () & 1)
    {
        int c = charUnHex (*iter++);

        if (c < 0)
            Throw<std::runtime_error>("Invalid hex in blob");

        tmp.push_back(c);
    }

    while (iter != s.cend ())
    {
        int cHigh = charUnHex (*iter++);

        if (cHigh < 0)
            Throw<std::runtime_error>("Invalid hex in blob");

        int cLow = charUnHex (*iter);

        if (cLow < 0)
            Throw<std::runtime_error>("Invalid hex in blob");

        iter++;

        tmp.push_back (
            static_cast<std::uint8_t>(cHigh << 4) |
            static_cast<std::uint8_t>(cLow));
    }

    return tmp;
}

template <class T>
T parse_decimal(std::string const& s)
{
    T t = 0;

    for (auto const c : s)
    {
        if (c < '0' || c > '9')
            throw std::domain_error ("invalid decimal digit");

        t = (t * 10) + (c - '0');
    }

    return t;
}

template <class T>
T parse_hexadecimal(std::string const& s)
{
    T t = 0;

    for (auto const c : s)
    {
        if (c >= '0' && c <= '9')
            t = (t * 16) + (c - '0');
        else if (c >= 'a' && c <= 'f')
            t = (t * 16) + 10 + (c - 'a');
        else if (c >= 'A' && c <= 'F')
            t = (t * 16) + 10 + (c - 'A');
        else
            throw std::domain_error ("invalid hexadecimal digit");
    }

    return t;
}

template <class Integer>
std::string
to_hex (Integer value)
{
    std::stringstream ss;
    ss << std::hex << value;
    return ss.str();
}

template <class Integer>
std::string
to_dec (Integer value)
{
    std::stringstream ss;
    ss << std::dec << value;
    return ss.str();
}

// ISO/IEC 8825/7 or ITU-T X.696: Octet Encoding Rules
// FIXME: This assumes a little-endian architecture!
namespace oer
{

// Simple conversion: write integer as big-endian byte stream
// This needs to be improved and optimized:
template <class Integer>
std::vector<std::uint8_t>
encode_integer(Integer value)
{
    static_assert (
            std::is_same<Integer, std::uint8_t>::value ||
            std::is_same<Integer, std::uint16_t>::value ||
            std::is_same<Integer, std::uint32_t>::value ||
            std::is_same<Integer, std::uint64_t>::value,
        "encode_integer accepts only std::uint{8,16,32,64}_t");

    std::size_t const n = sizeof(Integer);
    std::size_t k = 0;

    std::vector<std::uint8_t> ret;
    ret.reserve(n);

    while(k++ != n)
    {
        ret.push_back(
            static_cast<std::uint8_t>(
                (value >> ((n - k) * 8))) & 0xFF);
    }

    return ret;
}

// Simple conversion: big-endian byte stream to integer
template <class Integer>
Integer decode_integer(std::vector<std::uint8_t> const& data)
{
    static_assert (
            std::is_same<Integer, std::uint8_t>::value ||
            std::is_same<Integer, std::uint16_t>::value ||
            std::is_same<Integer, std::uint32_t>::value ||
            std::is_same<Integer, std::uint64_t>::value,
        "decode_integer accepts only std::uint{8,16,32,64}_t");

    if (data.size() < sizeof(Integer))
        Throw<std::length_error>("short integer: " + std::to_string(data.size()));

    Integer i = 0;

    for (std::size_t k = 0; k != sizeof(Integer); ++k)
        i = (i << 8) | data[k];

    return i;
}

inline
std::vector<std::uint8_t>
encode_length (std::size_t len)
{
    std::vector<std::uint8_t> ret;

    if (len <= 0x7F)
    {
        ret.reserve (1);
        ret.push_back (static_cast<std::uint8_t>(len & 0x7F));
        return ret;
    }

    // Decide how many bytes we need:
    if (len <= 0xFFFF)
    {
        ret.reserve (3);
        ret.push_back (0x82);
        ret.push_back (static_cast<std::uint8_t>((len >> 8) & 0xFF));
        ret.push_back (static_cast<std::uint8_t>(len & 0xFF));
    }

    if (len <= 0xFFFFFF)
    {
        ret.reserve (4);
        ret.push_back (0x83);
        ret.push_back (static_cast<std::uint8_t>((len >> 16) & 0xFF));
        ret.push_back (static_cast<std::uint8_t>((len >> 8) & 0xFF));
        ret.push_back (static_cast<std::uint8_t>(len & 0xFF));
    }

    if (len <= 0xFFFFFFFF)
    {
        ret.reserve (5);
        ret.push_back (0x84);
        ret.push_back (static_cast<std::uint8_t>((len >> 24) & 0xFF));
        ret.push_back (static_cast<std::uint8_t>((len >> 16) & 0xFF));
        ret.push_back (static_cast<std::uint8_t>((len >> 8) & 0xFF));
        ret.push_back (static_cast<std::uint8_t>(len & 0xFF));
    }

    // Note: OER can represent lengths up to (2^1016) - 1,
    // which is, truly, enough for everyone. We never
    // exceed 2^32.
    Throw<std::length_error>("overlong encoding length: " + std::to_string(len));
}

// A "streambuf" would serve us better here - instead of the
// crazy paired return, we consume data from it and things
// just magically work.
inline
std::pair<std::size_t, std::size_t>
decode_length (std::vector<std::uint8_t>& data)
{
    if (data[0] < 128)
        return { data[0], 1 };

    std::size_t bytes = data[0] & 0x7F;

    if (bytes > 4)
        Throw<std::length_error>("overlong encoded length: " + std::to_string(bytes));

    std::size_t len = 0;

    for (std::size_t i = 0; i < bytes; ++i)
        len = (len << 8) | data[i];

    return { len, bytes + 1 };
}

}
}
}

#endif
