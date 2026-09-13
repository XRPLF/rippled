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
#include <test/jtx/fee.h>
#include <test/jtx/multisign.h>
#include <test/jtx/noop.h>
#include <test/jtx/pay.h>
#include <test/jtx/sig.h>
#include <test/jtx/ter.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/jss.h>

#include <string>
#include <utility>
#include <vector>

namespace xrpl {

class PasskeyListSet_test : public beast::unit_test::Suite
{
    json::Value
    passkeyListSet(test::jtx::Account const& account)
    {
        json::Value jv;
        jv[sfAccount.jsonName] = account.human();
        jv[sfTransactionType.jsonName] = jss::PasskeyListSet;
        jv[sfPasskeys.jsonName] = json::ValueType::Array;
        jv[sfPasskeys.jsonName][0u][sfPasskey.jsonName][sfPasskeyID.jsonName] = "DEADBEEF";
        jv[sfPasskeys.jsonName][0u][sfPasskey.jsonName][sfPublicKey.jsonName] =
            strHex(account.pk());
        return jv;
    }

    json::Value
    passkeyListSetMulti(
        test::jtx::Account const& account,
        std::vector<std::pair<std::string, std::string>> const& entries)
    {
        json::Value jv;
        jv[sfAccount.jsonName] = account.human();
        jv[sfTransactionType.jsonName] = jss::PasskeyListSet;
        jv[sfPasskeys.jsonName] = json::ValueType::Array;
        for (json::UInt i = 0; i < entries.size(); ++i)
        {
            jv[sfPasskeys.jsonName][i][sfPasskey.jsonName][sfPasskeyID.jsonName] =
                entries[i].first;
            jv[sfPasskeys.jsonName][i][sfPasskey.jsonName][sfPublicKey.jsonName] =
                entries[i].second;
        }
        return jv;
    }

public:
    void
    testBasicPasskeyListSet(FeatureBitset features)
    {
        using namespace test::jtx;

        testcase("basic passkey list set");

        Env env{*this, envconfig(), features};
        Account const alice{"alice", KeyType::P256};
        env.fund(XRP(1000), alice);
        env.close();

        env(passkeyListSet(alice));
        env.close();
    }

    void
    testValidMultiplePasskeys(FeatureBitset features)
    {
        using namespace test::jtx;

        testcase("valid multiple passkeys");

        Env env{*this, envconfig(), features};
        Account const alice{"alice", KeyType::P256};
        Account const bob{"bob", KeyType::P256};
        env.fund(XRP(1000), alice, bob);
        env.close();

        // Two valid entries with different IDs and different PublicKeys
        auto jv = passkeyListSetMulti(
            alice, {{"DEADBEEF01", strHex(alice.pk())}, {"DEADBEEF02", strHex(bob.pk())}});
        env(jv);
        env.close();
    }

    void
    testEmptyPasskeyList(FeatureBitset features)
    {
        using namespace test::jtx;

        testcase("empty passkey list rejected");

        Env env{*this, envconfig(), features};
        Account const alice{"alice", KeyType::P256};
        env.fund(XRP(1000), alice);
        env.close();

        json::Value jv;
        jv[sfAccount.jsonName] = alice.human();
        jv[sfTransactionType.jsonName] = jss::PasskeyListSet;
        jv[sfPasskeys.jsonName] = json::ValueType::Array;
        env(jv, Ter(temMALFORMED));
    }

    void
    testDuplicatePasskeyID(FeatureBitset features)
    {
        using namespace test::jtx;

        testcase("duplicate passkey ID rejected");

        Env env{*this, envconfig(), features};
        Account const alice{"alice", KeyType::P256};
        Account const bob{"bob", KeyType::P256};
        env.fund(XRP(1000), alice, bob);
        env.close();

        // Two entries with the same PasskeyID but different PublicKeys
        auto jv = passkeyListSetMulti(
            alice, {{"DEADBEEF", strHex(alice.pk())}, {"DEADBEEF", strHex(bob.pk())}});
        env(jv, Ter(temMALFORMED));
    }

    void
    testDuplicatePublicKey(FeatureBitset features)
    {
        using namespace test::jtx;

        testcase("duplicate public key rejected");

        Env env{*this, envconfig(), features};
        Account const alice{"alice", KeyType::P256};
        env.fund(XRP(1000), alice);
        env.close();

        // Two entries with different PasskeyIDs but the same PublicKey
        auto jv = passkeyListSetMulti(
            alice, {{"DEADBEEF01", strHex(alice.pk())}, {"DEADBEEF02", strHex(alice.pk())}});
        env(jv, Ter(temMALFORMED));
    }

    void
    testInvalidKeyType(FeatureBitset features)
    {
        using namespace test::jtx;

        testcase("non-P256 key rejected");

        Env env{*this, envconfig(), features};
        Account const alice{"alice", KeyType::P256};
        Account const bob{"bob"};  // secp256k1
        env.fund(XRP(1000), alice, bob);
        env.close();

        // A secp256k1 key should be rejected
        auto jv = passkeyListSetMulti(alice, {{"DEADBEEF", strHex(bob.pk())}});
        env(jv, Ter(temMALFORMED));
    }

    void
    testEd25519KeyRejected(FeatureBitset features)
    {
        using namespace test::jtx;

        testcase("ed25519 key rejected");

        Env env{*this, envconfig(), features};
        Account const alice{"alice", KeyType::P256};
        Account const carol{"carol", KeyType::Ed25519};
        env.fund(XRP(1000), alice, carol);
        env.close();

        // An ed25519 key should be rejected
        auto jv = passkeyListSetMulti(alice, {{"DEADBEEF", strHex(carol.pk())}});
        env(jv, Ter(temMALFORMED));
    }

    void
    testInvalidKeyPrefix(FeatureBitset features)
    {
        using namespace test::jtx;

        testcase("invalid P256 prefix rejected");

        Env env{*this, envconfig(), features};
        Account const alice{"alice", KeyType::P256};
        env.fund(XRP(1000), alice);
        env.close();

        // Create a 65-byte key with wrong prefix (0x04 instead of 0xF6)
        auto pkHex = strHex(alice.pk());
        pkHex[0] = '0';
        pkHex[1] = '4';

        auto jv = passkeyListSetMulti(alice, {{"DEADBEEF", pkHex}});
        env(jv, Ter(temMALFORMED));
    }

    void
    testPasskeyPayment(FeatureBitset features)
    {
        using namespace test::jtx;

        testcase("payment with passkey signer");

        Env env{*this, envconfig(), features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const dave{"dave", KeyType::P256};
        env.fund(XRP(1000), alice, bob, dave);
        env.close();

        // Register dave's P-256 key as a passkey for alice's account.
        env(passkeyListSetMulti(alice, {{"DEADBEEF", strHex(dave.pk())}}));
        env(pay(alice, bob, XRP(100)), Sig(dave));
        env.close();

        BEAST_EXPECT(env.balance(bob) == XRP(1100));
    }

    void
    testMultisignWithP256(FeatureBitset features)
    {
        using namespace test::jtx;

        testcase("multisign with P256 signers");

        Env env{*this, envconfig(), features};
        Account const alice{"alice"};
        Account const bob{"bob", KeyType::P256};
        Account const carol{"carol", KeyType::P256};
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

        // Set up a signer list with P-256 accounts
        env(signers(alice, 1, {{bob, 1}, {carol, 1}}));
        env.close();

        auto const baseFee = env.current()->fees().base;

        // Multi-sign with one P-256 signer
        env(noop(alice), Msig(bob), Fee(2 * baseFee));
        env.close();

        // Multi-sign with both P-256 signers
        env(noop(alice), Msig(bob, carol), Fee(3 * baseFee));
        env.close();
    }

    void
    testMultisignMixedKeyTypes(FeatureBitset features)
    {
        using namespace test::jtx;

        testcase("multisign with mixed key types including P256");

        Env env{*this, envconfig(), features};
        Account const alice{"alice"};
        Account const bob{"bob"};  // secp256k1
        Account const carol{"carol", KeyType::Ed25519};
        Account const dave{"dave", KeyType::P256};
        env.fund(XRP(1000), alice, bob, carol, dave);
        env.close();

        // Set up a signer list with mixed key types
        env(signers(alice, 2, {{bob, 1}, {carol, 1}, {dave, 1}}));
        env.close();

        auto const baseFee = env.current()->fees().base;

        // Multi-sign with secp256k1 + P-256
        env(noop(alice), Msig(bob, dave), Fee(3 * baseFee));
        env.close();

        // Multi-sign with ed25519 + P-256
        env(noop(alice), Msig(carol, dave), Fee(3 * baseFee));
        env.close();

        // Multi-sign with all three key types
        env(noop(alice), Msig(bob, carol, dave), Fee(4 * baseFee));
        env.close();
    }

    void
    run() override
    {
        using namespace test::jtx;
        auto const sa = testableAmendments();

        testBasicPasskeyListSet(sa);
        testValidMultiplePasskeys(sa);
        testEmptyPasskeyList(sa);
        testDuplicatePasskeyID(sa);
        testDuplicatePublicKey(sa);
        testInvalidKeyType(sa);
        testEd25519KeyRejected(sa);
        testInvalidKeyPrefix(sa);
        testPasskeyPayment(sa);
        testMultisignWithP256(sa);
        testMultisignMixedKeyTypes(sa);
    }
};

BEAST_DEFINE_TESTSUITE(PasskeyListSet, app, xrpl);

}  // namespace xrpl
