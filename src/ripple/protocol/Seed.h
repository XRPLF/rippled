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

#ifndef RIPPLE_PROTOCOL_SEED_H_INCLUDED
#define RIPPLE_PROTOCOL_SEED_H_INCLUDED

#include <ripple/basics/Slice.h>
#include <ripple/basics/base_uint.h>
#include <ripple/protocol/tokens.h>
#include <boost/optional.hpp>
#include <array>

namespace ripple {

/** Seeds are used to generate deterministic secret keys. */
class Seed
{
private:
    std::array<uint8_t, 16> buf_;

public:
    using const_iterator = std::array<uint8_t, 16>::const_iterator;

    Seed() = delete;

    Seed(Seed const&) = default;
    Seed&
    operator=(Seed const&) = default;

    /** Destroy the seed.
        The buffer will first be securely erased.
    */
    ~Seed();

    /** Construct a seed */
    /** @{ */
    explicit Seed(Slice const& slice);
    explicit Seed(uint128 const& seed);
    /** @} */

    std::uint8_t const*
    data() const
    {
        return buf_.data();
    }

    std::size_t
    size() const
    {
        return buf_.size();
    }

    const_iterator
    begin() const noexcept
    {
        return buf_.begin();
    }

    const_iterator
    cbegin() const noexcept
    {
        return buf_.cbegin();
    }

    const_iterator
    end() const noexcept
    {
        return buf_.end();
    }

    const_iterator
    cend() const noexcept
    {
        return buf_.cend();
    }
};

//------------------------------------------------------------------------------

/** Create a seed using secure random numbers. */
Seed
randomSeed();

/** Generate a seed deterministically.

    The algorithm is specific to Ripple:

        The seed is calculated as the first 128 bits
        of the SHA512-Half of the string text excluding
        any terminating null.

    @note This will not attempt to determine the format of
          the string (e.g. hex or base58).
*/
Seed
generateSeed(std::string const& passPhrase);

/** Parse a Base58 encoded string into a seed */
template <>
boost::optional<Seed>
parseBase58(std::string const& s);

/** Attempt to parse a string as a seed */
boost::optional<Seed>
parseGenericSeed(std::string const& str);

/** Encode a Seed in RFC1751 format */
std::string
seedAs1751(Seed const& seed);

/** Format a seed as a Base58 string */
inline std::string
toBase58(Seed const& seed)
{
    return base58EncodeToken(TokenType::FamilySeed, seed.data(), seed.size());
}

}  // namespace ripple

#endif
