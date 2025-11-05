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
    // A 66-byte array of random unsigned char values
    constexpr static unsigned char badCiphertext[ecGamalEncryptedTotalLength] =
        {0x3E, 0x9A, 0x0F, 0x7C, 0x51, 0xD8, 0x22, 0x8B, 0x6E, 0x14, 0xC9,
         0xF5, 0x4D, 0x6A, 0x03, 0x81, 0x77, 0x2B, 0xEE, 0x9F, 0x10, 0xC2,
         0x57, 0x3D, 0x88, 0x65, 0x0C, 0xAB, 0xF1, 0x4E, 0x19, 0x96, 0x2A,
         0x73, 0xDC, 0x44, 0xB8, 0x5F, 0x01, 0xEA, 0x87, 0x36, 0x60, 0xCE,
         0x92, 0x25, 0x7D, 0x5B, 0xC0, 0x1E, 0x48, 0xF9, 0x84, 0x33, 0x67,
         0xAD, 0x0B, 0xE3, 0x91, 0x50, 0xDA, 0x2F, 0x75, 0xC6, 0xBD, 0x42};

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

        mptAlice.convert({
            .account = bob,
            .amt = 20,
            .proof = "123",
        });

        mptAlice.convert({
            .account = bob,
            .amt = 40,
            .proof = "123",
        });

        mptAlice.convert({
            .account = bob,
            .amt = 40,
            .proof = "123",
        });
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
             .err = temBAD_CIPHERTEXT});

        mptAlice.convert(
            {.account = bob,
             .amt = 10,
             .proof = "123",
             .holderPubKey = mptAlice.getPubKey(bob),
             .issuerEncryptedAmt = Buffer{},
             .err = temBAD_CIPHERTEXT});

        mptAlice.convert(
            {.account = bob,
             .amt = maxMPTokenAmount + 1,
             .proof = "123",
             .holderPubKey = mptAlice.getPubKey(bob),
             .err = temBAD_AMOUNT});

        mptAlice.convert(
            {.account = bob,
             .amt = 1,
             .proof = "123",
             .holderPubKey = mptAlice.getPubKey(bob),
             .holderEncryptedAmt =
                 Buffer{badCiphertext, ecGamalEncryptedTotalLength},
             .err = temBAD_CIPHERTEXT});

        mptAlice.convert(
            {.account = bob,
             .amt = 1,
             .proof = "123",
             .holderPubKey = mptAlice.getPubKey(bob),
             .issuerEncryptedAmt =
                 Buffer{badCiphertext, ecGamalEncryptedTotalLength},
             .err = temBAD_CIPHERTEXT});

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

        // cannot convert if locked
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

            mptAlice.set({.account = alice, .holder = bob, .flags = tfMPTLock});

            mptAlice.generateKeyPair(bob);

            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
                 .proof = "123",
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .err = tecINSUFFICIENT_FUNDS});

            mptAlice.set(
                {.account = alice, .holder = bob, .flags = tfMPTUnlock});

            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .proof = "123",
                .holderPubKey = mptAlice.getPubKey(bob),
            });
        }

        // cannot convert if unauth
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTRequireAuth});

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = alice, .holder = bob});
            env.close();
            mptAlice.pay(alice, bob, 100);
            env.close();

            mptAlice.generateKeyPair(alice);

            mptAlice.set(
                {.account = alice, .pubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            // Unauthorize bob
            mptAlice.authorize(
                {.account = alice, .holder = bob, .flags = tfMPTUnauthorize});

            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
                 .proof = "123",
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .err = tecINSUFFICIENT_FUNDS});

            // auth bob
            mptAlice.authorize({
                .account = alice,
                .holder = bob,
            });

            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .proof = "123",
                .holderPubKey = mptAlice.getPubKey(bob),
            });
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

        mptAlice.mergeInbox({
            .account = bob,
        });
    }

    void
    testMergeInboxPreflight(FeatureBitset features)
    {
        testcase("Merge inbox preflight");
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

        mptAlice.mergeInbox({.account = alice, .err = temMALFORMED});

        env.disableFeature(featureConfidentialTransfer);
        env.close();

        mptAlice.mergeInbox({.account = bob, .err = temDISABLED});
    }

    void
    testMergeInboxPreclaim(FeatureBitset features)
    {
        testcase("Merge inbox preclaim");
        using namespace test::jtx;

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

            mptAlice.mergeInbox({.account = bob, .err = tecOBJECT_NOT_FOUND});
        }

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

            mptAlice.mergeInbox({.account = bob, .err = tecNO_PERMISSION});
        }

        // no mptoken
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

            mptAlice.set(
                {.account = alice, .pubKey = mptAlice.getPubKey(alice)});

            mptAlice.mergeInbox({.account = bob, .err = tecOBJECT_NOT_FOUND});
        }

        // bob doesn't have encrypted balances
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

            mptAlice.mergeInbox({.account = bob, .err = tecNO_PERMISSION});
        }
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
                 .err = temBAD_CIPHERTEXT});
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
                 .err = temBAD_CIPHERTEXT});
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
                 .err = temBAD_CIPHERTEXT});

            auto const ciphertextHex = generatePlaceholderCiphertext();

            // sender encrypted amount malformed
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = "123",
                 .senderEncryptedAmt =
                     Buffer(ripple::ecGamalEncryptedTotalLength),
                 .destEncryptedAmt = ciphertextHex,
                 .issuerEncryptedAmt = ciphertextHex,
                 .err = temBAD_CIPHERTEXT});
            // dest encrypted amount malformed
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = "123",
                 .senderEncryptedAmt = ciphertextHex,
                 .destEncryptedAmt =
                     Buffer(ripple::ecGamalEncryptedTotalLength),
                 .issuerEncryptedAmt = ciphertextHex,
                 .err = temBAD_CIPHERTEXT});
            // issuer encrypted amount malformed
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = "123",
                 .senderEncryptedAmt = ciphertextHex,
                 .destEncryptedAmt = ciphertextHex,
                 .issuerEncryptedAmt =
                     Buffer(ripple::ecGamalEncryptedTotalLength),
                 .err = temBAD_CIPHERTEXT});

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
        mptAlice.create(
            {.flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTRequireAuth});
        mptAlice.authorize({.account = bob});
        mptAlice.authorize({.account = alice, .holder = bob});
        mptAlice.authorize({.account = carol});
        mptAlice.authorize({.account = alice, .holder = carol});
        mptAlice.authorize({.account = dave});
        mptAlice.authorize({.account = alice, .holder = dave});

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

        // bob and carol merge inbox
        mptAlice.mergeInbox({
            .account = bob,
        });
        mptAlice.mergeInbox({
            .account = carol,
        });

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

        auto const ciphertextHex = generatePlaceholderCiphertext();

        // destination does not exist
        {
            Account const unknown("unknown");
            mptAlice.send(
                {.account = bob,
                 .dest = unknown,
                 .amt = 10,
                 .proof = "123",
                 .issuerEncryptedAmt = ciphertextHex,
                 .destEncryptedAmt = ciphertextHex,
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
                 .destEncryptedAmt = ciphertextHex,
                 .err = tecOBJECT_NOT_FOUND});
        }

        // issuance is locked globally
        {
            // lock issuance
            mptAlice.set({.account = alice, .flags = tfMPTLock});
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = "123",
                 .err = tecLOCKED});
            // unlock issuance
            mptAlice.set({.account = alice, .flags = tfMPTUnlock});
            // now can send
            mptAlice.send(
                {.account = bob, .dest = carol, .amt = 1, .proof = "123"});
        }

        // sender is locked
        {
            // lock bob
            mptAlice.set({.account = alice, .holder = bob, .flags = tfMPTLock});
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = "123",
                 .err = tecLOCKED});
            // unlock bob
            mptAlice.set(
                {.account = alice, .holder = bob, .flags = tfMPTUnlock});
            // now can send
            mptAlice.send(
                {.account = bob, .dest = carol, .amt = 2, .proof = "123"});
        }

        // destination is locked
        {
            // lock carol
            mptAlice.set(
                {.account = alice, .holder = carol, .flags = tfMPTLock});
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = "123",
                 .err = tecLOCKED});
            // unlock carol
            mptAlice.set(
                {.account = alice, .holder = carol, .flags = tfMPTUnlock});
            // now can send
            mptAlice.send(
                {.account = bob, .dest = carol, .amt = 3, .proof = "123"});
        }

        // sender not authorized
        {
            // unauthorize bob
            mptAlice.authorize(
                {.account = alice, .holder = bob, .flags = tfMPTUnauthorize});
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = "123",
                 .err = tecNO_AUTH});
            // authorize bob again
            mptAlice.authorize({
                .account = alice,
                .holder = bob,
            });
            // now can send
            mptAlice.send(
                {.account = bob, .dest = carol, .amt = 4, .proof = "123"});
        }

        // destination not authorized
        {
            // unauthorize carol
            mptAlice.authorize(
                {.account = alice, .holder = carol, .flags = tfMPTUnauthorize});
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = "123",
                 .err = tecNO_AUTH});
            // authorize carol again
            mptAlice.authorize({
                .account = alice,
                .holder = carol,
            });
            // now can send
            mptAlice.send(
                {.account = bob, .dest = carol, .amt = 5, .proof = "123"});
        }
    }

    void
    testDelete(FeatureBitset features)
    {
        testcase("Delete");
        using namespace test::jtx;

        // cannot delete mptoken where it has encrypted balance
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

            mptAlice.convert({
                .account = bob,
                .amt = 100,
                .proof = "123",
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            mptAlice.authorize(
                {.account = bob,
                 .flags = tfMPTUnauthorize,
                 .err = tecHAS_OBLIGATIONS});
        }

        // cannot delete mptoken where it has encrypted balance
        {
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
            env.close();
            mptAlice.pay(alice, bob, 100);
            env.close();

            mptAlice.generateKeyPair(alice);

            mptAlice.set(
                {.account = alice, .pubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(carol);

            mptAlice.convert({
                .account = bob,
                .amt = 100,
                .proof = "123",
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            mptAlice.convert({
                .account = carol,
                .amt = 0,
                .proof = "123",
                .holderPubKey = mptAlice.getPubKey(carol),
            });

            // carol cannot delete even if he has encrypted zero amount
            mptAlice.authorize(
                {.account = carol,
                 .flags = tfMPTUnauthorize,
                 .err = tecHAS_OBLIGATIONS});
        }

        // can delete mptoken if outstanding confidential balance is zero
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

            mptAlice.generateKeyPair(alice);

            mptAlice.set(
                {.account = alice, .pubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            mptAlice.convert({
                .account = bob,
                .amt = 0,
                .proof = "123",
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            mptAlice.authorize({
                .account = bob,
                .flags = tfMPTUnauthorize,
            });
        }

        // can delete mptoken if issuance has been destroyed and has encrypted
        // zero balance
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

            mptAlice.generateKeyPair(alice);

            mptAlice.set(
                {.account = alice, .pubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            mptAlice.convert({
                .account = bob,
                .amt = 0,
                .proof = "123",
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            mptAlice.destroy();

            mptAlice.authorize({
                .account = bob,
                .flags = tfMPTUnauthorize,
            });
        }
        // todo: test with convert back and delete
    }

    void
    testConvertBack(FeatureBitset features)
    {
        testcase("Convert back");
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

        mptAlice.mergeInbox({
            .account = bob,
        });

        mptAlice.convertBack({
            .account = bob,
            .amt = 30,
            .proof = "123",
        });

        // mptAlice.convertBack({
        //     .account = bob,
        //     .amt = 10,
        //     .proof = "123",
        // });
    }

    void
    testConvertBackPreflight(FeatureBitset features)
    {
        testcase("Convert back preflight");
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

            mptAlice.convertBack(
                {.account = bob,
                 .amt = 30,
                 .proof = "123",
                 .err = temDISABLED});
        }

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

            mptAlice.convert({
                .account = bob,
                .amt = 40,
                .proof = "123",
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            mptAlice.mergeInbox({
                .account = bob,
            });

            mptAlice.convertBack(
                {.account = alice,
                 .amt = 30,
                 .proof = "123",
                 .err = temMALFORMED});

            mptAlice.convertBack(
                {.account = bob,
                 .amt = 0,
                 .proof = "123",
                 .err = temBAD_AMOUNT});

            mptAlice.convertBack(
                {.account = bob,
                 .amt = maxMPTokenAmount + 1,
                 .proof = "123",
                 .err = temBAD_AMOUNT});

            mptAlice.convertBack(
                {.account = bob,
                 .amt = 30,
                 .proof = "123",
                 .holderEncryptedAmt = Buffer{},
                 .err = temBAD_CIPHERTEXT});

            mptAlice.convertBack(
                {.account = bob,
                 .amt = 30,
                 .proof = "123",
                 .issuerEncryptedAmt = Buffer{},
                 .err = temBAD_CIPHERTEXT});

            mptAlice.convertBack(
                {.account = bob,
                 .amt = 30,
                 .proof = "123",
                 .holderEncryptedAmt =
                     Buffer{badCiphertext, ecGamalEncryptedTotalLength},
                 .err = temBAD_CIPHERTEXT});

            mptAlice.convertBack(
                {.account = bob,
                 .amt = 30,
                 .proof = "123",
                 .issuerEncryptedAmt =
                     Buffer{badCiphertext, ecGamalEncryptedTotalLength},
                 .err = temBAD_CIPHERTEXT});
        }
    }

    void
    testConvertBackPreclaim(FeatureBitset features)
    {
        testcase("Convert back preclaim");
        using namespace test::jtx;

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

            mptAlice.convertBack(
                {.account = bob,
                 .amt = 30,
                 .proof = "123",
                 .err = tecOBJECT_NOT_FOUND});
        }

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

            mptAlice.convertBack(
                {.account = bob,
                 .amt = 30,
                 .proof = "123",
                 .err = tecNO_PERMISSION});
        }

        // no mptoken
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

            mptAlice.convertBack(
                {.account = bob,
                 .amt = 30,
                 .proof = "123",
                 .err = tecOBJECT_NOT_FOUND});
        }

        // bob doesn't have encrypted balances
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

            mptAlice.convertBack(
                {.account = bob,
                 .amt = 30,
                 .proof = "123",
                 .err = tecNO_PERMISSION});
        }

        // bob tries to convert back more than COA
        {
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
            env.close();
            mptAlice.pay(alice, bob, 100);
            mptAlice.pay(alice, carol, 100);
            env.close();

            mptAlice.generateKeyPair(alice);

            mptAlice.set(
                {.account = alice, .pubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(carol);

            mptAlice.convert({
                .account = bob,
                .amt = 40,
                .proof = "123",
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            mptAlice.mergeInbox({
                .account = bob,
            });

            mptAlice.convert({
                .account = carol,
                .amt = 40,
                .proof = "123",
                .holderPubKey = mptAlice.getPubKey(carol),
            });

            mptAlice.convertBack(
                {.account = bob,
                 .amt = 300,
                 .proof = "123",
                 .err = tecINSUFFICIENT_FUNDS});
        }

        // cannot convert if locked or unauth
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTRequireAuth});

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = alice, .holder = bob});
            env.close();
            mptAlice.pay(alice, bob, 100);
            env.close();

            mptAlice.generateKeyPair(alice);

            mptAlice.set(
                {.account = alice, .pubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            mptAlice.convert({
                .account = bob,
                .amt = 40,
                .proof = "123",
                .holderPubKey = mptAlice.getPubKey(bob),
            });
            mptAlice.mergeInbox({.account = bob});
            mptAlice.set({.account = alice, .holder = bob, .flags = tfMPTLock});

            mptAlice.convertBack(
                {.account = bob, .amt = 10, .proof = "123", .err = tecLOCKED});

            mptAlice.set(
                {.account = alice, .holder = bob, .flags = tfMPTUnlock});

            mptAlice.convertBack({.account = bob, .amt = 10, .proof = "123"});

            mptAlice.authorize(
                {.account = alice, .holder = bob, .flags = tfMPTUnauthorize});

            mptAlice.convertBack(
                {.account = bob, .amt = 10, .proof = "123", .err = tecNO_AUTH});

            mptAlice.authorize({
                .account = alice,
                .holder = bob,
            });

            mptAlice.convertBack({
                .account = bob,
                .amt = 10,
                .proof = "123",
            });
        }
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testConvert(features);
        testConvertPreflight(features);
        testConvertPreclaim(features);

        testMergeInbox(features);
        testMergeInboxPreflight(features);
        testMergeInboxPreclaim(features);

        testSetPreflight(features);

        // ConfidentialSend
        testSend(features);
        testSendPreflight(features);
        testSendPreclaim(features);

        testDelete(features);

        testConvertBack(features);
        testConvertBackPreflight(features);
        testConvertBackPreclaim(features);
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
