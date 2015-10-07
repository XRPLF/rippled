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

#ifndef RIPPLE_PROTOCOL_RIPPLEPUBLICKEY_H_INCLUDED
#define RIPPLE_PROTOCOL_RIPPLEPUBLICKEY_H_INCLUDED

#include <ripple/crypto/Base58.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <ostream>
#include <stdexcept>
#include <type_traits>

namespace ripple {

// Simplified public key that avoids the complexities of RippleAddress
class RipplePublicKey
{
private:
    std::array<std::uint8_t, 33> data_;

public:
    /** Construct from a range of unsigned char. */
    template <class FwdIt, class = std::enable_if_t<std::is_same<unsigned char,
        typename std::iterator_traits<FwdIt>::value_type>::value>>
    RipplePublicKey (FwdIt first, FwdIt last);

    template <class = void>
    std::string
    to_string() const;

    friend
    bool
    operator< (RipplePublicKey const& lhs, RipplePublicKey const& rhs)
    {
        return lhs.data_ < rhs.data_;
    }

    friend
    bool
    operator== (RipplePublicKey const& lhs, RipplePublicKey const& rhs)
    {
        return lhs.data_ == rhs.data_;
    }
};

template <class FwdIt, class>
RipplePublicKey::RipplePublicKey (FwdIt first, FwdIt last)
{
    assert(std::distance(first, last) == data_.size());
    // VFALCO TODO Use 4-arg copy from C++14
    std::copy (first, last, data_.begin());
}

template <class>
std::string
RipplePublicKey::to_string() const
{
    // The expanded form of the key is:
    //  <type> <key> <checksum>
    std::array <std::uint8_t, 1 + 33 + 4> e;
    e[0] = 28; // node public key type
    std::copy (data_.begin(), data_.end(), e.begin() + 1);
    Base58::fourbyte_hash256 (&*(e.begin() + 34), e.data(), 34);
    // Convert key + checksum to little endian with an extra pad byte
    std::array <std::uint8_t, 4 + 33 + 1 + 1> le;
    std::reverse_copy (e.begin(), e.end(), le.begin());
    le.back() = 0; // make BIGNUM positive
    return Base58::raw_encode (le.data(),
        le.data() + le.size(), Base58::getRippleAlphabet());
}

inline
std::ostream&
operator<< (std::ostream& os, RipplePublicKey const& k)
{
    return os << k.to_string();
}

}

#endif
