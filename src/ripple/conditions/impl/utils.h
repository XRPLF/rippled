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

#ifndef RIPPLE_CONDITIONS_UTILS_H
#define RIPPLE_CONDITIONS_UTILS_H

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
    std::vector<std::uint8_t> result;
    result.reserve (1 + (s.size () / 2));

    auto iter = s.cbegin ();

    if (s.size () & 1)
    {
        int c = charUnHex (*iter++);

        if (c < 0)
            Throw<std::runtime_error>("Invalid hex in blob");

        result.push_back(c);
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

        result.push_back (
            static_cast<std::uint8_t>(cHigh << 4) |
            static_cast<std::uint8_t>(cLow));
    }

    return result;
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
template <class Integer, class OutputIt>
void
encode_integer(Integer value, OutputIt out)
{
    static_assert (
            std::is_same<Integer, std::uint8_t>::value ||
            std::is_same<Integer, std::uint16_t>::value ||
            std::is_same<Integer, std::uint32_t>::value ||
            std::is_same<Integer, std::uint64_t>::value,
        "encode_integer accepts only std::uint{8,16,32,64}_t");

    std::size_t n = sizeof(Integer);

    while(n--)
    {
        *out++ = static_cast<std::uint8_t>(
            (value >> (n * 8)) & 0xFF);
    }
}

// Simple conversion: big-endian byte stream to integer
template <class Integer, class InputIt>
std::pair<InputIt, Integer>
decode_integer(InputIt begin, InputIt end)
{
    static_assert (
            std::is_same<Integer, std::uint8_t>::value ||
            std::is_same<Integer, std::uint16_t>::value ||
            std::is_same<Integer, std::uint32_t>::value ||
            std::is_same<Integer, std::uint64_t>::value,
        "decode_integer accepts only std::uint{8,16,32,64}_t");

    std::size_t size = std::distance (begin, end);

    if (size < sizeof(Integer))
        Throw<std::length_error>("short integer: " + std::to_string(size));

    Integer res = 0;

    for (std::size_t i = 0; i < sizeof(Integer); ++i)
        res = (res << 8) | *begin++;

    return { begin, res };
}

template <class OutputIt>
inline
OutputIt
encode_length (std::size_t len, OutputIt out)
{
    if (len <= 0x7F)
    {
        *out++ = static_cast<std::uint8_t>(len & 0x7F);
        return out;
    }

    // Decide how many bytes we need:
    if (len <= 0xFFFF)
    {
        *out++ = 0x82;
        *out++ = static_cast<std::uint8_t>((len >> 8) & 0xFF);
        *out++ = static_cast<std::uint8_t>(len & 0xFF);
        return out;
    }

    if (len <= 0xFFFFFF)
    {
        *out++ = 0x83;
        *out++ = static_cast<std::uint8_t>((len >> 16) & 0xFF);
        *out++ = static_cast<std::uint8_t>((len >> 8) & 0xFF);
        *out++ = static_cast<std::uint8_t>(len & 0xFF);
        return out;
    }

    if (len <= 0xFFFFFFFF)
    {
        *out++ = 0x84;
        *out++ = static_cast<std::uint8_t>((len >> 24) & 0xFF);
        *out++ = static_cast<std::uint8_t>((len >> 16) & 0xFF);
        *out++ = static_cast<std::uint8_t>((len >> 8) & 0xFF);
        *out++ = static_cast<std::uint8_t>(len & 0xFF);
        return out;
    }

    // Note: OER can represent lengths up to (2^1016) - 1,
    // which is, truly, enough for everyone. We never
    // exceed 2^32.
    Throw<std::length_error>("overlong encoding length: " + std::to_string(len));
}

// A "streambuf" would serve us better here - instead of the
// crazy paired return, we consume data from it and things
// just magically work.
template <class InputIt>
std::pair<InputIt, std::size_t>
decode_length (InputIt begin, InputIt end)
{
    if (begin == end)
        Throw<std::length_error>("empty buffer");

    std::size_t bytes = *begin++;

    if (bytes < 128)
        return { begin, bytes };

    bytes &= 0x7F;

    if (bytes > 4)
        Throw<std::length_error>("overlong encoded length: " + std::to_string(bytes));

    std::size_t len = 0;

    if (std::distance (begin, end) < bytes)
        Throw<std::length_error>("short encoded length: " + std::to_string(bytes));

    while (bytes--)
        len = (len << 8) | *begin++;

    return { begin, len };
}

/** Encode a fixed-size octet string: OER 2.6 (2) */
template <class InputIt, class OutputIt>
OutputIt
encode_octetstring(InputIt begin, InputIt end, OutputIt out)
{
    while (begin != end)
        *out++ = *begin++;

    return out;
}

/** Encode a dynamic size octet string: OER 2.6 (1) */
inline
std::size_t
predict_octetstring_size(std::size_t size)
{
    // Alternatively, always guess 4 + size and call it a day?
    if (size <= 0x7F)
        return size + 1;

    // Decide how many bytes we need:
    if (size <= 0xFFFF)
        return size + 3;

    if (size <= 0xFFFFFF)
        return size + 4;

    if (size <= 0xFFFFFFFF)
        return size + 5;

    Throw<std::length_error>("overlong encoding length: " + std::to_string(size));
}

/** Encode an dynamic size octet string: OER 2.6 (1) */
template <class InputIt, class OutputIt>
OutputIt
encode_octetstring(std::size_t size, InputIt begin, InputIt end, OutputIt out)
{
    // This will encode the length first, followed by the
    // payload octets:
    return encode_octetstring (
        begin,
        end,
        oer::encode_length (size, out));
}

template <class Integer, class OutputIt>
std::enable_if_t<std::is_unsigned<Integer>::value>
encode_varuint (Integer value, OutputIt out)
{
    auto count = [](Integer n)
    {
        std::size_t c = 0;

        do
        {
            n >>= 8;
            ++c;
        } while (n);

        return c;
    };

    std::size_t c = count (value);

    out = encode_length (c, out);

    while(c--)
    {
        *out++ = static_cast<std::uint8_t>(
            (value >> (c * 8)) & 0xFF);
    }
}

template <class Integer, class InputIt>
std::enable_if_t<std::is_unsigned<Integer>::value, std::pair<InputIt, Integer>>
decode_varuint (InputIt begin, InputIt end)
{
    auto y = decode_length (begin, end);

    if (y.second > sizeof(Integer))
        Throw<std::length_error>("Encoded integer exceeds allowable range: " + std::to_string(y.second));

    Integer x = 0;

    for (std::size_t i = 0; i != y.second; ++i)
        x = (x << 8) + *y.first++;

    return { y.first, x };
}

}
}
}

#endif
