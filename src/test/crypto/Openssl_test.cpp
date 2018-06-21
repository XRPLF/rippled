//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

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

#include <ripple/beast/unit_test.h>
#include <ripple/crypto/impl/openssl.h>
#include <type_traits>

namespace ripple {
struct Openssl_test : public beast::unit_test::suite
{
    void
    testBasicProperties()
    {
        using namespace openssl;

        BEAST_EXPECT(std::is_default_constructible<bignum>{});
        BEAST_EXPECT(!std::is_copy_constructible<bignum>{});
        BEAST_EXPECT(!std::is_copy_assignable<bignum>{});
        BEAST_EXPECT(std::is_nothrow_move_constructible<bignum>{});
        BEAST_EXPECT(std::is_nothrow_move_assignable<bignum>{});

        BEAST_EXPECT(!std::is_default_constructible<ec_point>{});
        BEAST_EXPECT(!std::is_copy_constructible<ec_point>{});
        BEAST_EXPECT(!std::is_copy_assignable<ec_point>{});
        BEAST_EXPECT(std::is_nothrow_move_constructible<ec_point>{});
        BEAST_EXPECT(!std::is_nothrow_move_assignable<ec_point>{});
    }

    void
    run() override
    {
        testBasicProperties();
    };
};

BEAST_DEFINE_TESTSUITE(Openssl, crypto, ripple);

}  // namespace ripple
