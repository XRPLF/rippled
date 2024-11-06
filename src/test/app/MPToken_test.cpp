//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2024 Ripple Labs Inc.

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
#include <test/jtx/xchain_bridge.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {

class MPToken_test : public beast::unit_test::suite
{
    void
    testCreateValidation(FeatureBitset features)
    {
        testcase("Create Validate");
        using namespace test::jtx;
        Account const alice("alice");

        // test preflight of MPTokenIssuanceCreate
        {
            // If the MPT amendment is not enabled, you should not be able to
            // create MPTokenIssuances
            Env env{*this, features - featureMPTokensV1};
            MPTTester mptAlice(env, alice);

            mptAlice.create({.ownerCount = 0, .err = temDISABLED});
        }

        // test preflight of MPTokenIssuanceCreate
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice);

            mptAlice.create({.flags = 0x00000001, .err = temINVALID_FLAG});

            // tries to set a txfee while not enabling in the flag
            mptAlice.create(
                {.maxAmt = 100,
                 .assetScale = 0,
                 .transferFee = 1,
                 .metadata = "test",
                 .err = temMALFORMED});

            // tries to set a txfee greater than max
            mptAlice.create(
                {.maxAmt = 100,
                 .assetScale = 0,
                 .transferFee = maxTransferFee + 1,
                 .metadata = "test",
                 .flags = tfMPTCanTransfer,
                 .err = temBAD_TRANSFER_FEE});

            // tries to set a txfee while not enabling transfer
            mptAlice.create(
                {.maxAmt = 100,
                 .assetScale = 0,
                 .transferFee = maxTransferFee,
                 .metadata = "test",
                 .err = temMALFORMED});

            // empty metadata returns error
            mptAlice.create(
                {.maxAmt = 100,
                 .assetScale = 0,
                 .transferFee = 0,
                 .metadata = "",
                 .err = temMALFORMED});

            // MaximumAmout of 0 returns error
            mptAlice.create(
                {.maxAmt = 0,
                 .assetScale = 1,
                 .transferFee = 1,
                 .metadata = "test",
                 .err = temMALFORMED});

            // MaximumAmount larger than 63 bit returns error
            mptAlice.create(
                {.maxAmt = 0xFFFF'FFFF'FFFF'FFF0,  // 18'446'744'073'709'551'600
                 .assetScale = 0,
                 .transferFee = 0,
                 .metadata = "test",
                 .err = temMALFORMED});
            mptAlice.create(
                {.maxAmt = maxMPTokenAmount + 1,  // 9'223'372'036'854'775'808
                 .assetScale = 0,
                 .transferFee = 0,
                 .metadata = "test",
                 .err = temMALFORMED});
        }
    }

    void
    testCreateEnabled(FeatureBitset features)
    {
        testcase("Create Enabled");

        using namespace test::jtx;
        Account const alice("alice");

        {
            // If the MPT amendment IS enabled, you should be able to create
            // MPTokenIssuances
            Env env{*this, features};
            MPTTester mptAlice(env, alice);
            mptAlice.create(
                {.maxAmt = maxMPTokenAmount,  // 9'223'372'036'854'775'807
                 .assetScale = 1,
                 .transferFee = 10,
                 .metadata = "123",
                 .ownerCount = 1,
                 .flags = tfMPTCanLock | tfMPTRequireAuth | tfMPTCanEscrow |
                     tfMPTCanTrade | tfMPTCanTransfer | tfMPTCanClawback});

            // Get the hash for the most recent transaction.
            std::string const txHash{
                env.tx()->getJson(JsonOptions::none)[jss::hash].asString()};

            Json::Value const result = env.rpc("tx", txHash)[jss::result];
            BEAST_EXPECT(
                result[sfMaximumAmount.getJsonName()] == "9223372036854775807");
        }
    }

    void
    testDestroyValidation(FeatureBitset features)
    {
        testcase("Destroy Validate");

        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        // MPTokenIssuanceDestroy (preflight)
        {
            Env env{*this, features - featureMPTokensV1};
            MPTTester mptAlice(env, alice);
            auto const id = makeMptID(env.seq(alice), alice);
            mptAlice.destroy({.id = id, .ownerCount = 0, .err = temDISABLED});

            env.enableFeature(featureMPTokensV1);

            mptAlice.destroy(
                {.id = id, .flags = 0x00000001, .err = temINVALID_FLAG});
        }

        // MPTokenIssuanceDestroy (preclaim)
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.destroy(
                {.id = makeMptID(env.seq(alice), alice),
                 .ownerCount = 0,
                 .err = tecOBJECT_NOT_FOUND});

            mptAlice.create({.ownerCount = 1});

            // a non-issuer tries to destroy a mptissuance they didn't issue
            mptAlice.destroy({.issuer = bob, .err = tecNO_PERMISSION});

            // Make sure that issuer can't delete issuance when it still has
            // outstanding balance
            {
                // bob now holds a mptoken object
                mptAlice.authorize({.account = bob, .holderCount = 1});

                // alice pays bob 100 tokens
                mptAlice.pay(alice, bob, 100);

                mptAlice.destroy({.err = tecHAS_OBLIGATIONS});
            }
        }
    }

    void
    testDestroyEnabled(FeatureBitset features)
    {
        testcase("Destroy Enabled");

        using namespace test::jtx;
        Account const alice("alice");

        // If the MPT amendment IS enabled, you should be able to destroy
        // MPTokenIssuances
        Env env{*this, features};
        MPTTester mptAlice(env, alice);

        mptAlice.create({.ownerCount = 1});

        mptAlice.destroy({.ownerCount = 0});
    }

    void
    testAuthorizeValidation(FeatureBitset features)
    {
        testcase("Validate authorize transaction");

        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const cindy("cindy");
        // Validate amendment enable in MPTokenAuthorize (preflight)
        {
            Env env{*this, features - featureMPTokensV1};
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.authorize(
                {.account = bob,
                 .id = makeMptID(env.seq(alice), alice),
                 .err = temDISABLED});
        }

        // Validate fields in MPTokenAuthorize (preflight)
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1});

            // The only valid MPTokenAuthorize flag is tfMPTUnauthorize, which
            // has a value of 1
            mptAlice.authorize(
                {.account = bob, .flags = 0x00000002, .err = temINVALID_FLAG});

            mptAlice.authorize(
                {.account = bob, .holder = bob, .err = temMALFORMED});

            mptAlice.authorize({.holder = alice, .err = temMALFORMED});
        }

        // Try authorizing when MPTokenIssuance doesn't exist in
        // MPTokenAuthorize (preclaim)
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}});
            auto const id = makeMptID(env.seq(alice), alice);

            mptAlice.authorize(
                {.holder = bob, .id = id, .err = tecOBJECT_NOT_FOUND});

            mptAlice.authorize(
                {.account = bob, .id = id, .err = tecOBJECT_NOT_FOUND});
        }

        // Test bad scenarios without allowlisting in MPTokenAuthorize
        // (preclaim)
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1});

            // bob submits a tx with a holder field
            mptAlice.authorize(
                {.account = bob, .holder = alice, .err = tecNO_PERMISSION});

            // alice tries to hold onto her own token
            mptAlice.authorize({.account = alice, .err = tecNO_PERMISSION});

            // the mpt does not enable allowlisting
            mptAlice.authorize({.holder = bob, .err = tecNO_AUTH});

            // bob now holds a mptoken object
            mptAlice.authorize({.account = bob, .holderCount = 1});

            // bob cannot create the mptoken the second time
            mptAlice.authorize({.account = bob, .err = tecDUPLICATE});

            // Check that bob cannot delete MPToken when his balance is
            // non-zero
            {
                // alice pays bob 100 tokens
                mptAlice.pay(alice, bob, 100);

                // bob tries to delete his MPToken, but fails since he still
                // holds tokens
                mptAlice.authorize(
                    {.account = bob,
                     .flags = tfMPTUnauthorize,
                     .err = tecHAS_OBLIGATIONS});

                // bob pays back alice 100 tokens
                mptAlice.pay(bob, alice, 100);
            }

            // bob deletes/unauthorizes his MPToken
            mptAlice.authorize({.account = bob, .flags = tfMPTUnauthorize});

            // bob receives error when he tries to delete his MPToken that has
            // already been deleted
            mptAlice.authorize(
                {.account = bob,
                 .holderCount = 0,
                 .flags = tfMPTUnauthorize,
                 .err = tecOBJECT_NOT_FOUND});
        }

        // Test bad scenarios with allow-listing in MPTokenAuthorize (preclaim)
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTRequireAuth});

            // alice submits a tx without specifying a holder's account
            mptAlice.authorize({.err = tecNO_PERMISSION});

            // alice submits a tx to authorize a holder that hasn't created
            // a mptoken yet
            mptAlice.authorize({.holder = bob, .err = tecOBJECT_NOT_FOUND});

            // alice specifys a holder acct that doesn't exist
            mptAlice.authorize({.holder = cindy, .err = tecNO_DST});

            // bob now holds a mptoken object
            mptAlice.authorize({.account = bob, .holderCount = 1});

            // alice tries to unauthorize bob.
            // although tx is successful,
            // but nothing happens because bob hasn't been authorized yet
            mptAlice.authorize({.holder = bob, .flags = tfMPTUnauthorize});

            // alice authorizes bob
            // make sure bob's mptoken has set lsfMPTAuthorized
            mptAlice.authorize({.holder = bob});

            // alice tries authorizes bob again.
            // tx is successful, but bob is already authorized,
            // so no changes
            mptAlice.authorize({.holder = bob});

            // bob deletes his mptoken
            mptAlice.authorize(
                {.account = bob, .holderCount = 0, .flags = tfMPTUnauthorize});
        }

        // Test mptoken reserve requirement - first two mpts free (doApply)
        {
            Env env{*this, features};
            auto const acctReserve = env.current()->fees().accountReserve(0);
            auto const incReserve = env.current()->fees().increment;

            // 1 drop
            BEAST_EXPECT(incReserve > XRPAmount(1));
            MPTTester mptAlice1(
                env,
                alice,
                {.holders = {bob},
                 .xrpHolders = acctReserve + (incReserve - 1)});
            mptAlice1.create();

            MPTTester mptAlice2(env, alice, {.fund = false});
            mptAlice2.create();

            MPTTester mptAlice3(env, alice, {.fund = false});
            mptAlice3.create({.ownerCount = 3});

            // first mpt for free
            mptAlice1.authorize({.account = bob, .holderCount = 1});

            // second mpt free
            mptAlice2.authorize({.account = bob, .holderCount = 2});

            mptAlice3.authorize(
                {.account = bob, .err = tecINSUFFICIENT_RESERVE});

            env(pay(
                env.master, bob, drops(incReserve + incReserve + incReserve)));
            env.close();

            mptAlice3.authorize({.account = bob, .holderCount = 3});
        }
    }

    void
    testAuthorizeEnabled(FeatureBitset features)
    {
        testcase("Authorize Enabled");

        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        // Basic authorization without allowlisting
        {
            Env env{*this, features};

            // alice create mptissuance without allowisting
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1});

            // bob creates a mptoken
            mptAlice.authorize({.account = bob, .holderCount = 1});

            // bob deletes his mptoken
            mptAlice.authorize(
                {.account = bob, .holderCount = 0, .flags = tfMPTUnauthorize});
        }

        // With allowlisting
        {
            Env env{*this, features};

            // alice creates a mptokenissuance that requires authorization
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTRequireAuth});

            // bob creates a mptoken
            mptAlice.authorize({.account = bob, .holderCount = 1});

            // alice authorizes bob
            mptAlice.authorize({.account = alice, .holder = bob});

            // Unauthorize bob's mptoken
            mptAlice.authorize(
                {.account = alice,
                 .holder = bob,
                 .holderCount = 1,
                 .flags = tfMPTUnauthorize});

            mptAlice.authorize(
                {.account = bob, .holderCount = 0, .flags = tfMPTUnauthorize});
        }

        // Holder can have dangling MPToken even if issuance has been destroyed.
        // Make sure they can still delete/unauthorize the MPToken
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1});

            // bob creates a mptoken
            mptAlice.authorize({.account = bob, .holderCount = 1});

            // alice deletes her issuance
            mptAlice.destroy({.ownerCount = 0});

            // bob can delete his mptoken even though issuance is no longer
            // existent
            mptAlice.authorize(
                {.account = bob, .holderCount = 0, .flags = tfMPTUnauthorize});
        }
    }

    void
    testSetValidation(FeatureBitset features)
    {
        testcase("Validate set transaction");

        using namespace test::jtx;
        Account const alice("alice");  // issuer
        Account const bob("bob");      // holder
        Account const cindy("cindy");
        // Validate fields in MPTokenIssuanceSet (preflight)
        {
            Env env{*this, features - featureMPTokensV1};
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.set(
                {.account = bob,
                 .id = makeMptID(env.seq(alice), alice),
                 .err = temDISABLED});

            env.enableFeature(featureMPTokensV1);

            mptAlice.create({.ownerCount = 1, .holderCount = 0});

            mptAlice.authorize({.account = bob, .holderCount = 1});

            // test invalid flag - only valid flags are tfMPTLock (1) and Unlock
            // (2)
            mptAlice.set(
                {.account = alice,
                 .flags = 0x00000008,
                 .err = temINVALID_FLAG});

            // set both lock and unlock flags at the same time will fail
            mptAlice.set(
                {.account = alice,
                 .flags = tfMPTLock | tfMPTUnlock,
                 .err = temINVALID_FLAG});

            // if the holder is the same as the acct that submitted the tx,
            // tx fails
            mptAlice.set(
                {.account = alice,
                 .holder = alice,
                 .flags = tfMPTLock,
                 .err = temMALFORMED});
        }

        // Validate fields in MPTokenIssuanceSet (preclaim)
        // test when a mptokenissuance has disabled locking
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1});

            // alice tries to lock a mptissuance that has disabled locking
            mptAlice.set(
                {.account = alice,
                 .flags = tfMPTLock,
                 .err = tecNO_PERMISSION});

            // alice tries to unlock mptissuance that has disabled locking
            mptAlice.set(
                {.account = alice,
                 .flags = tfMPTUnlock,
                 .err = tecNO_PERMISSION});

            // issuer tries to lock a bob's mptoken that has disabled
            // locking
            mptAlice.set(
                {.account = alice,
                 .holder = bob,
                 .flags = tfMPTLock,
                 .err = tecNO_PERMISSION});

            // issuer tries to unlock a bob's mptoken that has disabled
            // locking
            mptAlice.set(
                {.account = alice,
                 .holder = bob,
                 .flags = tfMPTUnlock,
                 .err = tecNO_PERMISSION});
        }

        // Validate fields in MPTokenIssuanceSet (preclaim)
        // test when mptokenissuance has enabled locking
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob}});

            // alice trying to set when the mptissuance doesn't exist yet
            mptAlice.set(
                {.id = makeMptID(env.seq(alice), alice),
                 .flags = tfMPTLock,
                 .err = tecOBJECT_NOT_FOUND});

            // create a mptokenissuance with locking
            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanLock});

            // a non-issuer acct tries to set the mptissuance
            mptAlice.set(
                {.account = bob, .flags = tfMPTLock, .err = tecNO_PERMISSION});

            // trying to set a holder who doesn't have a mptoken
            mptAlice.set(
                {.holder = bob,
                 .flags = tfMPTLock,
                 .err = tecOBJECT_NOT_FOUND});

            // trying to set a holder who doesn't exist
            mptAlice.set(
                {.holder = cindy, .flags = tfMPTLock, .err = tecNO_DST});
        }
    }

    void
    testSetEnabled(FeatureBitset features)
    {
        testcase("Enabled set transaction");

        using namespace test::jtx;

        // Test locking and unlocking
        Env env{*this, features};
        Account const alice("alice");  // issuer
        Account const bob("bob");      // holder

        MPTTester mptAlice(env, alice, {.holders = {bob}});

        // create a mptokenissuance with locking
        mptAlice.create(
            {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanLock});

        mptAlice.authorize({.account = bob, .holderCount = 1});

        // locks bob's mptoken
        mptAlice.set({.account = alice, .holder = bob, .flags = tfMPTLock});

        // trying to lock bob's mptoken again will still succeed
        // but no changes to the objects
        mptAlice.set({.account = alice, .holder = bob, .flags = tfMPTLock});

        // alice locks the mptissuance
        mptAlice.set({.account = alice, .flags = tfMPTLock});

        // alice tries to lock up both mptissuance and mptoken again
        // it will not change the flags and both will remain locked.
        mptAlice.set({.account = alice, .flags = tfMPTLock});
        mptAlice.set({.account = alice, .holder = bob, .flags = tfMPTLock});

        // alice unlocks bob's mptoken
        mptAlice.set({.account = alice, .holder = bob, .flags = tfMPTUnlock});

        // locks up bob's mptoken again
        mptAlice.set({.account = alice, .holder = bob, .flags = tfMPTLock});

        // alice unlocks mptissuance
        mptAlice.set({.account = alice, .flags = tfMPTUnlock});

        // alice unlocks bob's mptoken
        mptAlice.set({.account = alice, .holder = bob, .flags = tfMPTUnlock});

        // alice unlocks mptissuance and bob's mptoken again despite that
        // they are already unlocked. Make sure this will not change the
        // flags
        mptAlice.set({.account = alice, .holder = bob, .flags = tfMPTUnlock});
        mptAlice.set({.account = alice, .flags = tfMPTUnlock});
    }

    void
    testPayment(FeatureBitset features)
    {
        testcase("Payment");

        using namespace test::jtx;
        Account const alice("alice");  // issuer
        Account const bob("bob");      // holder
        Account const carol("carol");  // holder

        // preflight validation

        // MPT is disabled
        {
            Env env{*this, features - featureMPTokensV1};
            Account const alice("alice");
            Account const bob("bob");

            env.fund(XRP(1'000), alice);
            env.fund(XRP(1'000), bob);
            STAmount mpt{MPTIssue{makeMptID(1, alice)}, UINT64_C(100)};

            env(pay(alice, bob, mpt), ter(temDISABLED));
        }

        // MPT is disabled, unsigned request
        {
            Env env{*this, features - featureMPTokensV1};
            Account const alice("alice");  // issuer
            Account const carol("carol");
            auto const USD = alice["USD"];

            env.fund(XRP(1'000), alice);
            env.fund(XRP(1'000), carol);
            STAmount mpt{MPTIssue{makeMptID(1, alice)}, UINT64_C(100)};

            Json::Value jv;
            jv[jss::secret] = alice.name();
            jv[jss::tx_json] = pay(alice, carol, mpt);
            jv[jss::tx_json][jss::Fee] = to_string(env.current()->fees().base);
            auto const jrr = env.rpc("json", "submit", to_string(jv));
            BEAST_EXPECT(jrr[jss::result][jss::engine_result] == "temDISABLED");
        }

        // Invalid flag
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .holderCount = 0});
            auto const MPT = mptAlice["MPT"];

            mptAlice.authorize({.account = bob});

            for (auto flags : {tfNoRippleDirect, tfLimitQuality})
                env(pay(alice, bob, MPT(10)),
                    txflags(flags),
                    ter(temINVALID_FLAG));
        }

        // Invalid combination of send, sendMax, deliverMin, paths
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const carol("carol");

            MPTTester mptAlice(env, alice, {.holders = {carol}});

            mptAlice.create({.ownerCount = 1, .holderCount = 0});

            mptAlice.authorize({.account = carol});

            // sendMax and DeliverMin are valid XRP amount,
            // but is invalid combination with MPT amount
            auto const MPT = mptAlice["MPT"];
            env(pay(alice, carol, MPT(100)),
                sendmax(XRP(100)),
                ter(temMALFORMED));
            env(pay(alice, carol, MPT(100)),
                delivermin(XRP(100)),
                ter(temBAD_AMOUNT));
            // sendMax MPT is invalid with IOU or XRP
            auto const USD = alice["USD"];
            env(pay(alice, carol, USD(100)),
                sendmax(MPT(100)),
                ter(temMALFORMED));
            env(pay(alice, carol, XRP(100)),
                sendmax(MPT(100)),
                ter(temMALFORMED));
            env(pay(alice, carol, USD(100)),
                delivermin(MPT(100)),
                ter(temBAD_AMOUNT));
            env(pay(alice, carol, XRP(100)),
                delivermin(MPT(100)),
                ter(temBAD_AMOUNT));
            // sendmax and amount are different MPT issue
            test::jtx::MPT const MPT1(
                "MPT", makeMptID(env.seq(alice) + 10, alice));
            env(pay(alice, carol, MPT1(100)),
                sendmax(MPT(100)),
                ter(temMALFORMED));
            // paths is invalid
            env(pay(alice, carol, MPT(100)), path(~USD), ter(temMALFORMED));
        }

        // build_path is invalid if MPT
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const carol("carol");

            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create({.ownerCount = 1, .holderCount = 0});
            auto const MPT = mptAlice["MPT"];

            mptAlice.authorize({.account = carol});

            Json::Value payment;
            payment[jss::secret] = alice.name();
            payment[jss::tx_json] = pay(alice, carol, MPT(100));

            payment[jss::build_path] = true;
            auto jrr = env.rpc("json", "submit", to_string(payment));
            BEAST_EXPECT(jrr[jss::result][jss::error] == "invalidParams");
            BEAST_EXPECT(
                jrr[jss::result][jss::error_message] ==
                "Field 'build_path' not allowed in this context.");
        }

        // Can't pay negative amount
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create({.ownerCount = 1, .holderCount = 0});
            auto const MPT = mptAlice["MPT"];

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});

            mptAlice.pay(alice, bob, -1, temBAD_AMOUNT);

            mptAlice.pay(bob, carol, -1, temBAD_AMOUNT);

            mptAlice.pay(bob, alice, -1, temBAD_AMOUNT);

            env(pay(alice, bob, MPT(10)), sendmax(MPT(-1)), ter(temBAD_AMOUNT));
        }

        // Pay to self
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .holderCount = 0});

            mptAlice.authorize({.account = bob});

            mptAlice.pay(bob, bob, 10, temREDUNDANT);
        }

        // preclaim validation

        // Destination doesn't exist
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .holderCount = 0});

            mptAlice.authorize({.account = bob});

            Account const bad{"bad"};
            env.memoize(bad);

            mptAlice.pay(bob, bad, 10, tecNO_DST);
        }

        // apply validation

        // If RequireAuth is enabled, Payment fails if the receiver is not
        // authorized
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTRequireAuth | tfMPTCanTransfer});

            mptAlice.authorize({.account = bob});

            mptAlice.pay(alice, bob, 100, tecNO_AUTH);
        }

        // If RequireAuth is enabled, Payment fails if the sender is not
        // authorized
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTRequireAuth | tfMPTCanTransfer});

            // bob creates an empty MPToken
            mptAlice.authorize({.account = bob});

            // alice authorizes bob to hold funds
            mptAlice.authorize({.account = alice, .holder = bob});

            // alice sends 100 MPT to bob
            mptAlice.pay(alice, bob, 100);

            // alice UNAUTHORIZES bob
            mptAlice.authorize(
                {.account = alice, .holder = bob, .flags = tfMPTUnauthorize});

            // bob fails to send back to alice because he is no longer
            // authorize to move his funds!
            mptAlice.pay(bob, alice, 100, tecNO_AUTH);
        }

        // Non-issuer cannot send to each other if MPTCanTransfer isn't set
        {
            Env env(*this, features);
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const cindy{"cindy"};

            MPTTester mptAlice(env, alice, {.holders = {bob, cindy}});

            // alice creates issuance without MPTCanTransfer
            mptAlice.create({.ownerCount = 1, .holderCount = 0});

            // bob creates a MPToken
            mptAlice.authorize({.account = bob});

            // cindy creates a MPToken
            mptAlice.authorize({.account = cindy});

            // alice pays bob 100 tokens
            mptAlice.pay(alice, bob, 100);

            // bob tries to send cindy 10 tokens, but fails because canTransfer
            // is off
            mptAlice.pay(bob, cindy, 10, tecNO_AUTH);

            // bob can send back to alice(issuer) just fine
            mptAlice.pay(bob, alice, 10);
        }

        // Holder is not authorized
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});

            // issuer to holder
            mptAlice.pay(alice, bob, 100, tecNO_AUTH);

            // holder to issuer
            mptAlice.pay(bob, alice, 100, tecNO_AUTH);

            // holder to holder
            mptAlice.pay(bob, carol, 50, tecNO_AUTH);
        }

        // Payer doesn't have enough funds
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer});

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});

            mptAlice.pay(alice, bob, 100);

            // Pay to another holder
            mptAlice.pay(bob, carol, 101, tecPATH_PARTIAL);

            // Pay to the issuer
            mptAlice.pay(bob, alice, 101, tecPATH_PARTIAL);
        }

        // MPT is locked
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create(
                {.ownerCount = 1, .flags = tfMPTCanLock | tfMPTCanTransfer});

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});

            mptAlice.pay(alice, bob, 100);
            mptAlice.pay(alice, carol, 100);

            // Global lock
            mptAlice.set({.account = alice, .flags = tfMPTLock});
            // Can't send between holders
            mptAlice.pay(bob, carol, 1, tecLOCKED);
            mptAlice.pay(carol, bob, 2, tecLOCKED);
            // Issuer can send
            mptAlice.pay(alice, bob, 3);
            // Holder can send back to issuer
            mptAlice.pay(bob, alice, 4);

            // Global unlock
            mptAlice.set({.account = alice, .flags = tfMPTUnlock});
            // Individual lock
            mptAlice.set({.account = alice, .holder = bob, .flags = tfMPTLock});
            // Can't send between holders
            mptAlice.pay(bob, carol, 5, tecLOCKED);
            mptAlice.pay(carol, bob, 6, tecLOCKED);
            // Issuer can send
            mptAlice.pay(alice, bob, 7);
            // Holder can send back to issuer
            mptAlice.pay(bob, alice, 8);
        }

        // Transfer fee
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            // Transfer fee is 10%
            mptAlice.create(
                {.transferFee = 10'000,
                 .ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanTransfer});

            // Holders create MPToken
            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});

            // Payment between the issuer and the holder, no transfer fee.
            mptAlice.pay(alice, bob, 2'000);

            // Payment between the holder and the issuer, no transfer fee.
            mptAlice.pay(bob, alice, 1'000);
            BEAST_EXPECT(mptAlice.checkMPTokenAmount(bob, 1'000));

            // Payment between the holders. The sender doesn't have
            // enough funds to cover the transfer fee.
            mptAlice.pay(bob, carol, 1'000, tecPATH_PARTIAL);

            // Payment between the holders. The sender has enough funds
            // but SendMax is not included.
            mptAlice.pay(bob, carol, 100, tecPATH_PARTIAL);

            auto const MPT = mptAlice["MPT"];
            // SendMax doesn't cover the fee
            env(pay(bob, carol, MPT(100)),
                sendmax(MPT(109)),
                ter(tecPATH_PARTIAL));

            // Payment succeeds if sufficient SendMax is included.
            // 100 to carol, 10 to issuer
            env(pay(bob, carol, MPT(100)), sendmax(MPT(110)));
            // 100 to carol, 10 to issuer
            env(pay(bob, carol, MPT(100)), sendmax(MPT(115)));
            BEAST_EXPECT(mptAlice.checkMPTokenAmount(bob, 780));
            BEAST_EXPECT(mptAlice.checkMPTokenAmount(carol, 200));
            // Payment succeeds if partial payment even if
            // SendMax is less than deliver amount
            env(pay(bob, carol, MPT(100)),
                sendmax(MPT(90)),
                txflags(tfPartialPayment));
            // 82 to carol, 8 to issuer (90 / 1.1 ~ 81.81 (rounded to nearest) =
            // 82)
            BEAST_EXPECT(mptAlice.checkMPTokenAmount(bob, 690));
            BEAST_EXPECT(mptAlice.checkMPTokenAmount(carol, 282));
        }

        // Insufficient SendMax with no transfer fee
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});

            // Holders create MPToken
            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});
            mptAlice.pay(alice, bob, 1'000);

            auto const MPT = mptAlice["MPT"];
            // SendMax is less than the amount
            env(pay(bob, carol, MPT(100)),
                sendmax(MPT(99)),
                ter(tecPATH_PARTIAL));
            env(pay(bob, alice, MPT(100)),
                sendmax(MPT(99)),
                ter(tecPATH_PARTIAL));

            // Payment succeeds if sufficient SendMax is included.
            env(pay(bob, carol, MPT(100)), sendmax(MPT(100)));
            BEAST_EXPECT(mptAlice.checkMPTokenAmount(carol, 100));
            // Payment succeeds if partial payment
            env(pay(bob, carol, MPT(100)),
                sendmax(MPT(99)),
                txflags(tfPartialPayment));
            BEAST_EXPECT(mptAlice.checkMPTokenAmount(carol, 199));
        }

        // DeliverMin
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});

            // Holders create MPToken
            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});
            mptAlice.pay(alice, bob, 1'000);

            auto const MPT = mptAlice["MPT"];
            // Fails even with the partial payment because
            // deliver amount < deliverMin
            env(pay(bob, alice, MPT(100)),
                sendmax(MPT(99)),
                delivermin(MPT(100)),
                txflags(tfPartialPayment),
                ter(tecPATH_PARTIAL));
            // Payment succeeds if deliver amount >= deliverMin
            env(pay(bob, alice, MPT(100)),
                sendmax(MPT(99)),
                delivermin(MPT(99)),
                txflags(tfPartialPayment));
        }

        // Issuer fails trying to send more than the maximum amount allowed
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.maxAmt = 100,
                 .ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanTransfer});

            mptAlice.authorize({.account = bob});

            // issuer sends holder the max amount allowed
            mptAlice.pay(alice, bob, 100);

            // issuer tries to exceed max amount
            mptAlice.pay(alice, bob, 1, tecPATH_PARTIAL);
        }

        // Issuer fails trying to send more than the default maximum
        // amount allowed
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .holderCount = 0});

            mptAlice.authorize({.account = bob});

            // issuer sends holder the default max amount allowed
            mptAlice.pay(alice, bob, maxMPTokenAmount);

            // issuer tries to exceed max amount
            mptAlice.pay(alice, bob, 1, tecPATH_PARTIAL);
        }

        // Pay more than max amount fails in the json parser before
        // transactor is called
        {
            Env env{*this, features};
            env.fund(XRP(1'000), alice, bob);
            STAmount mpt{MPTIssue{makeMptID(1, alice)}, UINT64_C(100)};
            Json::Value jv;
            jv[jss::secret] = alice.name();
            jv[jss::tx_json] = pay(alice, bob, mpt);
            jv[jss::tx_json][jss::Amount][jss::value] =
                to_string(maxMPTokenAmount + 1);
            auto const jrr = env.rpc("json", "submit", to_string(jv));
            BEAST_EXPECT(jrr[jss::result][jss::error] == "invalidParams");
        }

        // Pay maximum amount with the transfer fee, SendMax, and
        // partial payment
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create(
                {.maxAmt = 10'000,
                 .transferFee = 100,
                 .ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanTransfer});
            auto const MPT = mptAlice["MPT"];

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});

            // issuer sends holder the max amount allowed
            mptAlice.pay(alice, bob, 10'000);

            // payment between the holders
            env(pay(bob, carol, MPT(10'000)),
                sendmax(MPT(10'000)),
                txflags(tfPartialPayment));
            // Verify the metadata
            auto const meta = env.meta()->getJson(
                JsonOptions::none)[sfAffectedNodes.fieldName];
            // Issuer got 10 in the transfer fees
            BEAST_EXPECT(
                meta[0u][sfModifiedNode.fieldName][sfFinalFields.fieldName]
                    [sfOutstandingAmount.fieldName] == "9990");
            // Destination account got 9'990
            BEAST_EXPECT(
                meta[1u][sfModifiedNode.fieldName][sfFinalFields.fieldName]
                    [sfMPTAmount.fieldName] == "9990");
            // Source account spent 10'000
            BEAST_EXPECT(
                meta[2u][sfModifiedNode.fieldName][sfPreviousFields.fieldName]
                    [sfMPTAmount.fieldName] == "10000");
            BEAST_EXPECT(
                !meta[2u][sfModifiedNode.fieldName][sfFinalFields.fieldName]
                     .isMember(sfMPTAmount.fieldName));

            // payment between the holders fails without
            // partial payment
            env(pay(bob, carol, MPT(10'000)),
                sendmax(MPT(10'000)),
                ter(tecPATH_PARTIAL));
        }

        // Pay maximum allowed amount
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create(
                {.maxAmt = maxMPTokenAmount,
                 .ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanTransfer});
            auto const MPT = mptAlice["MPT"];

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});

            // issuer sends holder the max amount allowed
            mptAlice.pay(alice, bob, maxMPTokenAmount);
            BEAST_EXPECT(
                mptAlice.checkMPTokenOutstandingAmount(maxMPTokenAmount));

            // payment between the holders
            mptAlice.pay(bob, carol, maxMPTokenAmount);
            BEAST_EXPECT(
                mptAlice.checkMPTokenOutstandingAmount(maxMPTokenAmount));
            // holder pays back to the issuer
            mptAlice.pay(carol, alice, maxMPTokenAmount);
            BEAST_EXPECT(mptAlice.checkMPTokenOutstandingAmount(0));
        }

        // Issuer fails trying to send fund after issuance was destroyed
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .holderCount = 0});

            mptAlice.authorize({.account = bob});

            // alice destroys issuance
            mptAlice.destroy({.ownerCount = 0});

            // alice tries to send bob fund after issuance is destroyed, should
            // fail.
            mptAlice.pay(alice, bob, 100, tecOBJECT_NOT_FOUND);
        }

        // Non-existent issuance
        {
            Env env{*this, features};

            env.fund(XRP(1'000), alice, bob);

            STAmount const mpt{MPTID{0}, 100};
            env(pay(alice, bob, mpt), ter(tecOBJECT_NOT_FOUND));
        }

        // Issuer fails trying to send to an account, which doesn't own MPT for
        // an issuance that was destroyed
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .holderCount = 0});

            // alice destroys issuance
            mptAlice.destroy({.ownerCount = 0});

            // alice tries to send bob who doesn't own the MPT after issuance is
            // destroyed, it should fail
            mptAlice.pay(alice, bob, 100, tecOBJECT_NOT_FOUND);
        }

        // Issuers issues maximum amount of MPT to a holder, the holder should
        // be able to transfer the max amount to someone else
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const carol("bob");
            Account const bob("carol");

            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create(
                {.maxAmt = 100, .ownerCount = 1, .flags = tfMPTCanTransfer});

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});

            mptAlice.pay(alice, bob, 100);

            // transfer max amount to another holder
            mptAlice.pay(bob, carol, 100);
        }

        // Simple payment
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});

            // issuer to holder
            mptAlice.pay(alice, bob, 100);

            // holder to issuer
            mptAlice.pay(bob, alice, 100);

            // holder to holder
            mptAlice.pay(alice, bob, 100);
            mptAlice.pay(bob, carol, 50);
        }
    }

    void
    testDepositPreauth()
    {
        testcase("DepositPreauth");

        using namespace test::jtx;
        Account const alice("alice");  // issuer
        Account const bob("bob");      // holder
        Account const diana("diana");
        Account const dpIssuer("dpIssuer");  // holder

        const char credType[] = "abcde";

        {
            Env env(*this);

            env.fund(XRP(50000), diana, dpIssuer);
            env.close();

            MPTTester mptAlice(env, alice, {.holders = {bob}});
            mptAlice.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTRequireAuth | tfMPTCanTransfer});

            env(pay(diana, bob, XRP(500)));
            env.close();

            // bob creates an empty MPToken
            mptAlice.authorize({.account = bob});
            // alice authorizes bob to hold funds
            mptAlice.authorize({.account = alice, .holder = bob});

            // Bob require preauthorization
            env(fset(bob, asfDepositAuth));
            env.close();

            // alice try to send 100 MPT to bob, not authorized
            mptAlice.pay(alice, bob, 100, tecNO_PERMISSION);
            env.close();

            // Bob authorize alice
            env(deposit::auth(bob, alice));
            env.close();

            // alice sends 100 MPT to bob
            mptAlice.pay(alice, bob, 100);
            env.close();

            // Create credentials
            env(credentials::create(alice, dpIssuer, credType));
            env.close();
            env(credentials::accept(alice, dpIssuer, credType));
            env.close();
            auto const jv =
                credentials::ledgerEntry(env, alice, dpIssuer, credType);
            std::string const credIdx = jv[jss::result][jss::index].asString();

            // alice sends 100 MPT to bob with credentials which aren't required
            mptAlice.pay(alice, bob, 100, tesSUCCESS, {{credIdx}});
            env.close();

            // Bob revoke authorization
            env(deposit::unauth(bob, alice));
            env.close();

            // alice try to send 100 MPT to bob, not authorized
            mptAlice.pay(alice, bob, 100, tecNO_PERMISSION);
            env.close();

            // alice sends 100 MPT to bob with credentials, not authorized
            mptAlice.pay(alice, bob, 100, tecNO_PERMISSION, {{credIdx}});
            env.close();

            // Bob authorize credentials
            env(deposit::authCredentials(bob, {{dpIssuer, credType}}));
            env.close();

            // alice try to send 100 MPT to bob, not authorized
            mptAlice.pay(alice, bob, 100, tecNO_PERMISSION);
            env.close();

            // alice sends 100 MPT to bob with credentials
            mptAlice.pay(alice, bob, 100, tesSUCCESS, {{credIdx}});
            env.close();
        }

        testcase("DepositPreauth disabled featureCredentials");
        {
            Env env(*this, supported_amendments() - featureCredentials);

            std::string const credIdx =
                "D007AE4B6E1274B4AF872588267B810C2F82716726351D1C7D38D3E5499FC6"
                "E2";

            env.fund(XRP(50000), diana, dpIssuer);
            env.close();

            MPTTester mptAlice(env, alice, {.holders = {bob}});
            mptAlice.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTRequireAuth | tfMPTCanTransfer});

            env(pay(diana, bob, XRP(500)));
            env.close();

            // bob creates an empty MPToken
            mptAlice.authorize({.account = bob});
            // alice authorizes bob to hold funds
            mptAlice.authorize({.account = alice, .holder = bob});

            // Bob require preauthorization
            env(fset(bob, asfDepositAuth));
            env.close();

            // alice try to send 100 MPT to bob, not authorized
            mptAlice.pay(alice, bob, 100, tecNO_PERMISSION);
            env.close();

            // alice try to send 100 MPT to bob with credentials, amendment
            // disabled
            mptAlice.pay(alice, bob, 100, temDISABLED, {{credIdx}});
            env.close();

            // Bob authorize alice
            env(deposit::auth(bob, alice));
            env.close();

            // alice sends 100 MPT to bob
            mptAlice.pay(alice, bob, 100);
            env.close();

            // alice sends 100 MPT to bob with credentials, amendment disabled
            mptAlice.pay(alice, bob, 100, temDISABLED, {{credIdx}});
            env.close();

            // Bob revoke authorization
            env(deposit::unauth(bob, alice));
            env.close();

            // alice try to send 100 MPT to bob
            mptAlice.pay(alice, bob, 100, tecNO_PERMISSION);
            env.close();

            // alice sends 100 MPT to bob with credentials, amendment disabled
            mptAlice.pay(alice, bob, 100, temDISABLED, {{credIdx}});
            env.close();
        }
    }

    void
    testMPTInvalidInTx(FeatureBitset features)
    {
        testcase("MPT Amount Invalid in Transaction");
        using namespace test::jtx;

        // Validate that every transaction with an amount field,
        // which doesn't support MPT, fails.

        // keyed by transaction + amount field
        std::set<std::string> txWithAmounts;
        for (auto const& format : TxFormats::getInstance())
        {
            for (auto const& e : format.getSOTemplate())
            {
                // Transaction has amount fields.
                // Exclude pseudo-transaction SetFee. Don't consider
                // the Fee field since it's included in every transaction.
                if (e.supportMPT() == soeMPTNotSupported &&
                    e.sField().getName() != jss::Fee &&
                    format.getName() != jss::SetFee)
                {
                    txWithAmounts.insert(
                        format.getName() + e.sField().fieldName);
                    break;
                }
            }
        }

        Account const alice("alice");
        auto const USD = alice["USD"];
        Account const carol("carol");
        MPTIssue issue(makeMptID(1, alice));
        STAmount mpt{issue, UINT64_C(100)};
        auto const jvb = bridge(alice, USD, alice, USD);
        for (auto const& feature : {features, features - featureMPTokensV1})
        {
            Env env{*this, feature};
            env.fund(XRP(1'000), alice);
            env.fund(XRP(1'000), carol);
            auto test = [&](Json::Value const& jv,
                            std::string const& amtField) {
                txWithAmounts.erase(
                    jv[jss::TransactionType].asString() + amtField);

                // tx is signed
                auto jtx = env.jt(jv);
                Serializer s;
                jtx.stx->add(s);
                auto jrr = env.rpc("submit", strHex(s.slice()));
                BEAST_EXPECT(
                    jrr[jss::result][jss::error] == "invalidTransaction");

                // tx is unsigned
                Json::Value jv1;
                jv1[jss::secret] = alice.name();
                jv1[jss::tx_json] = jv;
                jrr = env.rpc("json", "submit", to_string(jv1));
                BEAST_EXPECT(jrr[jss::result][jss::error] == "invalidParams");

                jrr = env.rpc("json", "sign", to_string(jv1));
                BEAST_EXPECT(jrr[jss::result][jss::error] == "invalidParams");
            };
            // All transactions with sfAmount, which don't support MPT
            // and transactions with amount fields, which can't be MPT

            // AMMCreate
            auto ammCreate = [&](SField const& field) {
                Json::Value jv;
                jv[jss::TransactionType] = jss::AMMCreate;
                jv[jss::Account] = alice.human();
                jv[jss::Amount] = (field.fieldName == sfAmount.fieldName)
                    ? mpt.getJson(JsonOptions::none)
                    : "100000000";
                jv[jss::Amount2] = (field.fieldName == sfAmount2.fieldName)
                    ? mpt.getJson(JsonOptions::none)
                    : "100000000";
                jv[jss::TradingFee] = 0;
                test(jv, field.fieldName);
            };
            ammCreate(sfAmount);
            ammCreate(sfAmount2);
            // AMMDeposit
            auto ammDeposit = [&](SField const& field) {
                Json::Value jv;
                jv[jss::TransactionType] = jss::AMMDeposit;
                jv[jss::Account] = alice.human();
                jv[jss::Asset] = to_json(xrpIssue());
                jv[jss::Asset2] = to_json(USD.issue());
                jv[field.fieldName] = mpt.getJson(JsonOptions::none);
                jv[jss::Flags] = tfSingleAsset;
                test(jv, field.fieldName);
            };
            for (SField const& field :
                 {std::ref(sfAmount),
                  std::ref(sfAmount2),
                  std::ref(sfEPrice),
                  std::ref(sfLPTokenOut)})
                ammDeposit(field);
            // AMMWithdraw
            auto ammWithdraw = [&](SField const& field) {
                Json::Value jv;
                jv[jss::TransactionType] = jss::AMMWithdraw;
                jv[jss::Account] = alice.human();
                jv[jss::Asset] = to_json(xrpIssue());
                jv[jss::Asset2] = to_json(USD.issue());
                jv[jss::Flags] = tfSingleAsset;
                jv[field.fieldName] = mpt.getJson(JsonOptions::none);
                test(jv, field.fieldName);
            };
            ammWithdraw(sfAmount);
            for (SField const& field :
                 {std::ref(sfAmount2),
                  std::ref(sfEPrice),
                  std::ref(sfLPTokenIn)})
                ammWithdraw(field);
            // AMMBid
            auto ammBid = [&](SField const& field) {
                Json::Value jv;
                jv[jss::TransactionType] = jss::AMMBid;
                jv[jss::Account] = alice.human();
                jv[jss::Asset] = to_json(xrpIssue());
                jv[jss::Asset2] = to_json(USD.issue());
                jv[field.fieldName] = mpt.getJson(JsonOptions::none);
                test(jv, field.fieldName);
            };
            ammBid(sfBidMin);
            ammBid(sfBidMax);
            // AMMClawback
            {
                Json::Value jv;
                jv[jss::TransactionType] = jss::AMMClawback;
                jv[jss::Account] = alice.human();
                jv[jss::Holder] = carol.human();
                jv[jss::Asset] = to_json(xrpIssue());
                jv[jss::Asset2] = to_json(USD.issue());
                jv[jss::Amount] = mpt.getJson(JsonOptions::none);
                test(jv, jss::Amount.c_str());
            }
            // CheckCash
            auto checkCash = [&](SField const& field) {
                Json::Value jv;
                jv[jss::TransactionType] = jss::CheckCash;
                jv[jss::Account] = alice.human();
                jv[sfCheckID.fieldName] = to_string(uint256{1});
                jv[field.fieldName] = mpt.getJson(JsonOptions::none);
                test(jv, field.fieldName);
            };
            checkCash(sfAmount);
            checkCash(sfDeliverMin);
            // CheckCreate
            {
                Json::Value jv;
                jv[jss::TransactionType] = jss::CheckCreate;
                jv[jss::Account] = alice.human();
                jv[jss::Destination] = carol.human();
                jv[jss::SendMax] = mpt.getJson(JsonOptions::none);
                test(jv, jss::SendMax.c_str());
            }
            // EscrowCreate
            {
                Json::Value jv;
                jv[jss::TransactionType] = jss::EscrowCreate;
                jv[jss::Account] = alice.human();
                jv[jss::Destination] = carol.human();
                jv[jss::Amount] = mpt.getJson(JsonOptions::none);
                test(jv, jss::Amount.c_str());
            }
            // OfferCreate
            {
                Json::Value jv = offer(alice, USD(100), mpt);
                test(jv, jss::TakerPays.c_str());
                jv = offer(alice, mpt, USD(100));
                test(jv, jss::TakerGets.c_str());
            }
            // PaymentChannelCreate
            {
                Json::Value jv;
                jv[jss::TransactionType] = jss::PaymentChannelCreate;
                jv[jss::Account] = alice.human();
                jv[jss::Destination] = carol.human();
                jv[jss::SettleDelay] = 1;
                jv[sfPublicKey.fieldName] = strHex(alice.pk().slice());
                jv[jss::Amount] = mpt.getJson(JsonOptions::none);
                test(jv, jss::Amount.c_str());
            }
            // PaymentChannelFund
            {
                Json::Value jv;
                jv[jss::TransactionType] = jss::PaymentChannelFund;
                jv[jss::Account] = alice.human();
                jv[sfChannel.fieldName] = to_string(uint256{1});
                jv[jss::Amount] = mpt.getJson(JsonOptions::none);
                test(jv, jss::Amount.c_str());
            }
            // PaymentChannelClaim
            {
                Json::Value jv;
                jv[jss::TransactionType] = jss::PaymentChannelClaim;
                jv[jss::Account] = alice.human();
                jv[sfChannel.fieldName] = to_string(uint256{1});
                jv[jss::Amount] = mpt.getJson(JsonOptions::none);
                test(jv, jss::Amount.c_str());
            }
            // NFTokenCreateOffer
            {
                Json::Value jv;
                jv[jss::TransactionType] = jss::NFTokenCreateOffer;
                jv[jss::Account] = alice.human();
                jv[sfNFTokenID.fieldName] = to_string(uint256{1});
                jv[jss::Amount] = mpt.getJson(JsonOptions::none);
                test(jv, jss::Amount.c_str());
            }
            // NFTokenAcceptOffer
            {
                Json::Value jv;
                jv[jss::TransactionType] = jss::NFTokenAcceptOffer;
                jv[jss::Account] = alice.human();
                jv[sfNFTokenBrokerFee.fieldName] =
                    mpt.getJson(JsonOptions::none);
                test(jv, sfNFTokenBrokerFee.fieldName);
            }
            // NFTokenMint
            {
                Json::Value jv;
                jv[jss::TransactionType] = jss::NFTokenMint;
                jv[jss::Account] = alice.human();
                jv[sfNFTokenTaxon.fieldName] = 1;
                jv[jss::Amount] = mpt.getJson(JsonOptions::none);
                test(jv, jss::Amount.c_str());
            }
            // TrustSet
            auto trustSet = [&](SField const& field) {
                Json::Value jv;
                jv[jss::TransactionType] = jss::TrustSet;
                jv[jss::Account] = alice.human();
                jv[jss::Flags] = 0;
                jv[field.fieldName] = mpt.getJson(JsonOptions::none);
                test(jv, field.fieldName);
            };
            trustSet(sfLimitAmount);
            trustSet(sfFee);
            // XChainCommit
            {
                Json::Value const jv = xchain_commit(alice, jvb, 1, mpt);
                test(jv, jss::Amount.c_str());
            }
            // XChainClaim
            {
                Json::Value const jv = xchain_claim(alice, jvb, 1, mpt, alice);
                test(jv, jss::Amount.c_str());
            }
            // XChainCreateClaimID
            {
                Json::Value const jv =
                    xchain_create_claim_id(alice, jvb, mpt, alice);
                test(jv, sfSignatureReward.fieldName);
            }
            // XChainAddClaimAttestation
            {
                Json::Value const jv = claim_attestation(
                    alice,
                    jvb,
                    alice,
                    mpt,
                    alice,
                    true,
                    1,
                    alice,
                    signer(alice));
                test(jv, jss::Amount.c_str());
            }
            // XChainAddAccountCreateAttestation
            {
                Json::Value jv = create_account_attestation(
                    alice,
                    jvb,
                    alice,
                    mpt,
                    XRP(10),
                    alice,
                    false,
                    1,
                    alice,
                    signer(alice));
                for (auto const& field :
                     {sfAmount.fieldName, sfSignatureReward.fieldName})
                {
                    jv[field] = mpt.getJson(JsonOptions::none);
                    test(jv, field);
                }
            }
            // XChainAccountCreateCommit
            {
                Json::Value jv = sidechain_xchain_account_create(
                    alice, jvb, alice, mpt, XRP(10));
                for (auto const& field :
                     {sfAmount.fieldName, sfSignatureReward.fieldName})
                {
                    jv[field] = mpt.getJson(JsonOptions::none);
                    test(jv, field);
                }
            }
            // XChain[Create|Modify]Bridge
            auto bridgeTx = [&](Json::StaticString const& tt,
                                STAmount const& rewardAmount,
                                STAmount const& minAccountAmount,
                                std::string const& field) {
                Json::Value jv;
                jv[jss::TransactionType] = tt;
                jv[jss::Account] = alice.human();
                jv[sfXChainBridge.fieldName] = jvb;
                jv[sfSignatureReward.fieldName] =
                    rewardAmount.getJson(JsonOptions::none);
                jv[sfMinAccountCreateAmount.fieldName] =
                    minAccountAmount.getJson(JsonOptions::none);
                test(jv, field);
            };
            auto reward = STAmount{sfSignatureReward, mpt};
            auto minAmount = STAmount{sfMinAccountCreateAmount, USD(10)};
            for (SField const& field :
                 {std::ref(sfSignatureReward),
                  std::ref(sfMinAccountCreateAmount)})
            {
                bridgeTx(
                    jss::XChainCreateBridge,
                    reward,
                    minAmount,
                    field.fieldName);
                bridgeTx(
                    jss::XChainModifyBridge,
                    reward,
                    minAmount,
                    field.fieldName);
                reward = STAmount{sfSignatureReward, USD(10)};
                minAmount = STAmount{sfMinAccountCreateAmount, mpt};
            }
        }
        BEAST_EXPECT(txWithAmounts.empty());
    }

    void
    testTxJsonMetaFields(FeatureBitset features)
    {
        // checks synthetically injected mptissuanceid from  `tx` response
        testcase("Test synthetic fields from tx response");

        using namespace test::jtx;

        Account const alice{"alice"};

        Env env{*this, features};
        MPTTester mptAlice(env, alice);

        mptAlice.create();

        std::string const txHash{
            env.tx()->getJson(JsonOptions::none)[jss::hash].asString()};
        BEAST_EXPECTS(
            txHash ==
                "E11F0E0CA14219922B7881F060B9CEE67CFBC87E4049A441ED2AE348FF8FAC"
                "0E",
            txHash);
        Json::Value const meta = env.rpc("tx", txHash)[jss::result][jss::meta];
        auto const id = meta[jss::mpt_issuance_id].asString();
        // Expect mpt_issuance_id field
        BEAST_EXPECT(meta.isMember(jss::mpt_issuance_id));
        BEAST_EXPECT(id == to_string(mptAlice.issuanceID()));
        BEAST_EXPECTS(
            id == "00000004AE123A8556F3CF91154711376AFB0F894F832B3D", id);
    }

    void
    testClawbackValidation(FeatureBitset features)
    {
        testcase("MPT clawback validations");
        using namespace test::jtx;

        // Make sure clawback cannot work when featureMPTokensV1 is disabled
        {
            Env env(*this, features - featureMPTokensV1);
            Account const alice{"alice"};
            Account const bob{"bob"};

            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const USD = alice["USD"];
            auto const mpt = ripple::test::jtx::MPT(
                alice.name(), makeMptID(env.seq(alice), alice));

            env(claw(alice, bob["USD"](5), bob), ter(temMALFORMED));
            env.close();

            env(claw(alice, mpt(5)), ter(temDISABLED));
            env.close();

            env(claw(alice, mpt(5), bob), ter(temDISABLED));
            env.close();
        }

        // Test preflight
        {
            Env env(*this, features);
            Account const alice{"alice"};
            Account const bob{"bob"};

            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const USD = alice["USD"];
            auto const mpt = ripple::test::jtx::MPT(
                alice.name(), makeMptID(env.seq(alice), alice));

            // clawing back IOU from a MPT holder fails
            env(claw(alice, bob["USD"](5), bob), ter(temMALFORMED));
            env.close();

            // clawing back MPT without specifying a holder fails
            env(claw(alice, mpt(5)), ter(temMALFORMED));
            env.close();

            // clawing back zero amount fails
            env(claw(alice, mpt(0), bob), ter(temBAD_AMOUNT));
            env.close();

            // alice can't claw back from herself
            env(claw(alice, mpt(5), alice), ter(temMALFORMED));
            env.close();

            // can't clawback negative amount
            env(claw(alice, mpt(-1), bob), ter(temBAD_AMOUNT));
            env.close();
        }

        // Preclaim - clawback fails when MPTCanClawback is disabled on issuance
        {
            Env env(*this, features);
            Account const alice{"alice"};
            Account const bob{"bob"};

            MPTTester mptAlice(env, alice, {.holders = {bob}});

            // enable asfAllowTrustLineClawback for alice
            env(fset(alice, asfAllowTrustLineClawback));
            env.close();
            env.require(flags(alice, asfAllowTrustLineClawback));

            // Create issuance without enabling clawback
            mptAlice.create({.ownerCount = 1, .holderCount = 0});

            mptAlice.authorize({.account = bob});

            mptAlice.pay(alice, bob, 100);

            // alice cannot clawback before she didn't enable MPTCanClawback
            // asfAllowTrustLineClawback has no effect
            mptAlice.claw(alice, bob, 1, tecNO_PERMISSION);
        }

        // Preclaim - test various scenarios
        {
            Env env(*this, features);
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const carol{"carol"};
            env.fund(XRP(1000), carol);
            env.close();
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            auto const fakeMpt = ripple::test::jtx::MPT(
                alice.name(), makeMptID(env.seq(alice), alice));

            // issuer tries to clawback MPT where issuance doesn't exist
            env(claw(alice, fakeMpt(5), bob), ter(tecOBJECT_NOT_FOUND));
            env.close();

            // alice creates issuance
            mptAlice.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanClawback});

            // alice tries to clawback from someone who doesn't have MPToken
            mptAlice.claw(alice, bob, 1, tecOBJECT_NOT_FOUND);

            // bob creates a MPToken
            mptAlice.authorize({.account = bob});

            // clawback fails because bob currently has a balance of zero
            mptAlice.claw(alice, bob, 1, tecINSUFFICIENT_FUNDS);

            // alice pays bob 100 tokens
            mptAlice.pay(alice, bob, 100);

            // carol fails tries to clawback from bob because he is not the
            // issuer
            mptAlice.claw(carol, bob, 1, tecNO_PERMISSION);
        }

        // clawback more than max amount
        // fails in the json parser before
        // transactor is called
        {
            Env env(*this, features);
            Account const alice{"alice"};
            Account const bob{"bob"};

            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const mpt = ripple::test::jtx::MPT(
                alice.name(), makeMptID(env.seq(alice), alice));

            Json::Value jv = claw(alice, mpt(1), bob);
            jv[jss::Amount][jss::value] = to_string(maxMPTokenAmount + 1);
            Json::Value jv1;
            jv1[jss::secret] = alice.name();
            jv1[jss::tx_json] = jv;
            auto const jrr = env.rpc("json", "submit", to_string(jv1));
            BEAST_EXPECT(jrr[jss::result][jss::error] == "invalidParams");
        }
    }

    void
    testClawback(FeatureBitset features)
    {
        testcase("MPT Clawback");
        using namespace test::jtx;

        {
            Env env(*this, features);
            Account const alice{"alice"};
            Account const bob{"bob"};

            MPTTester mptAlice(env, alice, {.holders = {bob}});

            // alice creates issuance
            mptAlice.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanClawback});

            // bob creates a MPToken
            mptAlice.authorize({.account = bob});

            // alice pays bob 100 tokens
            mptAlice.pay(alice, bob, 100);

            mptAlice.claw(alice, bob, 1);

            mptAlice.claw(alice, bob, 1000);

            // clawback fails because bob currently has a balance of zero
            mptAlice.claw(alice, bob, 1, tecINSUFFICIENT_FUNDS);
        }

        // Test that globally locked funds can be clawed
        {
            Env env(*this, features);
            Account const alice{"alice"};
            Account const bob{"bob"};

            MPTTester mptAlice(env, alice, {.holders = {bob}});

            // alice creates issuance
            mptAlice.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanLock | tfMPTCanClawback});

            // bob creates a MPToken
            mptAlice.authorize({.account = bob});

            // alice pays bob 100 tokens
            mptAlice.pay(alice, bob, 100);

            mptAlice.set({.account = alice, .flags = tfMPTLock});

            mptAlice.claw(alice, bob, 100);
        }

        // Test that individually locked funds can be clawed
        {
            Env env(*this, features);
            Account const alice{"alice"};
            Account const bob{"bob"};

            MPTTester mptAlice(env, alice, {.holders = {bob}});

            // alice creates issuance
            mptAlice.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanLock | tfMPTCanClawback});

            // bob creates a MPToken
            mptAlice.authorize({.account = bob});

            // alice pays bob 100 tokens
            mptAlice.pay(alice, bob, 100);

            mptAlice.set({.account = alice, .holder = bob, .flags = tfMPTLock});

            mptAlice.claw(alice, bob, 100);
        }

        // Test that unauthorized funds can be clawed back
        {
            Env env(*this, features);
            Account const alice{"alice"};
            Account const bob{"bob"};

            MPTTester mptAlice(env, alice, {.holders = {bob}});

            // alice creates issuance
            mptAlice.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanClawback | tfMPTRequireAuth});

            // bob creates a MPToken
            mptAlice.authorize({.account = bob});

            // alice authorizes bob
            mptAlice.authorize({.account = alice, .holder = bob});

            // alice pays bob 100 tokens
            mptAlice.pay(alice, bob, 100);

            // alice unauthorizes bob
            mptAlice.authorize(
                {.account = alice, .holder = bob, .flags = tfMPTUnauthorize});

            mptAlice.claw(alice, bob, 100);
        }
    }

    void
    testTokensEquality()
    {
        using namespace test::jtx;
        testcase("Tokens Equality");
        Currency const cur1{to_currency("CU1")};
        Currency const cur2{to_currency("CU2")};
        Account const gw1{"gw1"};
        Account const gw2{"gw2"};
        MPTID const mpt1 = makeMptID(1, gw1);
        MPTID const mpt1a = makeMptID(1, gw1);
        MPTID const mpt2 = makeMptID(1, gw2);
        MPTID const mpt3 = makeMptID(2, gw2);
        Asset const assetCur1Gw1{Issue{cur1, gw1}};
        Asset const assetCur1Gw1a{Issue{cur1, gw1}};
        Asset const assetCur2Gw1{Issue{cur2, gw1}};
        Asset const assetCur2Gw2{Issue{cur2, gw2}};
        Asset const assetMpt1Gw1{mpt1};
        Asset const assetMpt1Gw1a{mpt1a};
        Asset const assetMpt1Gw2{mpt2};
        Asset const assetMpt2Gw2{mpt3};

        // Assets holding Issue
        // Currencies are equal regardless of the issuer
        BEAST_EXPECT(equalTokens(assetCur1Gw1, assetCur1Gw1a));
        BEAST_EXPECT(equalTokens(assetCur2Gw1, assetCur2Gw2));
        // Currencies are different regardless of whether the issuers
        // are the same or not
        BEAST_EXPECT(!equalTokens(assetCur1Gw1, assetCur2Gw1));
        BEAST_EXPECT(!equalTokens(assetCur1Gw1, assetCur2Gw2));

        // Assets holding MPTIssue
        // MPTIDs are the same if the sequence and the issuer are the same
        BEAST_EXPECT(equalTokens(assetMpt1Gw1, assetMpt1Gw1a));
        // MPTIDs are different if sequence and the issuer don't match
        BEAST_EXPECT(!equalTokens(assetMpt1Gw1, assetMpt1Gw2));
        BEAST_EXPECT(!equalTokens(assetMpt1Gw2, assetMpt2Gw2));

        // Assets holding Issue and MPTIssue
        BEAST_EXPECT(!equalTokens(assetCur1Gw1, assetMpt1Gw1));
        BEAST_EXPECT(!equalTokens(assetMpt2Gw2, assetCur2Gw2));
    }

    void
    testHelperFunctions()
    {
        using namespace test::jtx;
        Account const gw{"gw"};
        Asset const asset1{makeMptID(1, gw)};
        Asset const asset2{makeMptID(2, gw)};
        Asset const asset3{makeMptID(3, gw)};
        STAmount const amt1{asset1, 100};
        STAmount const amt2{asset2, 100};
        STAmount const amt3{asset3, 10'000};

        {
            testcase("Test STAmount MPT arithmetics");
            using namespace std::string_literals;
            STAmount res = multiply(amt1, amt2, asset3);
            BEAST_EXPECT(res == amt3);

            res = mulRound(amt1, amt2, asset3, true);
            BEAST_EXPECT(res == amt3);

            res = mulRoundStrict(amt1, amt2, asset3, true);
            BEAST_EXPECT(res == amt3);

            // overflow, any value > 3037000499ull
            STAmount mptOverflow{asset2, UINT64_C(3037000500)};
            try
            {
                res = multiply(mptOverflow, mptOverflow, asset3);
                fail("should throw runtime exception 1");
            }
            catch (std::runtime_error const& e)
            {
                BEAST_EXPECTS(e.what() == "MPT value overflow"s, e.what());
            }
            // overflow, (v1 >> 32) * v2 > 2147483648ull
            mptOverflow = STAmount{asset2, UINT64_C(2147483648)};
            uint64_t const mantissa = (2ull << 32) + 2;
            try
            {
                res = multiply(STAmount{asset1, mantissa}, mptOverflow, asset3);
                fail("should throw runtime exception 2");
            }
            catch (std::runtime_error const& e)
            {
                BEAST_EXPECTS(e.what() == "MPT value overflow"s, e.what());
            }
        }

        {
            testcase("Test MPTAmount arithmetics");
            MPTAmount mptAmt1{100};
            MPTAmount const mptAmt2{100};
            BEAST_EXPECT((mptAmt1 += mptAmt2) == MPTAmount{200});
            BEAST_EXPECT(mptAmt1 == 200);
            BEAST_EXPECT((mptAmt1 -= mptAmt2) == mptAmt1);
            BEAST_EXPECT(mptAmt1 == mptAmt2);
            BEAST_EXPECT(mptAmt1 == 100);
            BEAST_EXPECT(MPTAmount::minPositiveAmount() == MPTAmount{1});
        }

        {
            testcase("Test MPTIssue from/to Json");
            MPTIssue const issue1{asset1.get<MPTIssue>()};
            Json::Value const jv = to_json(issue1);
            BEAST_EXPECT(
                jv[jss::mpt_issuance_id] == to_string(asset1.get<MPTIssue>()));
            BEAST_EXPECT(issue1 == mptIssueFromJson(jv));
        }

        {
            testcase("Test Asset from/to Json");
            Json::Value const jv = to_json(asset1);
            BEAST_EXPECT(
                jv[jss::mpt_issuance_id] == to_string(asset1.get<MPTIssue>()));
            BEAST_EXPECT(
                to_string(jv) ==
                "{\"mpt_issuance_id\":"
                "\"00000001A407AF5856CCF3C42619DAA925813FC955C72983\"}");
            BEAST_EXPECT(asset1 == assetFromJson(jv));
        }
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{supported_amendments()};

        // MPTokenIssuanceCreate
        testCreateValidation(all);
        testCreateEnabled(all);

        // MPTokenIssuanceDestroy
        testDestroyValidation(all);
        testDestroyEnabled(all);

        // MPTokenAuthorize
        testAuthorizeValidation(all);
        testAuthorizeEnabled(all);

        // MPTokenIssuanceSet
        testSetValidation(all);
        testSetEnabled(all);

        // MPT clawback
        testClawbackValidation(all);
        testClawback(all);

        // Test Direct Payment
        testPayment(all);
        testDepositPreauth();

        // Test MPT Amount is invalid in Tx, which don't support MPT
        testMPTInvalidInTx(all);

        // Test parsed MPTokenIssuanceID in API response metadata
        testTxJsonMetaFields(all);

        // Test tokens equality
        testTokensEquality();

        // Test helpers
        testHelperFunctions();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(MPToken, tx, ripple, 2);

}  // namespace test
}  // namespace ripple
