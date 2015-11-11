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

#include <ripple/basics/contract.h>
#include <ripple/crypto/impl/openssl.h>
#include <openssl/hmac.h>

namespace ripple  {
namespace openssl {

bignum::bignum()
{
    ptr = BN_new();
    if (ptr == nullptr)
        Throw<std::runtime_error> ("BN_new() failed");
}

void bignum::assign (uint8_t const* data, size_t size)
{
    // This reuses and assigns ptr
    BIGNUM* bn = BN_bin2bn (data, size, ptr);
    if (bn == nullptr)
        Throw<std::runtime_error> ("BN_bin2bn() failed");
}

void bignum::assign_new (uint8_t const* data, size_t size)
{
    // ptr must not be allocated

    ptr = BN_bin2bn (data, size, nullptr);
    if (ptr == nullptr)
        Throw<std::runtime_error> ("BN_bin2bn() failed");
}

bn_ctx::bn_ctx()
{
    ptr = BN_CTX_new();
    if (ptr == nullptr)
        Throw<std::runtime_error> ("BN_CTX_new() failed");
}

bignum get_order (EC_GROUP const* group, bn_ctx& ctx)
{
    bignum result;
    if (! EC_GROUP_get_order (group, result.get(), ctx.get()))
        Throw<std::runtime_error> ("EC_GROUP_get_order() failed");

    return result;
}

ec_point::ec_point (EC_GROUP const* group)
{
    ptr = EC_POINT_new (group);
    if (ptr == nullptr)
        Throw<std::runtime_error> ("EC_POINT_new() failed");
}

void add_to (EC_GROUP const* group,
             ec_point const& a,
             ec_point& b,
             bn_ctx& ctx)
{
    if (!EC_POINT_add (group, b.get(), a.get(), b.get(), ctx.get()))
        Throw<std::runtime_error> ("EC_POINT_add() failed");
}

ec_point multiply (EC_GROUP const* group,
                   bignum const& n,
                   bn_ctx& ctx)
{
    ec_point result (group);
    if (! EC_POINT_mul (group, result.get(), n.get(), nullptr, nullptr, ctx.get()))
        Throw<std::runtime_error> ("EC_POINT_mul() failed");

    return result;
}

ec_point bn2point (EC_GROUP const* group, BIGNUM const* number)
{
    EC_POINT* result = EC_POINT_bn2point (group, number, nullptr, nullptr);
    if (result == nullptr)
        Throw<std::runtime_error> ("EC_POINT_bn2point() failed");

    return ec_point::acquire (result);
}

static ec_key ec_key_new_secp256k1_compressed()
{
    EC_KEY* key = EC_KEY_new_by_curve_name (NID_secp256k1);

    if (key == nullptr)  Throw<std::runtime_error> ("EC_KEY_new_by_curve_name() failed");

    EC_KEY_set_conv_form (key, POINT_CONVERSION_COMPRESSED);

    return ec_key((ec_key::pointer_t) key);
}

void serialize_ec_point (ec_point const& point, std::uint8_t* ptr)
{
    ec_key key = ec_key_new_secp256k1_compressed();
    if (EC_KEY_set_public_key((EC_KEY*) key.get(), point.get()) <= 0)
        Throw<std::runtime_error> ("EC_KEY_set_public_key() failed");

    int const size = i2o_ECPublicKey ((EC_KEY*) key.get(), &ptr);

    assert (size <= 33);
    (void) size;
}

} // openssl
} // ripple

#include <stdio.h>
#ifdef _MSC_VER
FILE _iob[] = {*stdin, *stdout, *stderr};
extern "C" FILE * __cdecl __iob_func(void)
{
    return _iob;
}
#endif
