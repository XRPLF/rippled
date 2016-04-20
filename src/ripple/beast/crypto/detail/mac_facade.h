//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_CRYPTO_MAC_FACADE_H_INCLUDED
#define BEAST_CRYPTO_MAC_FACADE_H_INCLUDED

#include <ripple/beast/crypto/secure_erase.h>
#include <ripple/beast/hash/endian.h>
#include <type_traits>
#include <array>

namespace beast {
namespace detail {

// Message Authentication Code (MAC) facade
template <class Context, bool Secure>
class mac_facade
{
private:
    Context ctx_;

public:
    static beast::endian const endian =
        beast::endian::native;

    static std::size_t const digest_size =
        Context::digest_size;

    using result_type =
        std::array<std::uint8_t, digest_size>;

    mac_facade() noexcept
    {
        init(ctx_);
    }

    ~mac_facade()
    {
        erase(std::integral_constant<
            bool, Secure>{});
    }

    void
    operator()(void const* data,
        std::size_t size) noexcept
    {
        update(ctx_, data, size);
    }

    explicit
    operator result_type() noexcept
    {
        result_type digest;
        finish(ctx_, &digest[0]);
        return digest;
    }

private:
    inline
    void
    erase (std::false_type) noexcept
    {
    }

    inline
    void
    erase (std::true_type) noexcept
    {
        secure_erase(&ctx_, sizeof(ctx_));
    }
};

} // detail
} // beast

#endif
