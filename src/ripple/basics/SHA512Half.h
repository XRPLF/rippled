//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2014 Ripple Labs Inc.

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

#ifndef RIPPLE_BASICS_SHA512HALF_H_INCLUDED
#define RIPPLE_BASICS_SHA512HALF_H_INCLUDED

#include <ripple/basics/base_uint.h>
#include <ripple/basics/Slice.h> // needed by most callers
#include <beast/crypto/sha512.h>
#include <beast/hash/hash_append.h>
#include <beast/cxx14/type_traits.h> // <type_traits>

namespace ripple {

namespace detail {

template <bool Secure>
class SHA512HalfHasher
{
private:
    using hasher_type =
        std::conditional_t<Secure,
            beast::sha512_hasher_s,
                beast::sha512_hasher>;

    hasher_type hasher_;

public:
    static beast::endian const endian =
        beast::endian::big;

    using result_type = uint256;

    void
    operator() (void const* data,
        std::size_t size) noexcept
    {
        hasher_(data, size);
    }

    result_type
    finish() noexcept
    {
        result_type digest;
        auto const result = static_cast<
            typename decltype(hasher_)::result_type>
                (hasher_);
        std::memcpy(digest.data(),
            result.data(), 32);
        return digest;
    };

    explicit
    operator result_type() noexcept
    {
        return finish();
    }
};

} // detail

#ifdef _MSC_VER
// Call from main to fix magic statics pre-VS2015
inline
void
sha512_deprecatedMSVCWorkaround()
{
    beast::sha512_hasher h;
    auto const digest = static_cast<
        beast::sha512_hasher::result_type>(h);
}
#endif

using SHA512HalfHasher = detail::SHA512HalfHasher<false>;

/** Returns the SHA512-Half of a series of objects. */
template <class... Args>
SHA512HalfHasher::result_type
sha512Half (Args const&... args)
{
    SHA512HalfHasher h;
    using beast::hash_append;
    hash_append(h, args...);
    return static_cast<typename
        SHA512HalfHasher::result_type>(h);
}

/** Returns the SHA512-Half of a series of objects.

    Postconditions:
        Temporary memory storing copies of
        input messages will be cleared.
*/
template <class... Args>
SHA512HalfHasher::result_type
sha512Half_s (Args const&... args)
{
    detail::SHA512HalfHasher<true> h;
    using beast::hash_append;
    hash_append(h, args...);
    return static_cast<typename
        SHA512HalfHasher::result_type>(h);
}

}

#endif
