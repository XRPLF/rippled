//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2025 Ripple Labs Inc.

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
    testMergeInbox(FeatureBitset features)
    {
        testcase("Merge inbox");
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
            .amt = 40,
            .proof = "123",
            .holderPubKey = mptAlice.getPubKey(bob),
        });

        env.close();
        mptAlice.printMPT(bob);

        mptAlice.mergeInbox({
            .account = bob,
        });

        env.close();
        mptAlice.printMPT(bob);
    }

    void
    testSend(FeatureBitset features)
    {
        testcase("test confidential send");
        using namespace test::jtx;
        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

        mptAlice.create(
            {.ownerCount = 1,
             .holderCount = 0,
             .flags = tfMPTCanTransfer | tfMPTCanLock});

        mptAlice.authorize({.account = bob});
        mptAlice.authorize({.account = carol});

        mptAlice.pay(alice, bob, 100);
        mptAlice.pay(alice, carol, 50);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);

        mptAlice.set({.account = alice, .pubKey = mptAlice.getPubKey(alice)});

        // Convert 60 out of 100
        mptAlice.convert(
            {.account = bob,
             .amt = 60,
             .proof = "123",
             .holderPubKey = mptAlice.getPubKey(bob),
             .err = tesSUCCESS});

        BEAST_EXPECT(mptAlice.getBalance(bob) == 40);
        BEAST_EXPECT(
            mptAlice.getDecryptedBalance(
                bob, MPTTester::HOLDER_ENCRYPTED_INBOX) == 60);
        BEAST_EXPECT(
            mptAlice.getDecryptedBalance(
                bob, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 0);
        BEAST_EXPECT(
            mptAlice.getDecryptedBalance(
                bob, MPTTester::ISSUER_ENCRYPTED_BALANCE) == 60);

        // bob merge inbox
        mptAlice.mergeInbox({
            .account = bob,
        });

        mptAlice.convert(
            {.account = carol,
             .amt = 20,
             .proof = "123",
             .holderPubKey = mptAlice.getPubKey(carol),
             .err = tesSUCCESS});

        BEAST_EXPECT(mptAlice.getBalance(carol) == 30);
        BEAST_EXPECT(
            mptAlice.getDecryptedBalance(
                carol, MPTTester::HOLDER_ENCRYPTED_INBOX) == 20);
        BEAST_EXPECT(
            mptAlice.getDecryptedBalance(
                carol, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 0);
        BEAST_EXPECT(
            mptAlice.getDecryptedBalance(
                carol, MPTTester::ISSUER_ENCRYPTED_BALANCE) == 20);

        // carol merge inbox
        mptAlice.mergeInbox({
            .account = carol,
        });

        // bob sends 10 to carol
        mptAlice.send(
            {.account = bob,
             .dest = carol,
             .amt = 10,  // will be encrypted internally
             .proof = "123",
             .err = tesSUCCESS});

        // bob sends 1 to carol again
        mptAlice.send(
            {.account = bob,
             .dest = carol,
             .amt = 1,
             .proof = "123",
             .err = tesSUCCESS});

        mptAlice.mergeInbox({
            .account = carol,
        });

        // carol sends 15 backto bob
        mptAlice.send(
            {.account = carol,
             .dest = bob,
             .amt = 15,
             .proof = "123",
             .err = tesSUCCESS});
    }

    void
    testSendPreflight(FeatureBitset features)
    {
        testcase("test ConfidentialSend Preflight");
        using namespace test::jtx;

        // test disabled
        {
            Env env{*this, features - featureConfidentialTransfer};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            // Basic setup just to have accounts and MPT ID
            mptAlice.create();
            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});
            env.close();

            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = "123",
                 .senderEncryptedAmt =
                     Buffer(ripple::ecGamalEncryptedTotalLength),
                 .destEncryptedAmt =
                     Buffer(ripple::ecGamalEncryptedTotalLength),
                 .issuerEncryptedAmt =
                     Buffer(ripple::ecGamalEncryptedTotalLength),
                 .err = temDISABLED});
        }

        // test malformed
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create();
            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});
            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(carol);
            mptAlice.set(
                {.account = alice, .pubKey = mptAlice.getPubKey(alice)});
            mptAlice.pay(alice, bob, 100);
            mptAlice.pay(alice, carol, 50);
            env.close();

            // issuer can not be the same as sender
            mptAlice.send(
                {.account = alice,  // Issuer is sender
                 .dest = carol,
                 .amt = 10,
                 .proof = "123",
                 .senderEncryptedAmt =
                     Buffer(ripple::ecGamalEncryptedTotalLength),
                 .destEncryptedAmt =
                     Buffer(ripple::ecGamalEncryptedTotalLength),
                 .issuerEncryptedAmt =
                     Buffer(ripple::ecGamalEncryptedTotalLength),
                 .err = temMALFORMED});

            // can not send to self
            mptAlice.send(
                {.account = bob,
                 .dest = bob,
                 .amt = 10,
                 .proof = "123",
                 .senderEncryptedAmt =
                     Buffer(ripple::ecGamalEncryptedTotalLength),
                 .destEncryptedAmt =
                     Buffer(ripple::ecGamalEncryptedTotalLength),
                 .issuerEncryptedAmt =
                     Buffer(ripple::ecGamalEncryptedTotalLength),
                 .err = temMALFORMED});

            // sender encrypted amount wrong length
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = "123",
                 .senderEncryptedAmt = Buffer(10),
                 .destEncryptedAmt =
                     Buffer(ripple::ecGamalEncryptedTotalLength),
                 .issuerEncryptedAmt =
                     Buffer(ripple::ecGamalEncryptedTotalLength),
                 .err = temMALFORMED});
            // dest encrypted amount wrong length
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = "123",
                 .senderEncryptedAmt =
                     Buffer(ripple::ecGamalEncryptedTotalLength),
                 .destEncryptedAmt = Buffer(10),  // Incorrect length
                 .issuerEncryptedAmt =
                     Buffer(ripple::ecGamalEncryptedTotalLength),
                 .err = temMALFORMED});
            // issuer encrypted amount wrong length
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = "123",
                 .senderEncryptedAmt =
                     Buffer(ripple::ecGamalEncryptedTotalLength),
                 .destEncryptedAmt =
                     Buffer(ripple::ecGamalEncryptedTotalLength),
                 .issuerEncryptedAmt = Buffer(10),
                 .err = temMALFORMED});

            // todo: proof length check
        }
    }

    void
    testSendPreclaim(FeatureBitset features)
    {
        testcase("test ConfidentialSend Preclaim");

        using namespace test::jtx;
        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const dave("dave");
        Account const eve("eve");
        MPTTester mptAlice(env, alice, {.holders = {bob, carol, dave, eve}});

        // authorize bob, carol, dave (not eve)
        mptAlice.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
        mptAlice.authorize({.account = bob});
        mptAlice.authorize({.account = carol});
        mptAlice.authorize({.account = dave});
        env.close();

        // fund bob, carol (not dave or eve)
        mptAlice.pay(alice, bob, 100);
        mptAlice.pay(alice, carol, 50);
        env.close();

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);
        mptAlice.generateKeyPair(dave);
        mptAlice.set({.account = alice, .pubKey = mptAlice.getPubKey(alice)});
        env.close();

        // bob and carol convert some funds to confidential
        mptAlice.convert(
            {.account = bob,
             .amt = 60,
             .proof = "123",
             .holderPubKey = mptAlice.getPubKey(bob),
             .err = tesSUCCESS});
        mptAlice.convert(
            {.account = carol,
             .amt = 20,
             .proof = "123",
             .holderPubKey = mptAlice.getPubKey(carol),
             .err = tesSUCCESS});

        // // sender does not exist
        // {
        //     Json::Value jv;
        //     jv[jss::Account] = Account("unknown").human();
        //     jv[jss::Destination] = carol.human();
        //     jv[jss::TransactionType] = jss::ConfidentialSend;
        //     jv[jss::Sequence] = 1;
        //     jv[sfMPTokenIssuanceID] = to_string(mptAlice.issuanceID());
        //     jv[sfSenderEncryptedAmount.jsonName] =
        //     strHex(Buffer(ripple::ecGamalEncryptedTotalLength));
        //     jv[sfDestinationEncryptedAmount.jsonName] =
        //     strHex(Buffer(ripple::ecGamalEncryptedTotalLength));
        //     jv[sfIssuerEncryptedAmount.jsonName] =
        //     strHex(Buffer(ripple::ecGamalEncryptedTotalLength));
        //     jv[sfZKProof.jsonName] = "123";
        //     env(jv, ter(terNO_ACCOUNT));
        //     env.close();
        // }

        // destination does not exist
        {
            Account const unknown("unknown");
            mptAlice.send(
                {.account = bob,
                 .dest = unknown,
                 .amt = 10,
                 .proof = "123",
                 .issuerEncryptedAmt =
                     Buffer(ripple::ecGamalEncryptedTotalLength),
                 .destEncryptedAmt =
                     Buffer(ripple::ecGamalEncryptedTotalLength),
                 .err = tecNO_TARGET});
        }

        // dave exists, but has no confidential fields (never converted)
        {
            mptAlice.send(
                {.account = bob,
                 .dest = dave,
                 .amt = 10,
                 .proof = "123",
                 .err = tecNO_PERMISSION});
            mptAlice.send(
                {.account = dave,
                 .dest = carol,
                 .amt = 10,
                 .proof = "123",
                 .err = tecNO_PERMISSION});
        }

        // destination exists but has no MPT object.
        {
            mptAlice.send(
                {.account = bob,
                 .dest = eve,
                 .amt = 10,
                 .proof = "123",
                 .destEncryptedAmt =
                     Buffer(ripple::ecGamalEncryptedTotalLength),
                 .err = tecOBJECT_NOT_FOUND});
        }
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testConvert(features);
        testConvertPreflight(features);
        testConvertPreclaim(features);

        testMergeInbox(features);
        testSetPreflight(features);

        // ConfidentialSend
        testSend(features);
        testSendPreflight(features);
        testSendPreclaim(features);
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
