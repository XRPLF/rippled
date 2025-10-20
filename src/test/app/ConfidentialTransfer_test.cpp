//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2023 Ripple Labs Inc.

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

#include <test/jtx.h>
#include <test/jtx/trust.h>

#include <xrpl/protocol/ConfidentialTransfer.h>

#include <openssl/rand.h>

namespace ripple {

class ConfidentialTransfer_test : public beast::unit_test::suite
{
    void
    testConvert(FeatureBitset features)
    {
        testcase("Convert");
        using namespace test::jtx;
        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        MPTTester mptAlice(env, alice, {.holders = {bob}});

        mptAlice.create(
            {.ownerCount = 1,
             .holderCount = 0,
             .flags = tfMPTCanTransfer | tfMPTCanLock});

        mptAlice.authorize({.account = bob});
        env.close();
        mptAlice.pay(alice, bob, 100);
        env.close();

        mptAlice.generateKeyPair(alice);

        mptAlice.set({.account = alice, .pubKey = mptAlice.getPubKey(alice)});

        mptAlice.generateKeyPair(bob);

        mptAlice.convert({
            .account = bob,
            .amt = 0,
            .proof = "123",
            .holderPubKey = mptAlice.getPubKey(bob),
        });
        env.close();

        mptAlice.printMPT(bob);

        mptAlice.convert({
            .account = bob,
            .amt = 20,
            .proof = "123",
        });

        mptAlice.printMPT(bob);

        mptAlice.convert({
            .account = bob,
            .amt = 40,
            .proof = "123",
        });

        env.close();
        mptAlice.printMPT(bob);
    }

    void
    testConvertPreflight(FeatureBitset features)
    {
        testcase("Convert preflight");
        using namespace test::jtx;

        Env env{*this, features - featureConfidentialTransfer};
        Account const alice("alice");
        Account const bob("bob");
        MPTTester mptAlice(env, alice, {.holders = {bob}});

        mptAlice.create(
            {.ownerCount = 1,
             .holderCount = 0,
             .flags = tfMPTCanTransfer | tfMPTCanLock});

        mptAlice.authorize({.account = bob});
        env.close();
        mptAlice.pay(alice, bob, 100);
        env.close();

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);

        mptAlice.set(
            {.account = alice,
             .pubKey = mptAlice.getPubKey(alice),
             .err = temDISABLED});

        mptAlice.convert(
            {.account = bob,
             .amt = 10,
             .proof = "123",
             .holderPubKey = mptAlice.getPubKey(bob),
             .err = temDISABLED});

        env.close();

        env.enableFeature(featureConfidentialTransfer);
        env.close();

        mptAlice.convert(
            {.account = alice,
             .amt = 10,
             .proof = "123",
             .holderPubKey = mptAlice.getPubKey(bob),
             .err = temMALFORMED});

        mptAlice.convert(
            {.account = bob,
             .amt = 10,
             .proof = "123",
             .holderPubKey = mptAlice.getPubKey(bob),
             .holderEncryptedAmt = Buffer{},
             .err = temMALFORMED});

        mptAlice.convert(
            {.account = bob,
             .amt = 10,
             .proof = "123",
             .holderPubKey = mptAlice.getPubKey(bob),
             .issuerEncryptedAmt = Buffer{},
             .err = temMALFORMED});

        mptAlice.convert(
            {.account = bob,
             .amt = maxMPTokenAmount + 1,
             .proof = "123",
             .holderPubKey = mptAlice.getPubKey(bob),
             .err = temMALFORMED});

        // todo: change to to check proof size
        // mptAlice.convert(
        //     {.account = bob,
        //      .amt = 10,
        //      .proof = "123",
        //      .holderPubKey = mptAlice.getPubKey(bob),
        //      .err = temMALFORMED});
    }

    void
    testSetPreflight(FeatureBitset features)
    {
        testcase("Set preflight");
        using namespace test::jtx;

        {
            Env env{*this, features - featureConfidentialTransfer};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanTransfer | tfMPTCanLock});

            mptAlice.authorize({.account = bob});
            env.close();
            mptAlice.pay(alice, bob, 100);
            env.close();

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set(
                {.account = alice,
                 .pubKey = mptAlice.getPubKey(alice),
                 .err = temDISABLED});
        }

        // issuance has disabled confidential transfer
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanTransfer | tfMPTCanLock |
                     tfMPTNoConfidentialTransfer});

            mptAlice.authorize({.account = bob});
            env.close();
            mptAlice.pay(alice, bob, 100);
            env.close();

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set(
                {.account = alice,
                 .pubKey = mptAlice.getPubKey(alice),
                 .err = tecNO_PERMISSION});
        }
    }

    void
    testConvertPreclaim(FeatureBitset features)
    {
        testcase("Convert preclaim");
        using namespace test::jtx;

        // tfMPTNoConfidentialTransfer is set on issuance
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanTransfer | tfMPTCanLock |
                     tfMPTNoConfidentialTransfer});

            mptAlice.authorize({.account = bob});
            env.close();
            mptAlice.pay(alice, bob, 100);
            env.close();

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
                 .proof = "123",
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .err = tecNO_PERMISSION});
        }

        // issuer has not uploaded their sfIssuerElGamalPublicKey
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanTransfer | tfMPTCanLock});

            mptAlice.authorize({.account = bob});
            env.close();
            mptAlice.pay(alice, bob, 100);
            env.close();

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
                 .proof = "123",
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .err = tecNO_PERMISSION});
        }

        // issuance does not exist
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanTransfer | tfMPTCanLock});

            mptAlice.authorize({.account = bob});
            mptAlice.generateKeyPair(alice);

            mptAlice.set(
                {.account = alice, .pubKey = mptAlice.getPubKey(alice)});

            mptAlice.destroy();
            mptAlice.generateKeyPair(bob);

            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
                 .proof = "123",
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .err = tecOBJECT_NOT_FOUND});
        }

        // bob has not created MPToken
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanTransfer | tfMPTCanLock});

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set(
                {.account = alice, .pubKey = mptAlice.getPubKey(alice)});

            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
                 .proof = "123",
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .err = tecOBJECT_NOT_FOUND});
        }

        // trying to convert more than what bob has
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanTransfer | tfMPTCanLock});

            mptAlice.authorize({.account = bob});
            env.close();
            mptAlice.pay(alice, bob, 100);
            env.close();

            mptAlice.generateKeyPair(alice);

            mptAlice.set(
                {.account = alice, .pubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            mptAlice.convert(
                {.account = bob,
                 .amt = 200,
                 .proof = "123",
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .err = tecINSUFFICIENT_FUNDS});
        }

        // holder cannot upload pk again
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanTransfer | tfMPTCanLock});

            mptAlice.authorize({.account = bob});
            env.close();
            mptAlice.pay(alice, bob, 100);
            env.close();

            mptAlice.generateKeyPair(alice);

            mptAlice.set(
                {.account = alice, .pubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
                 .proof = "123",
                 .holderPubKey = mptAlice.getPubKey(bob)});

            // cannot upload pk again
            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
                 .proof = "123",
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .err = tecDUPLICATE});
        }

        // todo: test well formed proof
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testConvert(features);
        testConvertPreflight(features);
        testConvertPreclaim(features);

        testSetPreflight(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{testable_amendments()};

        testWithFeats(all);
    }
};

BEAST_DEFINE_TESTSUITE(ConfidentialTransfer, app, ripple);
}  // namespace ripple
