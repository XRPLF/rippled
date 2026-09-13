//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 XRPL-Labs.

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

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/pay.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>

#include <array>
#include <cstdint>

namespace xrpl {

class PassKey_test : public beast::unit_test::Suite
{
    void
    testP256KeyTypeDetection()
    {
        testcase("P256 key type requires 0xF6 prefix");

        using namespace test::jtx;

        // Valid P-256 key from the test framework
        Account const p256acct{"p256acct", KeyType::P256};
        auto const keyType = publicKeyType(p256acct.pk());
        BEAST_EXPECT(keyType.has_value());
        BEAST_EXPECT(*keyType == KeyType::P256);

        // A 65-byte buffer with 0x04 prefix (standard uncompressed EC)
        // must NOT be accepted as P-256 on XRPL
        std::array<uint8_t, 65> badKey{};
        badKey[0] = 0x04;
        auto const badType = publicKeyType(makeSlice(badKey));
        BEAST_EXPECT(!badType.has_value());

        // A 65-byte buffer with 0xF6 prefix should be accepted
        std::array<uint8_t, 65> goodKey{};
        goodKey[0] = 0xF6;
        auto const goodType = publicKeyType(makeSlice(goodKey));
        BEAST_EXPECT(goodType.has_value());
        BEAST_EXPECT(*goodType == KeyType::P256);

        // Wrong size keys should not be detected as P-256
        std::array<uint8_t, 33> shortKey{};
        shortKey[0] = 0xF6;
        auto const shortType = publicKeyType(makeSlice(shortKey));
        BEAST_EXPECT(!shortType.has_value() || *shortType != KeyType::P256);

        std::array<uint8_t, 66> longKey{};
        longKey[0] = 0xF6;
        auto const longType = publicKeyType(makeSlice(longKey));
        BEAST_EXPECT(!longType.has_value());
    }

    void
    testP256SingleSign(FeatureBitset features)
    {
        using namespace test::jtx;

        testcase("P256 single sign");

        Env env{*this, envconfig(), features};
        Account const alice{"alice", KeyType::P256};
        Account const bob{"bob"};
        env.fund(XRP(1000), alice, bob);
        env.close();

        env(pay(alice, bob, XRP(100)));
        env.close();

        // Verify the payment went through
        BEAST_EXPECT(env.balance(bob) == XRP(1100));
    }

    void
    testP256WithOtherKeyTypes(FeatureBitset features)
    {
        using namespace test::jtx;

        testcase("P256 alongside other key types");

        Env env{*this, envconfig(), features};
        Account const alice{"alice", KeyType::P256};
        Account const bob{"bob"};  // secp256k1
        Account const carol{"carol", KeyType::Ed25519};
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

        // All key types should work for payments
        env(pay(alice, bob, XRP(10)));
        env(pay(bob, carol, XRP(10)));
        env(pay(carol, alice, XRP(10)));
        env.close();
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testP256SingleSign(features);
        testP256WithOtherKeyTypes(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        auto const sa = testableAmendments();

        // Protocol-level tests (no env needed)
        testP256KeyTypeDetection();

        // Integration tests with env
        testWithFeats(sa);
    }
};

BEAST_DEFINE_TESTSUITE(PassKey, protocol, xrpl);

}  // namespace xrpl
