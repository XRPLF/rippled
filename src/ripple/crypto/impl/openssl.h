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

#ifndef RIPPLE_OPENSSL_H
#define RIPPLE_OPENSSL_H

#include <ripple/basics/base_uint.h>
#include <ripple/crypto/impl/ec_key.h>
#include <openssl/bn.h>
#include <openssl/ec.h>

namespace ripple {
namespace openssl {

class bignum
{
private:
    BIGNUM* ptr;

    // non-copyable
    bignum(bignum const&) = delete;
    bignum&
    operator=(bignum const&) = delete;

    void
    assign_new(uint8_t const* data, size_t size);

public:
    bignum();

    ~bignum()
    {
        if (ptr != nullptr)
        {
            BN_free(ptr);
        }
    }

    bignum(uint8_t const* data, size_t size)
    {
        assign_new(data, size);
    }

    template <class T>
    explicit bignum(T const& thing)
    {
        assign_new(thing.data(), thing.size());
    }

    bignum(bignum&& that) noexcept : ptr(that.ptr)
    {
        that.ptr = nullptr;
    }

    bignum&
    operator=(bignum&& that) noexcept
    {
        using std::swap;

        swap(ptr, that.ptr);

        return *this;
    }

    BIGNUM*
    get()
    {
        return ptr;
    }
    BIGNUM const*
    get() const
    {
        return ptr;
    }

    bool
    is_zero() const
    {
        return BN_is_zero(ptr);
    }

    void
    clear()
    {
        BN_clear(ptr);
    }

    void
    assign(uint8_t const* data, size_t size);
};

inline bool
operator<(bignum const& a, bignum const& b)
{
    return BN_cmp(a.get(), b.get()) < 0;
}

inline bool
operator>=(bignum const& a, bignum const& b)
{
    return !(a < b);
}

inline uint256
uint256_from_bignum_clear(bignum& number)
{
    uint256 result;
    result.zero();

    BN_bn2bin(number.get(), result.end() - BN_num_bytes(number.get()));

    number.clear();

    return result;
}

class bn_ctx
{
private:
    BN_CTX* ptr;

    // non-copyable
    bn_ctx(bn_ctx const&);
    bn_ctx&
    operator=(bn_ctx const&);

public:
    bn_ctx();

    ~bn_ctx()
    {
        BN_CTX_free(ptr);
    }

    BN_CTX*
    get()
    {
        return ptr;
    }
    BN_CTX const*
    get() const
    {
        return ptr;
    }
};

bignum
get_order(EC_GROUP const* group, bn_ctx& ctx);

inline void
add_to(bignum const& a, bignum& b, bignum const& modulus, bn_ctx& ctx)
{
    BN_mod_add(b.get(), a.get(), b.get(), modulus.get(), ctx.get());
}

class ec_point
{
public:
    using pointer_t = EC_POINT*;

private:
    pointer_t ptr;

    ec_point(pointer_t raw) : ptr(raw)
    {
    }

public:
    static ec_point
    acquire(pointer_t raw)
    {
        return ec_point(raw);
    }

    ec_point(EC_GROUP const* group);

    ~ec_point()
    {
        EC_POINT_free(ptr);
    }

    ec_point(ec_point const&) = delete;
    ec_point&
    operator=(ec_point const&) = delete;

    ec_point(ec_point&& that) noexcept
    {
        ptr = that.ptr;
        that.ptr = nullptr;
    }

    EC_POINT*
    get()
    {
        return ptr;
    }
    EC_POINT const*
    get() const
    {
        return ptr;
    }
};

void
add_to(EC_GROUP const* group, ec_point const& a, ec_point& b, bn_ctx& ctx);

ec_point
multiply(EC_GROUP const* group, bignum const& n, bn_ctx& ctx);

ec_point
bn2point(EC_GROUP const* group, BIGNUM const* number);

// output buffer must hold 33 bytes
void
serialize_ec_point(ec_point const& point, std::uint8_t* ptr);

}  // namespace openssl
}  // namespace ripple

#endif
