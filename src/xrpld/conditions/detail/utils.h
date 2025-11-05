#ifndef XRPL_CONDITIONS_UTILS_H
#define XRPL_CONDITIONS_UTILS_H

#include <xrpld/conditions/detail/error.h>

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/Slice.h>

#include <boost/dynamic_bitset.hpp>

#include <limits>

namespace ripple {
namespace cryptoconditions {

// A collection of functions to decode binary blobs
// encoded with X.690 Distinguished Encoding Rules.
//
// This is a very trivial decoder and only implements
// the bare minimum needed to support PreimageSha256.
namespace der {

// The preamble encapsulates the DER identifier and
// length octets:
struct Preamble
{
    explicit Preamble() = default;
    std::uint8_t type = 0;
    std::size_t tag = 0;
    std::size_t length = 0;
};

inline bool
isPrimitive(Preamble const& p)
{
    return (p.type & 0x20) == 0;
}

inline bool
isConstructed(Preamble const& p)
{
    return !isPrimitive(p);
}

inline bool
isUniversal(Preamble const& p)
{
    return (p.type & 0xC0) == 0;
}

inline bool
isApplication(Preamble const& p)
{
    return (p.type & 0xC0) == 0x40;
}

inline bool
isContextSpecific(Preamble const& p)
{
    return (p.type & 0xC0) == 0x80;
}

inline bool
isPrivate(Preamble const& p)
{
    return (p.type & 0xC0) == 0xC0;
}

inline Preamble
parsePreamble(Slice& s, std::error_code& ec)
{
    Preamble p;

    if (s.size() < 2)
    {
        ec = error::short_preamble;
        return p;
    }

    p.type = s[0] & 0xE0;
    p.tag = s[0] & 0x1F;

    s += 1;

    if (p.tag == 0x1F)
    {  // Long tag form, which we do not support:
        ec = error::long_tag;
        return p;
    }

    p.length = s[0];
    s += 1;

    if (p.length & 0x80)
    {  // Long form length:
        std::size_t const cnt = p.length & 0x7F;

        if (cnt == 0)
        {
            ec = error::malformed_encoding;
            return p;
        }

        if (cnt > sizeof(std::size_t))
        {
            ec = error::large_size;
            return p;
        }

        if (cnt > s.size())
        {
            ec = error::short_preamble;
            return p;
        }

        p.length = 0;

        for (std::size_t i = 0; i != cnt; ++i)
            p.length = (p.length << 8) + s[i];

        s += cnt;

        if (p.length == 0)
        {
            ec = error::malformed_encoding;
            return p;
        }
    }

    return p;
}

inline Buffer
parseOctetString(Slice& s, std::uint32_t count, std::error_code& ec)
{
    if (count > s.size())
    {
        ec = error::buffer_underfull;
        return {};
    }

    if (count > 65535)
    {
        ec = error::large_size;
        return {};
    }

    Buffer b(s.data(), count);
    s += count;
    return b;
}

template <class Integer>
Integer
parseInteger(Slice& s, std::size_t count, std::error_code& ec)
{
    Integer v{0};

    if (s.empty())
    {
        // can never have zero sized integers
        ec = error::malformed_encoding;
        return v;
    }

    if (count > s.size())
    {
        ec = error::buffer_underfull;
        return v;
    }

    bool const isSigned = std::numeric_limits<Integer>::is_signed;
    // unsigned types may have a leading zero octet
    size_t const maxLength = isSigned ? sizeof(Integer) : sizeof(Integer) + 1;
    if (count > maxLength)
    {
        ec = error::large_size;
        return v;
    }

    if (!isSigned && (s[0] & (1 << 7)))
    {
        // trying to decode a negative number into a positive value
        ec = error::malformed_encoding;
        return v;
    }

    if (!isSigned && count == sizeof(Integer) + 1 && s[0])
    {
        // since integers are coded as two's complement, the first byte may
        // be zero for unsigned reps
        ec = error::malformed_encoding;
        return v;
    }

    v = 0;
    for (size_t i = 0; i < count; ++i)
        v = (v << 8) | (s[i] & 0xff);

    if (isSigned && (s[0] & (1 << 7)))
    {
        for (int i = count; i < sizeof(Integer); ++i)
            v |= (Integer(0xff) << (8 * i));
    }
    s += count;
    return v;
}

}  // namespace der
}  // namespace cryptoconditions
}  // namespace ripple

#endif
