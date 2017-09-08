//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.

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

#ifndef RIPPLE_CONDITIONS_DERPRIMITIVETRAITS_H
#define RIPPLE_CONDITIONS_DERPRIMITIVETRAITS_H

#include <ripple/basics/Buffer.h>
#include <ripple/basics/Slice.h>
#include <ripple/conditions/impl/error.h>
#include <ripple/conditions/impl/DerTraits.h> // must come before encoder and decoder 
#include <ripple/conditions/impl/DerCoder.h>

#include <algorithm>
#include <array>
#include <bitset>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

#include <boost/container/small_vector.hpp>
#include <boost/optional.hpp>

namespace ripple {
namespace cryptoconditions {
namespace der {

/** base class for DerCoderTraits for integer types

    @see {@link #DerCoderTraits}
*/
struct IntegerTraits
{
    constexpr static
    ClassId
    classId()
    {
        return ClassId::universal;
    }

    constexpr static
    GroupType
    groupType()
    {
        return GroupType::integer;
    }

    static
    boost::optional<std::uint8_t> const&
    tagNum()
    {
        static boost::optional<std::uint8_t> tn{tagInteger};
        return tn;
    }

    template <class T>
    static
    std::uint8_t
    tagNum(T)
    {
        return tagInteger;
    }

    constexpr static
    bool
    primitive()
    {
        return true;
    }

    template <class T>
    static
    std::uint64_t
    length(T const& v)
    {
        const auto isSigned = std::numeric_limits<T>::is_signed;
        if (!v || (isSigned && v == -1))
            return 1;

        std::uint64_t n = sizeof(v);
        signed char toSkip = (isSigned && v < 0) ? 0xff : 0;
        // skip leading 0xff for negative signed, otherwise skip leading zeros
        // when skipping 0xff, the next octet's high bit must be set
        // when skipping 0, the first octet's high bit must not be set
        while (n--)
        {
            auto const c = static_cast<signed char>((v >> (n * 8)) & 0xff);
            if (c == toSkip &&
                !(isSigned && v < 0 && n &&
                  (static_cast<signed char>((v >> ((n - 1) * 8)) & 0xff) >= 0)))
                continue;

            if (v > 0 && c < 0)
                return n + 2;
            else
                return n + 1;
        }
        assert(0);  // will never happen
        return 1;
    }

    template <class T>
    static
    std::uint64_t
    length(
        T const& v,
        boost::optional<GroupType> const& parentGroupType,
        TagMode encoderTagMode,
        TraitsCache& traitsCache)
    {
        return length(v);
    }

    template <class T>
    static
    void
    encode(Encoder& s, T v)
    {
        if (s.subgroups_.empty())
        {
            s.ec_ = make_error_code(Error::logicError);
            return;
        }

        auto& parentSlice = s.parentSlice();

        if (!v)
        {
            if (parentSlice.empty())
            {
                s.ec_ = make_error_code(Error::logicError);
                return;
            }
            parentSlice.push_back(0);
            return;
        }

        boost::optional<GroupType> parentGroupType;
        if (!s.subgroups_.empty())
            parentGroupType.emplace(s.subgroups_.top().groupType());
        std::size_t n = length(v, parentGroupType, s.tagMode_, s.traitsCache_);
        if (parentSlice.size() != n)
        {
            s.ec_ = make_error_code(Error::logicError);
            return;
        }
        while (n--)
        {
            if (n >= sizeof(T))
                parentSlice.push_back(static_cast<char>(0));
            else
                parentSlice.push_back(static_cast<char>((v >> (n * 8)) & 0xFF));
        }
    }

    template <class T>
    static
    void
    decode(Decoder& decoder, T& v)
    {
        auto& slice = decoder.parentSlice();
        std::error_code& ec = decoder.ec_;

        if (slice.empty())
        {
            // can never have zero sized integers
            ec = make_error_code(Error::contentLengthMismatch);
            return;
        }

        const bool isSigned = std::numeric_limits<T>::is_signed;
        // unsigned types may have a leading zero octet
        const size_t maxLength = isSigned ? sizeof(T) : sizeof(T) + 1;
        if (slice.size() > maxLength)
        {
            ec = make_error_code(Error::integerBounds);
            return;
        }

        if (!isSigned && (slice[0] & (1 << 7)))
        {
            // trying to decode a negative number into a positive value
            ec = make_error_code(Error::integerBounds);
            return;
        }

        if (!isSigned && slice.size() == sizeof(T) + 1 && slice[0])
        {
            // since integers are coded as two's complement, the first byte may
            // be zero for unsigned reps
            ec = make_error_code(Error::integerBounds);
            return;
        }

        v = 0;
        for (size_t i = 0; i < slice.size(); ++i)
            v = (v << 8) | (slice[i] & 0xff);

        if (isSigned && (slice[0] & (1 << 7)))
        {
            for (int i = slice.size(); i < sizeof(T); ++i)
                v |= (T(0xff) << (8 * i));
        }
        slice += slice.size();
    }

    template <class T>
    static
    int
    compare(T const& lhs, T const& rhs, TraitsCache& traitsCache)
    {
        if (lhs >= 0 && rhs >= 0)
        {
            // fast common case
            // since the length is encoded, comparing the values directly will be
            // the same as comparing the encoded values
            return (lhs > rhs) - (lhs < rhs);
        }
        auto const lhsL = length(lhs);
        auto const rhsL = length(rhs);
        if (lhsL != rhsL)
        {
            if (lhsL < rhsL)
                return -1;
            return 1;
        }

        // lengths are equal
        auto n = std::min<T>(lhsL, sizeof(T) - 1);
        while (n--)
        {
            auto const lhsV = (static_cast<unsigned char>((lhs >> (n * 8)) & 0xFF));
            auto const rhsV = (static_cast<unsigned char>((rhs >> (n * 8)) & 0xFF));
            if (lhsV != rhsV)
            {
                if (lhsV < rhsV)
                    return -1;
                return 1;
            }
        }

        return 0;
    }
};

template <>
struct DerCoderTraits<std::uint8_t> : IntegerTraits
{
};
template <>
struct DerCoderTraits<std::uint16_t> : IntegerTraits
{
};
template <>
struct DerCoderTraits<std::uint32_t> : IntegerTraits
{
};
template <>
struct DerCoderTraits<std::uint64_t> : IntegerTraits
{
};
template <>
struct DerCoderTraits<std::int8_t> : IntegerTraits
{
};
template <>
struct DerCoderTraits<std::int16_t> : IntegerTraits
{
};
template <>
struct DerCoderTraits<std::int32_t> : IntegerTraits
{
};
template <>
struct DerCoderTraits<std::int64_t> : IntegerTraits
{
};

//------------------------------------------------------------------------------

/** base class for DerCoderTraits for types that will be coded as ASN.1 octet
    strings

    @see {@link #DerCoderTraits}

    @note this includes std::string and std::array<std::uintt_t, N>, Buffer, and
    boost small_vector<std::uint8_t, ...>
*/
struct OctetStringTraits
{
    constexpr static
    ClassId
    classId()
    {
        return ClassId::universal;
    }

    constexpr static
    GroupType
    groupType()
    {
        return GroupType::octetString;
    }

    static
    boost::optional<std::uint8_t> const&
    tagNum()
    {
        static boost::optional<std::uint8_t> tn{tagOctetString};
        return tn;
    }

    template <class T>
    static
    std::uint8_t
    tagNum(T const&)
    {
        return tagOctetString;
    }

    constexpr static
    bool
    primitive()
    {
        return true;
    }

protected:
    static
    void
    encode(Encoder& encoder, Slice s)
    {
        if (s.empty())
            return;

        auto& parentSlice = encoder.parentSlice();
        if (parentSlice.size() != s.size())
        {
            encoder.ec_ = make_error_code(Error::logicError);
            return;
        }
        memcpy(parentSlice.data(), s.data(), s.size());
        parentSlice += s.size();
    }

    static
    void
    decode(Decoder& decoder, void* dstData, std::size_t dstSize)
    {
        auto& slice = decoder.parentSlice();
        std::error_code& ec = decoder.ec_;

        if (dstSize != slice.size())
        {
            ec = make_error_code(Error::contentLengthMismatch);
            return;
        }

        if (!slice.empty())
            memcpy(dstData, slice.data(), slice.size());

        slice += slice.size();
    }
};

template <>
struct DerCoderTraits<std::string> : OctetStringTraits
{
    static
    void
    encode(Encoder& encoder, std::string const& s)
    {
        OctetStringTraits::encode(encoder, makeSlice(s));
    }

    static
    void
    decode(Decoder& decoder, std::string& v)
    {
        auto& slice = decoder.parentSlice();
        v.resize(slice.size());
        if (!v.empty())
            OctetStringTraits::decode(decoder, &v[0], v.size());
    }

    static
    std::uint64_t
    length(
        std::string const& v,
        boost::optional<GroupType> const& parentGroupType,
        TagMode encoderTagMode,
        TraitsCache& traitsCache)
    {
        return v.size();
    }

    static
    int
    compare(
        std::string const& lhs,
        std::string const& rhs,
        TraitsCache& traitsCache)
    {
        auto const lhsL = lhs.size();
        auto const rhsL = rhs.size();
        if (lhsL != rhsL)
        {
            if (lhsL < rhsL)
                return -1;
            return 1;
        }
        return lhs.compare(rhs);
    }
};

template <std::uint64_t S>
struct DerCoderTraits<std::array<std::uint8_t, S>> : OctetStringTraits
{
    static
    void
    encode(Encoder& encoder, std::array<std::uint8_t, S> const& s)
    {
        OctetStringTraits::encode(encoder, makeSlice(s));
    }

    static
    void
    decode(Decoder& decoder, std::array<std::uint8_t, S>& v)
    {
        OctetStringTraits::decode(decoder, v.data(), v.size());
    }

    static
    std::uint64_t
    length(
        std::array<std::uint8_t, S> const& v,
        boost::optional<GroupType> const& parentGroupType,
        TagMode encoderTagMode,
        TraitsCache& traitsCache)
    {
        return S;
    }

    static
    int
    compare(
        std::array<std::uint8_t, S> const& lhs,
        std::array<std::uint8_t, S> const& rhs,
        TraitsCache& traitsCache)
    {
        for(size_t i=0; i<S; ++i)
        {
            if (lhs[i] != rhs[i])
            {
                if (lhs[i] < rhs[i])
                    return -1;
                return 1;
            }
        }
        return 0;
    }
};

template <std::size_t S>
struct DerCoderTraits<boost::container::small_vector<std::uint8_t, S>> : OctetStringTraits
{
    static void
    encode(
        Encoder& encoder,
        boost::container::small_vector<std::uint8_t, S> const& s)
    {
        OctetStringTraits::encode(encoder, makeSlice(s));
    }

    static
    void
    decode(Decoder& decoder, boost::container::small_vector<std::uint8_t, S>& v)
    {
        auto& slice = decoder.parentSlice();
        v.resize(slice.size());
        if (!v.empty())
            OctetStringTraits::decode(decoder, v.data(), v.size());
    }

    static
    std::uint64_t
    length(
        boost::container::small_vector<std::uint8_t, S> const& v,
        boost::optional<GroupType> const& parentGroupType,
        TagMode encoderTagMode,
        TraitsCache& traitsCache)
    {
        return v.size();
    }

    static
    int
    compare(
        boost::container::small_vector<std::uint8_t, S> const& lhs,
        boost::container::small_vector<std::uint8_t, S> const& rhs,
        TraitsCache& traitsCache)
    {
        if (lhs.size() != rhs.size())
        {
            if (lhs.size() < rhs.size())
                return -1;
            return 1;
        }
        auto const s = lhs.size();
        for (size_t i = 0; i < s; ++i)
        {
            if (lhs[i] != rhs[i])
            {
                if (lhs[i] < rhs[i])
                    return -1;
                return 1;
            }
        }
        return 0;
    }
};

template <>
struct DerCoderTraits<Buffer> : OctetStringTraits
{
    static
    void
    encode(Encoder& encoder, Buffer const& b)
    {
        OctetStringTraits::encode(encoder, b);
    }

    static
    void
    decode(Decoder& decoder, Buffer& v)
    {
        auto& slice = decoder.parentSlice();
        v.alloc(slice.size());
        if (!v.empty())
            OctetStringTraits::decode(decoder, v.data(), v.size());
    }

    static
    std::uint64_t
    length(
        Buffer const& v,
        boost::optional<GroupType> const& parentGroupType,
        TagMode encoderTagMode,
        TraitsCache& traitsCache)
    {
        return v.size();
    }

    static
    int
    compare(Buffer const& lhs, Buffer const& rhs, TraitsCache& traitsCache)
    {
        if (lhs.size() != rhs.size())
        {
            if (lhs.size() < rhs.size())
                return -1;
            return 1;
        }
        auto const s = lhs.size();
        auto const lhsD = lhs.data();
        auto const rhsD = rhs.data();
        for (size_t i = 0; i < s; ++i)
        {
            if (lhsD[i] != rhsD[i])
            {
                if (lhsD[i] < rhsD[i])
                    return -1;
                return 1;
            }
        }
        return 0;
    }
};

/** Wrapper for a size constrained DER OctetString

    The size of the string must be equal to the specified constraint.
 */
template <class T>
struct OctetStringCheckEqualSize
{
    T& col_;
    std::size_t constraint_;
    OctetStringCheckEqualSize(T& col, std::size_t s) : col_{col}, constraint_{s}
    {
    }
};

/** Wrapper for a size constrained DER OctetString

    The size of the string must be less than the specified constraint.
 */
template <class T>
struct OctetStringCheckLessSize
{
    T& col_;
    std::size_t constraint_;
    OctetStringCheckLessSize(T& col, std::size_t s) : col_{col}, constraint_{s}
    {
    }
};

/** convenience function to create an equal-size constrained octet string

    @note the template parameter T must be one of the types OctetStringTraits is
          specialized on. @see {@link #OctetStringTraits}
*/
template <class T>
OctetStringCheckEqualSize<T>
make_octet_string_check_equal(T& t, std::size_t s)
{
    return OctetStringCheckEqualSize<T>(t, s);
}

/** convenience function to create a "less size" constrained octet string

    @note the template parameter T must be one of the types OctetStringTraits is
          specialized on. @see {@link #OctetStringTraits}
*/
template <class T>
OctetStringCheckLessSize<T>
make_octet_string_check_less(T& t, std::size_t s)
{
    return OctetStringCheckLessSize<T>(t, s);
}

/** DerCoderTraits for types that will be coded as "equal size" constrained
    ASN.1 octet strings

    @see {@link #DerCoderTraits}
    @see {@link #make_octet_string_check_equal}
*/
template <class T>
struct DerCoderTraits<OctetStringCheckEqualSize<T>> : OctetStringTraits
{
    static
    void
    encode(Encoder& encoder, OctetStringCheckEqualSize<T> const& v)
    {
        DerCoderTraits<T>::encode(encoder, v.col_);
    }

    static
    void
    decode(Decoder& decoder, OctetStringCheckEqualSize<T>& v)
    {
        auto& slice = decoder.parentSlice();
        if (slice.size() != v.constraint_)
        {
            decoder.ec_ = make_error_code(Error::contentLengthMismatch);
            return;
        }
        DerCoderTraits<T>::decode(decoder, v.col_);
    }

    static
    std::uint64_t
    length(
        OctetStringCheckEqualSize<T> const& v,
        boost::optional<GroupType> const& parentGroupType,
        TagMode encoderTagMode,
        TraitsCache& traitsCache)
    {
        return DerCoderTraits<T>::length(v.col_, parentGroupType, encoderTagMode, traitsCache);
    }

    static
    int
    compare(T const& lhs, T const& rhs, TraitsCache& traitsCache)
    {
        return DerCoderTraits<T>::compare(lhs, rhs, traitsCache);
    }
};

/** DerCoderTraits for types that will be coded as "less size" constrained ASN.1
    octet strings

    @see {@link #DerCoderTraits}
*/
template <class T>
struct DerCoderTraits<OctetStringCheckLessSize<T>> : OctetStringTraits
{
    static
    void
    encode(Encoder& encoder, OctetStringCheckLessSize<T> const& v)
    {
        DerCoderTraits<T>::encode(encoder, v.col_);
    }

    static
    void
    decode(Decoder& decoder, OctetStringCheckLessSize<T>& v)
    {
        auto& slice = decoder.parentSlice();
        if (slice.size() > v.constraint_)
        {
            // Return unsupported rather than contentLengthMismatch
            // because this constraint is an implementation limit rather
            // than a parser constraint
            decoder.ec_ = make_error_code(Error::unsupported);
            return;
        }
        DerCoderTraits<T>::decode(decoder, v.col_);
    }

    static
    std::uint64_t
    length(
        OctetStringCheckLessSize<T> const& v,
        boost::optional<GroupType> const& parentGroupType,
        TagMode encoderTagMode,
        TraitsCache& traitsCache)
    {
        return DerCoderTraits<T>::length(
            v.col_, parentGroupType, encoderTagMode, traitsCache);
    }

    static
    int
    compare(T const& lhs, T const& rhs, TraitsCache& traitsCache)
    {
        return DerCoderTraits<T>::compare(lhs, rhs, traitsCache);
    }
};

//------------------------------------------------------------------------------

/** DerCoderTraits for std::bitset

    bitsets will be coded as ans.1 bitStrings

    @see {@link #DerCoderTraits}
    @see {@link #make_octet_string_check_less}
*/
template <std::size_t S>
struct DerCoderTraits<std::bitset<S>>
{
    constexpr static std::uint8_t mod8 = S % 8;
    constexpr static std::uint8_t const minUnusedBits = mod8 ? 8 - mod8 : 0;
    constexpr static std::uint8_t const maxBytes = mod8 ? 1 + S / 8 : S / 8;

    constexpr static
    GroupType
    groupType()
    {
        return GroupType::bitString;
    }

    constexpr static
    ClassId
    classId()
    {
        return ClassId::universal;
    }
    
    static
    boost::optional<std::uint8_t> const&
    tagNum()
    {
        static boost::optional<std::uint8_t> tn{tagBitString};
        return tn;
    }

    static
    std::uint8_t
    tagNum(std::bitset<S> const&)
    {
        return tagBitString;
    }

    constexpr static
    bool
    primitive()
    {
        return true;
    }

    static
    std::uint8_t
    reverseBits(std::uint8_t b)
    {
        static constexpr std::uint8_t lut[256] = 
        {
          0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0, 0x10, 0x90, 0x50, 0xD0, 0x30, 0xB0, 0x70, 0xF0, 
          0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8, 0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8, 
          0x04, 0x84, 0x44, 0xC4, 0x24, 0xA4, 0x64, 0xE4, 0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4, 
          0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC, 0x1C, 0x9C, 0x5C, 0xDC, 0x3C, 0xBC, 0x7C, 0xFC, 
          0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2, 0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2, 
          0x0A, 0x8A, 0x4A, 0xCA, 0x2A, 0xAA, 0x6A, 0xEA, 0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA,
          0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6, 0x16, 0x96, 0x56, 0xD6, 0x36, 0xB6, 0x76, 0xF6, 
          0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE, 0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE,
          0x01, 0x81, 0x41, 0xC1, 0x21, 0xA1, 0x61, 0xE1, 0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1,
          0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9, 0x19, 0x99, 0x59, 0xD9, 0x39, 0xB9, 0x79, 0xF9, 
          0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5, 0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5,
          0x0D, 0x8D, 0x4D, 0xCD, 0x2D, 0xAD, 0x6D, 0xED, 0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD,
          0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3, 0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3, 
          0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB, 0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB,
          0x07, 0x87, 0x47, 0xC7, 0x27, 0xA7, 0x67, 0xE7, 0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7, 
          0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF, 0x1F, 0x9F, 0x5F, 0xDF, 0x3F, 0xBF, 0x7F, 0xFF
        };
        return lut[b];
    }

    /** return the number of leading zero bytes before the last byte

        @note If no bits are set on a 64-bit integer, this function returns 7
        _not_ 8 because DER will always consider the last byte, even if it is
        zero.
     */
    static
    std::uint64_t
    numLeadingZeroBytes(std::bitset<S> const& s)
    {
        auto const result = numLeadingZeroChunks<8>(s.to_ulong(), maxBytes);
        // Always consider the last byte, even if it is zero
        return std::min<std::uint64_t>(result, maxBytes - 1);
    }

    static
    std::uint8_t
    numUnusedBits(
        std::bitset<S> const& s,
        std::size_t leadingZeroBytes)
    {
        // b is first non-zero byte
        auto const bits = s.to_ulong ();
        std::uint8_t const b =
            (bits >> (maxBytes - leadingZeroBytes - 1) * 8) & 0xff;
        if (b & 0x80)
            return 0;
        if (b & 0x40)
            return 1;
        if (b & 0x20)
            return 2;
        if (b & 0x10)
            return 3;
        if (b & 0x08)
            return 4;
        if (b & 0x04)
            return 5;
        if (b & 0x02)
            return 6;
        if (b & 0x01)
            return 7;
        // DER always considers the last bit, even if no bits are set
        return 7;
    }

    static
    void
    encode(Encoder& encoder, std::bitset<S> const& s)
    {
        static_assert(
            maxBytes > 0 && maxBytes <= sizeof(unsigned long),
            "Unsupported bitset size");

        auto& parentSlice = encoder.parentSlice();
        auto const bits = s.to_ulong();

        if (bits == 0)
        {
            if (parentSlice.size() != 2)
            {
                encoder.ec_ = make_error_code(Error::logicError);
                return;
            }
            parentSlice.push_back(7);
            parentSlice.push_back(0);
            return;
        }

        std::size_t const leadingZeroBytes = numLeadingZeroBytes(s);
        std::uint8_t const unusedBits = numUnusedBits(s, leadingZeroBytes);

        if (parentSlice.size() != 1 + maxBytes - leadingZeroBytes)
        {
            encoder.ec_ = make_error_code(Error::logicError);
            return;
        }

        parentSlice.push_back(unusedBits);

        for (size_t curByte = 0; curByte < maxBytes - leadingZeroBytes; ++curByte)
        {
            uint8_t const v = (bits >> curByte * 8) & 0xff;
            parentSlice.push_back(reverseBits(v));
        }
    }

    static
    void
    decode(Decoder& decoder, std::bitset<S>& v)
    {
        static_assert(
            maxBytes > 0 && maxBytes <= sizeof(unsigned long),
            "Unsupported bitset size");

        auto& slice = decoder.parentSlice();
        std::error_code& ec = decoder.ec_;

        if (slice.empty() || slice.size() > maxBytes + 1)
        {
            ec = make_error_code(Error::contentLengthMismatch);
            return;
        }

        auto const unused = slice[0];
        slice += 1;

        if (unused < minUnusedBits)
        {
            ec = make_error_code(Error::contentLengthMismatch);
            return;
        }

        if (unused >= 8)
        {
            ec = make_error_code(Error::badDerEncoding);
            return;
        }

        unsigned long bits = 0;
        auto const numBytes = slice.size();
        size_t curByteIndex = 0;
        for (; !slice.empty(); ++curByteIndex, slice += 1)
        {
            std::uint8_t const curByte = reverseBits(slice[0]);
            bits |= curByte << (curByteIndex * 8);

            if ((curByteIndex == numBytes - 1) && unused)
            {
                // check last byte for correct zero padding
                std::uint8_t const mask = 0xff & ~((1 << (8 - unused)) - 1);
                if (curByte & mask)
                {
                    // last byte has incorrect padding
                    ec = make_error_code(Error::badDerEncoding);
                    return;
                }
            }
        }

        v = bits;
    }

    static
    std::uint64_t
    length(
        std::bitset<S> const& s,
        boost::optional<GroupType> const& parentGroupType,
        TagMode encoderTagMode,
        TraitsCache& traitsCache)
    {
        static_assert(
            maxBytes > 0 && maxBytes <= sizeof(unsigned long),
            "Unsupported bitset size");
        auto const bits = s.to_ulong();

        if (bits == 0)
        {
            return 2;
        }

        std::size_t const leadingZeroBytes = numLeadingZeroBytes(s);
        // +1 to store unusedBits
        return 1 + maxBytes - leadingZeroBytes;
    }

    static
    int
    compare(
        std::bitset<S> const& lhs,
        std::bitset<S> const& rhs,
        TraitsCache& traitsCache)
    {
        static_assert(
            maxBytes > 0 && maxBytes <= sizeof(unsigned long),
            "Unsupported bitset size");
        unsigned long const bits[2] = {lhs.to_ulong(), rhs.to_ulong()};

        std::size_t const leadingZeroBytes[2]{
            numLeadingZeroBytes (lhs), numLeadingZeroBytes (rhs)};

        if (leadingZeroBytes[0] != leadingZeroBytes[1])
        {
            if (leadingZeroBytes[0] < leadingZeroBytes[1])
                // when leadingZeroBytes is less, size will be greater
                return 1;
            return -1;
        }

        std::uint8_t const unusedBits[2]{
            numUnusedBits (lhs, leadingZeroBytes[0]),
            numUnusedBits (rhs, leadingZeroBytes[1])};

        if (unusedBits[0] != unusedBits[1])
        {
            if (unusedBits[0] < unusedBits[1])
                return -1;
            return 1;
        }

        // leadingZeroBytes and unusedBits are equal
        for (size_t curByte = 0; curByte < maxBytes - leadingZeroBytes[0];
             ++curByte)
        {
            uint8_t const v[2] = {
                reverseBits(static_cast<uint8_t>((bits[0] >> curByte * 8) & 0xff)),
                reverseBits(static_cast<uint8_t>((bits[1] >> curByte * 8) & 0xff))};
            if (v[0] != v[1])
            {
                if (v[0] < v[1])
                    return -1;
                return 1;
            }
        }
        return 0;
    }
};

//------------------------------------------------------------------------------

/** wrapper class for coding c++ collections as ans.1 sets

    @see {@link #SequenceWrapper} for the wrapper for sequences
    @see {@link #make_set} for the convenience factory function

    @note There are two types of collections in ans.1 - sets and sequences.
          Given a c++ collection - i.e. a std::vector - the coders need to know
          if the collection should be coded as a set or a sequence. This class
          is the way to tell the coder which collection to use.
 */
template <class T>
struct SetOfWrapper
{
    using value_type = typename T::value_type;

    T& col_;
    boost::container::small_vector<size_t, 8> sortOrder_;

    /** wrap the collection as a DER set

        @param col Collection to wrap
        @param sorted Flag that determines if the collection is already sorted
     */
    SetOfWrapper(T& col, TraitsCache& traitsCache, bool sorted = false)
        : col_(col), sortOrder_(col.size())
    {
        if (auto const cached = traitsCache.sortOrder(&col))
        {
            sortOrder_ = *cached;
            return;
        }

        // contains the indexes into subChoices_ so if the elements will be
        // sorted if accessed in the order specified by sortIndex_
        std::iota(sortOrder_.begin(), sortOrder_.end(), 0);
        if (!sorted)
        {
            std::sort(
                sortOrder_.begin(),
                sortOrder_.end(),
                [&col, &traitsCache](std::size_t lhs, std::size_t rhs) {
                    using Traits = cryptoconditions::der::DerCoderTraits<
                        std::decay_t<decltype(col[0])>>;
                    return Traits::compare(col[lhs], col[rhs], traitsCache) < 0;
                });
            traitsCache.sortOrder(&col, sortOrder_);
        }
    }
};

//------------------------------------------------------------------------------

/** wrapper class for coding c++ collections as ans.1 sets

    @see {@link #make_sequence} for the convenience factory function
    @see {@link #SetOfWrapper} for the wrapper for sets

    @note There are two types of collections in ans.1 - sets and sequences.
          Given a c++ collection - i.e. a std::vector - the coders need to know
          if the collection should be coded as a set or a sequence. This class
          is the way to tell the coder which collection to use.
 */
template <class T>
struct SequenceOfWrapper
{
    /** the collection being wrapped

       @note col_ may be a homogeneous collection like vector or a heterogeneous
             collection like tuple
    */
    T& col_;
    SequenceOfWrapper(T& col) : col_(col)
    {
    }
};

//------------------------------------------------------------------------------

/** convenience function to wrap a c++ collection as it will be coded as an ASN.1 set

    @param col Collection to wrap
    @param sorted Flag that determines if the collection is already sorted

    @{
 */
template <class T>
SetOfWrapper<T>
make_set(T& t, TraitsCache& traitsCache, bool sorted = false)
{
    return SetOfWrapper<T>(t, traitsCache, sorted);
}

template <class T>
SetOfWrapper<T>
make_set(T& t, Encoder& encoder, bool sorted = false)
{
    return SetOfWrapper<T>(t, encoder.traitsCache_, sorted);
}

template <class T>
SetOfWrapper<T>
make_set(T& t, Decoder& decoder, bool sorted = false)
{
    TraitsCache dummy; // cache traits are not used in decoding
    return SetOfWrapper<T>(t, dummy, sorted);
}
/** @} */

/// convenience function to wrap a c++ collection as it will be coded as an ASN.1 sequence
template <class T>
auto
make_sequence(T& t)
{
    return SequenceOfWrapper<T>(t);
}

/// convenience function to wrap a c++ tuple as it will be coded as an ASN.1 sequence
template <class... T>
auto
make_sequence(std::tuple<T...>& t)
{
    return SequenceOfWrapper<std::tuple<T...>>(t);
}

//------------------------------------------------------------------------------

/** DerCoderTraits for types that will be coded as ASN.1 sets

    @see {@link #DerCoderTraits}
    @see {@link #make_set}
    @see {@link #SetOfWrapper}
*/
template <class T>
struct DerCoderTraits<SetOfWrapper<T>>
{
    constexpr static GroupType
    groupType()
    {
        return GroupType::set;
    }

    constexpr static ClassId
    classId()
    {
        return ClassId::universal;
    }

    static boost::optional<std::uint8_t> const&
    tagNum()
    {
        static boost::optional<std::uint8_t> tn{tagSet};
        return tn;
    }

    static
    std::uint8_t
    tagNum(SetOfWrapper<T> const&)
    {
        return tagSet;
    }

    constexpr static
    bool
    primitive()
    {
        return false;
    }

    static
    void
    encode(Encoder& encoder, SetOfWrapper<T> const& v)
    {
        for(auto const i : v.sortOrder_)
        {
            encoder << v.col_[i];
            if (encoder.ec())
                return;
        }
    }

    static
    void
    decode(Decoder& decoder, SetOfWrapper<T>& v)
    {
        v.col_.clear();

        auto& slice = decoder.parentSlice();

        while (!slice.empty())
        {
            typename T::value_type val{};
            decoder >> val;
            if (decoder.ec())
                return;
            v.col_.emplace_back(std::move(val));
        }
    }

    static
    std::uint64_t
    length(
        SetOfWrapper<T> const& v,
        boost::optional<GroupType> const& parentGroupType,
        TagMode encoderTagMode,
        TraitsCache& traitsCache)
    {
        using ValueTraits = DerCoderTraits<typename T::value_type>;
        std::uint64_t l = 0;
        boost::optional<GroupType> const thisGroupType(groupType());
        std::uint64_t childNum = 0;
        for (auto const& e : v.col_)
        {
            l += totalLength<ValueTraits>(
                e, thisGroupType, encoderTagMode, traitsCache, childNum);
            ++childNum;
        }
        return l;
    }

    static
    int
    compare(
        SetOfWrapper<T> const& lhs,
        SetOfWrapper<T> const& rhs,
        TraitsCache& traitsCache)
    {
        auto const lhsSize = lhs.col_.size();
        auto const rhsSize = rhs.col_.size();

        if (lhsSize != rhsSize)
        {
            if (lhsSize < rhsSize)
                return -1;
            return 1;
        }

        auto const& lhsSortOrder = lhs.sortOrder_;
        auto const& rhsSortOrder = rhs.sortOrder_;

        using elementType = std::decay_t<decltype(lhs.col_[0])>;
        // sizes are equal
        for (size_t i = 0; i < lhsSize; ++i)
        {
            auto const r = DerCoderTraits<elementType>::compare(
                lhs.col_[lhsSortOrder[i]],
                rhs.col_[rhsSortOrder[i]],
                traitsCache);
            if (r != 0)
                return r;
        }

        return (lhsSize > rhsSize) - (lhsSize < rhsSize);
    }
};

//------------------------------------------------------------------------------

/** DerCoderTraits for types that will be coded as ASN.1 sequences

    @see {@link #DerCoderTraits}
    @see {@link #make_sequence}
    @see {@link #SequenceOfWrapper}
*/
template <class T>
struct DerCoderTraits<SequenceOfWrapper<T>>
{
    constexpr static
    GroupType
    groupType()
    {
        return GroupType::sequence;
    }

    constexpr static
    ClassId
    classId()
    {
        return ClassId::universal;
    }

    static
    boost::optional<std::uint8_t> const&
    tagNum()
    {
        static boost::optional<std::uint8_t> tn{tagSequence};
        return tn;
    }

    static
    std::uint8_t
    tagNum(SequenceOfWrapper<T> const&)
    {
        return tagSequence;
    }

    constexpr static
    bool
    primitive()
    {
        return false;
    }

    static
    void
    encode(Encoder& encoder, SequenceOfWrapper<T> const& v)
    {
        for (auto const& e : v.col_)
        {
            encoder << e;
            if (encoder.ec())
                return;
        }
    }

    static
    void
    decode(Decoder& decoder, SequenceOfWrapper<T>& v)
    {
        v.col_.clear();

        auto& slice = decoder.parentSlice();

        while (!slice.empty())
        {
            typename T::value_type val;
            decoder >> val;
            if (decoder.ec())
                return;
            v.col_.emplace_back(std::move(val));
        }
    }

    static
    std::uint64_t
    length(
        SequenceOfWrapper<T> const& v,
        boost::optional<GroupType> const& parentGroupType,
        TagMode encoderTagMode,
        TraitsCache& traitsCache)
    {
        using ValueTraits = DerCoderTraits<typename T::value_type>;
        std::uint64_t l = 0;
        boost::optional<GroupType> thisGroupType(groupType());
        std::uint64_t childNum = 0;
        for (auto const& e : v.col_)
        {
            l += totalLength<ValueTraits>(
                e, thisGroupType, encoderTagMode, traitsCache, childNum);
            ++childNum;
        }
        return l;
    }

    static
    int
    compare(
        SequenceOfWrapper<T> const& lhs,
        SequenceOfWrapper<T> const& rhs,
        TraitsCache& traitsCache)
    {
        auto const lhsSize = lhs.col_.size();
        auto const rhsSize = rhs.col_.size();
        if (lhsSize != rhsSize)
        {
            if (lhsSize < rhsSize)
                return -1;
            return 1;
        }

        // sizes are equal
        using elementType = std::decay_t<decltype(lhs.col_[0])>;
        for (size_t i = 0; i < lhsSize; ++i)
        {
            auto const r = DerCoderTraits<elementType>::compare(
                lhs.col_[i], rhs.col_[i], traitsCache);
            if (r != 0)
                return r;
        }

        return 0;
    }
};

//------------------------------------------------------------------------------

/** DerCoderTraits for std::tuples

    @note tuples will be decoded as ans.1 sequences

    @see {@link #DerCoderTraits}
*/
template <class... Ts>
struct DerCoderTraits<std::tuple<Ts&...>>
{
    using Tuple = std::tuple<Ts&...>;

    constexpr static
    GroupType
    groupType()
    {
        return GroupType::autoSequence;
    }

    constexpr static
    ClassId
    classId()
    {
        return ClassId::universal;
    }

    static
    boost::optional<std::uint8_t> const&
    tagNum()
    {
        static boost::optional<std::uint8_t> tn{tagSequence};
        return tn;
    }

    static
    std::uint8_t
    tagNum(Tuple const&)
    {
        return tagSequence;
    }

    constexpr static bool
    primitive()
    {
        return false;
    }

    template <class F, std::size_t... Is>
    static
    void
    forEachElement(
        Tuple const& elements,
        std::index_sequence<Is...>,
        F&& f)
    {
        // Sean Parent for_each_argument trick
        (void)std::array<int, sizeof...(Ts)>{
            {(f(std::get<Is>(elements)), 0)...}};
    }

    template <class F, std::size_t... Is>
    static
    void
    forEachIndex(
        std::index_sequence<Is...>,
        F&& f)
    {
        // Sean Parent for_each_argument trick
        (void)std::array<int, sizeof...(Ts)>{
            {(f(std::integral_constant<std::size_t, Is>{}), 0)...}};
    }

    template <std::size_t... Is>
    static
    void
    decodeElementsHelper(
        Decoder& decoder,
        Tuple const& elements,
        std::index_sequence<Is...>)
    {
        // Sean Parent for_each_argument trick
        (void)std::array<int, sizeof...(Ts)>{
            {((decoder >> std::get<Is>(elements)), 0)...}};
    }

    static
    void
    encode(Encoder& encoder, Tuple const& elements)
    {
        forEachElement(
            elements,
            std::index_sequence_for<Ts...>{},
            [&encoder](auto const& e) { encoder << e; });
    }

    static
    void
    decode(Decoder& decoder, Tuple const& elements)
    {
        forEachElement(
            elements, std::index_sequence_for<Ts...>{},
            [&decoder](auto& e) {decoder >> e;});
    }

    static
    std::uint64_t
    length(
        Tuple const& elements,
        boost::optional<GroupType> const& parentGroupType,
        TagMode encoderTagMode,
        TraitsCache& traitsCache)
    {
        std::uint64_t l = 0;
        boost::optional<GroupType> thisGroupType(groupType());
        forEachIndex(std::index_sequence_for<Ts...>{}, [&](auto indexParam) {
            // visual studio can't handle index as a constexpr
            constexpr typename decltype(indexParam)::value_type index =
                decltype(indexParam)::value;
            auto const& e = std::get<index>(elements);
            using ElementTraits = DerCoderTraits<std::decay_t<decltype(e)>>;
            std::uint64_t childNum = index;
            l += totalLength<ElementTraits>(
                e, thisGroupType, encoderTagMode, traitsCache, childNum);
        });
        return l;
    }

    template <class T>
    static
    void
    compareElementsHelper(
        T const& lhs,
        T const& rhs,
        TraitsCache& traitsCache,
        int* cmpResult)
    {
        if (*cmpResult)
            return;
        *cmpResult = DerCoderTraits<T>::compare(lhs, rhs, traitsCache);
    }

    template <std::size_t... Is>
    static
    int
    compareElementsHelper(
        Tuple const& lhs,
        Tuple const& rhs,
        TraitsCache& traitsCache,
        std::index_sequence<Is...>)
    {
        int result = 0;
        // Sean Parent for_each_argument trick
        (void)std::array<int, sizeof...(Ts)>{
            {((compareElementsHelper(std::get<Is>(lhs), std::get<Is>(rhs), traitsCache, &result)), 0)...}};
        return result;
    }

    static int
    compare(Tuple const& lhs, Tuple const& rhs, TraitsCache& traitsCache)
    {
        {
            // compare lengths even though the parent tag or tag mode are
            // unknown. Hard coding no parent tag and automatic tag mode
            // will still reveal differences in length.
            auto const lhsL = length(lhs, boost::none, TagMode::automatic, traitsCache);
            auto const rhsL = length(rhs, boost::none, TagMode::automatic, traitsCache);
            if (lhsL != rhsL)
            {
                if (lhsL < rhsL)
                    return -1;
                return 1;
            }
        }
        return compareElementsHelper(
            lhs, rhs, traitsCache, std::index_sequence_for<Ts...>{});
    }
};

}  // der
}  // cryptoconditions
}  // ripple

#endif
