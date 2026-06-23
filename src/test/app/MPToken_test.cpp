#include <test/jtx/AMM.h>
#include <test/jtx/AMMTest.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>  // IWYU pragma: keep
#include <test/jtx/check.h>
#include <test/jtx/credentials.h>
#include <test/jtx/delivermin.h>
#include <test/jtx/deposit.h>
#include <test/jtx/domain.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/escrow.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/multisign.h>
#include <test/jtx/offer.h>
#include <test/jtx/paths.h>
#include <test/jtx/pay.h>
#include <test/jtx/permissioned_dex.h>
#include <test/jtx/permissioned_domains.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>
#include <test/jtx/vault.h>
#include <test/jtx/xchain_bridge.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ApplyViewImpl.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTAmount.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>

#include <array>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace xrpl::test {

class MPToken_test : public beast::unit_test::Suite
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
            // create MPTokenIssuance
            Env env{*this, features - featureMPTokensV1};
            MPTTester mptAlice(env, alice);

            mptAlice.create({.ownerCount = 0, .err = temDISABLED});
        }

        // test preflight of MPTokenIssuanceCreate
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice);

            mptAlice.create({.flags = 0x00000001, .err = temINVALID_FLAG});

            // tries to set a txFee while not enabling in the flag
            mptAlice.create(
                {.maxAmt = 100,
                 .assetScale = 0,
                 .transferFee = 1,
                 .metadata = "test",
                 .err = temMALFORMED});

            if (!features[featureSingleAssetVault])
            {
                // tries to set DomainID when SAV is disabled
                mptAlice.create(
                    {.maxAmt = 100,
                     .assetScale = 0,
                     .metadata = "test",
                     .flags = tfMPTRequireAuth,
                     .domainID = uint256(42),
                     .err = temDISABLED});
            }
            else if (!features[featurePermissionedDomains])
            {
                // tries to set DomainID when PD is disabled
                mptAlice.create(
                    {.maxAmt = 100,
                     .assetScale = 0,
                     .metadata = "test",
                     .flags = tfMPTRequireAuth,
                     .domainID = uint256(42),
                     .err = temDISABLED});
            }
            else
            {
                // tries to set DomainID when RequireAuth is not set
                mptAlice.create(
                    {.maxAmt = 100,
                     .assetScale = 0,
                     .metadata = "test",
                     .domainID = uint256(42),
                     .err = temMALFORMED});

                // tries to set zero DomainID
                mptAlice.create(
                    {.maxAmt = 100,
                     .assetScale = 0,
                     .metadata = "test",
                     .flags = tfMPTRequireAuth,
                     .domainID = uint256{},
                     .err = temMALFORMED});
            }

            // tries to set a txFee greater than max
            mptAlice.create(
                {.maxAmt = 100,
                 .assetScale = 0,
                 .transferFee = kMaxTransferFee + 1,
                 .metadata = "test",
                 .flags = tfMPTCanTransfer,
                 .err = temBAD_TRANSFER_FEE});

            // tries to set a txFee while not enabling transfer
            mptAlice.create(
                {.maxAmt = 100,
                 .assetScale = 0,
                 .transferFee = kMaxTransferFee,
                 .metadata = "test",
                 .err = temMALFORMED});

            // empty metadata returns error
            mptAlice.create(
                {.maxAmt = 100,
                 .assetScale = 0,
                 .transferFee = 0,
                 .metadata = "",
                 .err = temMALFORMED});

            // MaximumAmount of 0 returns error
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
                {.maxAmt = kMaxMpTokenAmount + 1,  // 9'223'372'036'854'775'808
                 .assetScale = 0,
                 .transferFee = 0,
                 .metadata = "test",
                 .err = temMALFORMED});
        }

        // sfReferenceHolding is populated internally only by VaultCreate.
        // A user-submitted MPTokenIssuanceCreate carrying the field must be
        // rejected at preflight under fixCleanup3_2_0.
        if (features[fixCleanup3_2_0])
        {
            Env env{*this, features};
            env.fund(XRP(1'000), alice);
            env.close();

            json::Value jv;
            jv[sfAccount] = alice.human();
            jv[sfTransactionType] = jss::MPTokenIssuanceCreate;
            jv[sfReferenceHolding] = to_string(uint256{1});
            env(jv, Ter(temMALFORMED));
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
                {.maxAmt = kMaxMpTokenAmount,  // 9'223'372'036'854'775'807
                 .assetScale = 1,
                 .transferFee = 10,
                 .metadata = "123",
                 .ownerCount = 1,
                 .flags = tfMPTCanLock | tfMPTRequireAuth | tfMPTCanEscrow | tfMPTCanTrade |
                     tfMPTCanTransfer | tfMPTCanClawback});

            // Get the hash for the most recent transaction.
            std::string const txHash{
                env.tx()->getJson(JsonOptions::Values::None)[jss::hash].asString()};

            json::Value const result = env.rpc("tx", txHash)[jss::result];
            BEAST_EXPECT(result[sfMaximumAmount.getJsonName()] == "9223372036854775807");
        }

        if (features[featureSingleAssetVault])
        {
            // Add permissioned domain
            Account const credIssuer1{"credIssuer1"};
            std::string const credType = "credential";

            pdomain::Credentials const credentials1{{.issuer = credIssuer1, .credType = credType}};

            {
                Env env{*this, features};
                env.fund(XRP(1000), credIssuer1);

                env(pdomain::setTx(credIssuer1, credentials1));
                auto const domainId1 = [&]() {
                    auto tx = env.tx()->getJson(JsonOptions::Values::None);
                    return pdomain::getNewDomain(env.meta());
                }();

                MPTTester mptAlice(env, alice);
                mptAlice.create({
                    .maxAmt = kMaxMpTokenAmount,  // 9'223'372'036'854'775'807
                    .assetScale = 1,
                    .transferFee = 10,
                    .metadata = "123",
                    .ownerCount = 1,
                    .flags = tfMPTCanLock | tfMPTRequireAuth | tfMPTCanEscrow | tfMPTCanTrade |
                        tfMPTCanTransfer | tfMPTCanClawback,
                    .domainID = domainId1,
                });

                // Get the hash for the most recent transaction.
                std::string const txHash{
                    env.tx()->getJson(JsonOptions::Values::None)[jss::hash].asString()};

                json::Value const result = env.rpc("tx", txHash)[jss::result];
                BEAST_EXPECT(result[sfMaximumAmount.getJsonName()] == "9223372036854775807");
            }
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

            mptAlice.destroy({.id = id, .flags = 0x00000001, .err = temINVALID_FLAG});
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
                {.account = bob, .id = makeMptID(env.seq(alice), alice), .err = temDISABLED});
        }

        // Validate fields in MPTokenAuthorize (preflight)
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1});

            // The only valid MPTokenAuthorize flag is tfMPTUnauthorize, which
            // has a value of 1
            mptAlice.authorize({.account = bob, .flags = 0x00000002, .err = temINVALID_FLAG});

            mptAlice.authorize({.account = bob, .holder = bob, .err = temMALFORMED});

            mptAlice.authorize({.holder = alice, .err = temMALFORMED});
        }

        // Try authorizing when MPTokenIssuance doesn't exist in
        // MPTokenAuthorize (preclaim)
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}});
            auto const id = makeMptID(env.seq(alice), alice);

            mptAlice.authorize({.holder = bob, .id = id, .err = tecOBJECT_NOT_FOUND});

            mptAlice.authorize({.account = bob, .id = id, .err = tecOBJECT_NOT_FOUND});
        }

        // Test bad scenarios without allowlisting in MPTokenAuthorize
        // (preclaim)
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1});

            // bob submits a tx with a holder field
            mptAlice.authorize({.account = bob, .holder = alice, .err = tecNO_PERMISSION});

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
                    {.account = bob, .flags = tfMPTUnauthorize, .err = tecHAS_OBLIGATIONS});

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

            // alice specifies a holder acct that doesn't exist
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
            mptAlice.authorize({.account = bob, .holderCount = 0, .flags = tfMPTUnauthorize});
        }

        // Test mptoken reserve requirement - first two mpts free (doApply)
        {
            Env env{*this, features};
            auto const acctReserve = env.current()->fees().reserve;
            auto const incReserve = env.current()->fees().increment;

            // 1 drop
            BEAST_EXPECT(incReserve > XRPAmount(1));
            MPTTester mptAlice1(
                env, alice, {.holders = {bob}, .xrpHolders = acctReserve + (incReserve - 1)});
            mptAlice1.create();

            MPTTester mptAlice2(env, alice, {.fund = false});
            mptAlice2.create();

            MPTTester mptAlice3(env, alice, {.fund = false});
            mptAlice3.create({.ownerCount = 3});

            // first mpt for free
            mptAlice1.authorize({.account = bob, .holderCount = 1});

            // second mpt free
            mptAlice2.authorize({.account = bob, .holderCount = 2});

            mptAlice3.authorize({.account = bob, .err = tecINSUFFICIENT_RESERVE});

            env(pay(env.master, bob, drops(incReserve + incReserve + incReserve)));
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

            mptAlice.authorize({.account = bob, .holderCount = 1, .err = tecDUPLICATE});

            // bob deletes his mptoken
            mptAlice.authorize({.account = bob, .holderCount = 0, .flags = tfMPTUnauthorize});
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
                {.account = alice, .holder = bob, .holderCount = 1, .flags = tfMPTUnauthorize});

            mptAlice.authorize({.account = bob, .holderCount = 0, .flags = tfMPTUnauthorize});
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
            mptAlice.authorize({.account = bob, .holderCount = 0, .flags = tfMPTUnauthorize});
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
                {.account = bob, .id = makeMptID(env.seq(alice), alice), .err = temDISABLED});

            env.enableFeature(featureMPTokensV1);

            mptAlice.create({.ownerCount = 1, .holderCount = 0});

            mptAlice.authorize({.account = bob, .holderCount = 1});

            // test invalid flag - only valid flags are tfMPTLock (1) and Unlock
            // (2)
            mptAlice.set({.account = alice, .flags = 0x00000008, .err = temINVALID_FLAG});

            if (!features[featureSingleAssetVault] && !features[featureDynamicMPT])
            {
                // test invalid flags - nothing is being changed
                mptAlice.set({.account = alice, .flags = 0x00000000, .err = tecNO_PERMISSION});

                mptAlice.set(
                    {.account = alice,
                     .holder = bob,
                     .flags = 0x00000000,
                     .err = tecNO_PERMISSION});

                // cannot set DomainID since SAV is not enabled
                mptAlice.set({.account = alice, .domainID = uint256(42), .err = temDISABLED});
            }
            else
            {
                // test invalid flags - nothing is being changed
                mptAlice.set({.account = alice, .flags = 0x00000000, .err = temMALFORMED});

                mptAlice.set(
                    {.account = alice, .holder = bob, .flags = 0x00000000, .err = temMALFORMED});

                if (!features[featurePermissionedDomains] || !features[featureSingleAssetVault])
                {
                    // cannot set DomainID since PD is not enabled
                    mptAlice.set({.account = alice, .domainID = uint256(42), .err = temDISABLED});
                }
                else if (features[featureSingleAssetVault])
                {
                    // cannot set DomainID since Holder is set
                    mptAlice.set(
                        {.account = alice,
                         .holder = bob,
                         .domainID = uint256(42),
                         .err = temMALFORMED});
                }
            }

            // set both lock and unlock flags at the same time will fail
            mptAlice.set(
                {.account = alice, .flags = tfMPTLock | tfMPTUnlock, .err = temINVALID_FLAG});

            // if the holder is the same as the acct that submitted the tx,
            // tx fails
            mptAlice.set(
                {.account = alice, .holder = alice, .flags = tfMPTLock, .err = temMALFORMED});
        }

        // Validate fields in MPTokenIssuanceSet (preclaim)
        // test when a mptokenissuance has disabled locking
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1});

            // alice tries to lock a mptissuance that has disabled locking
            mptAlice.set({.account = alice, .flags = tfMPTLock, .err = tecNO_PERMISSION});

            // alice tries to unlock mptissuance that has disabled locking
            mptAlice.set({.account = alice, .flags = tfMPTUnlock, .err = tecNO_PERMISSION});

            // issuer tries to lock a bob's mptoken that has disabled
            // locking
            mptAlice.set(
                {.account = alice, .holder = bob, .flags = tfMPTLock, .err = tecNO_PERMISSION});

            // issuer tries to unlock a bob's mptoken that has disabled
            // locking
            mptAlice.set(
                {.account = alice, .holder = bob, .flags = tfMPTUnlock, .err = tecNO_PERMISSION});
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
            mptAlice.set({.account = bob, .flags = tfMPTLock, .err = tecNO_PERMISSION});

            // trying to set a holder who doesn't have a mptoken
            mptAlice.set({.holder = bob, .flags = tfMPTLock, .err = tecOBJECT_NOT_FOUND});

            // trying to set a holder who doesn't exist
            mptAlice.set({.holder = cindy, .flags = tfMPTLock, .err = tecNO_DST});
        }

        if (features[featureSingleAssetVault] && features[featurePermissionedDomains])
        {
            // Add permissioned domain
            Account const credIssuer1{"credIssuer1"};
            std::string const credType = "credential";

            pdomain::Credentials const credentials1{{.issuer = credIssuer1, .credType = credType}};

            {
                Env env{*this, features};

                MPTTester mptAlice(env, alice);
                mptAlice.create({});

                // Trying to set DomainID on a public MPTokenIssuance
                mptAlice.set({.domainID = uint256(42), .err = tecNO_PERMISSION});

                mptAlice.set({.domainID = uint256{}, .err = tecNO_PERMISSION});
            }

            {
                Env env{*this, features};

                MPTTester mptAlice(env, alice);
                mptAlice.create({.flags = tfMPTRequireAuth});

                // Trying to set non-existing DomainID
                mptAlice.set({.domainID = uint256(42), .err = tecOBJECT_NOT_FOUND});

                // Trying to lock but locking is disabled
                mptAlice.set(
                    {.flags = tfMPTUnlock, .domainID = uint256(42), .err = tecNO_PERMISSION});

                mptAlice.set(
                    {.flags = tfMPTUnlock, .domainID = uint256{}, .err = tecNO_PERMISSION});
            }
        }
    }

    void
    testSetEnabled(FeatureBitset features)
    {
        testcase("Enabled set transaction");

        using namespace test::jtx;
        Account const alice("alice");  // issuer
        Account const bob("bob");      // holder

        {
            // Test locking and unlocking
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob}});

            // create a mptokenissuance with locking
            mptAlice.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanLock});

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
            if (!features[featureSingleAssetVault])
            {
                // Delete bob's mptoken even though it is locked
                mptAlice.authorize({.account = bob, .flags = tfMPTUnauthorize});

                mptAlice.set(
                    {.account = alice,
                     .holder = bob,
                     .flags = tfMPTUnlock,
                     .err = tecOBJECT_NOT_FOUND});

                return;
            }

            // Cannot delete locked MPToken
            mptAlice.authorize(
                {.account = bob, .flags = tfMPTUnauthorize, .err = tecNO_PERMISSION});

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

        if (features[featureSingleAssetVault])
        {
            // Add permissioned domain
            std::string const credType = "credential";

            // Test setting and resetting domain ID
            Env env{*this, features};

            auto const domainId1 = [&]() {
                Account const credIssuer1{"credIssuer1"};
                env.fund(XRP(1000), credIssuer1);

                pdomain::Credentials const credentials1{
                    {.issuer = credIssuer1, .credType = credType}};

                env(pdomain::setTx(credIssuer1, credentials1));
                return [&]() {
                    auto tx = env.tx()->getJson(JsonOptions::Values::None);
                    return pdomain::getNewDomain(env.meta());
                }();
            }();

            auto const domainId2 = [&]() {
                Account const credIssuer2{"credIssuer2"};
                env.fund(XRP(1000), credIssuer2);

                pdomain::Credentials const credentials2{
                    {.issuer = credIssuer2, .credType = credType}};

                env(pdomain::setTx(credIssuer2, credentials2));
                return [&]() {
                    auto tx = env.tx()->getJson(JsonOptions::Values::None);
                    return pdomain::getNewDomain(env.meta());
                }();
            }();

            MPTTester mptAlice(env, alice, {.holders = {bob}});

            // create a mptokenissuance with auth.
            mptAlice.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTRequireAuth});
            BEAST_EXPECT(mptAlice.checkDomainID(std::nullopt));

            // reset "domain not set" to "domain not set", i.e. no change
            mptAlice.set({.domainID = uint256{}});
            BEAST_EXPECT(mptAlice.checkDomainID(std::nullopt));

            // reset "domain not set" to domain1
            mptAlice.set({.domainID = domainId1});
            BEAST_EXPECT(mptAlice.checkDomainID(domainId1));

            // reset domain1 to domain2
            mptAlice.set({.domainID = domainId2});
            BEAST_EXPECT(mptAlice.checkDomainID(domainId2));

            // reset domain to "domain not set"
            mptAlice.set({.domainID = uint256{}});
            BEAST_EXPECT(mptAlice.checkDomainID(std::nullopt));
        }
    }

    void
    testPayment(FeatureBitset features)
    {
        testcase("Payment");

        using namespace test::jtx;
        Account const alice("alice");  // issuer
        Account const bob("bob");      // holder
        Account const carol("carol");  // holder
        auto const mpTokensV2 = features[featureMPTokensV2];

        // preflight validation

        // MPT is disabled
        {
            Env env{*this, features - featureMPTokensV1};  // NOLINT TODO
            Account const alice("alice");
            Account const bob("bob");

            env.fund(XRP(1'000), alice);
            env.fund(XRP(1'000), bob);
            STAmount const mpt{MPTIssue{makeMptID(1, alice)}, UINT64_C(100)};

            env(pay(alice, bob, mpt), Ter(temDISABLED));
        }

        // MPT is disabled, unsigned request
        {
            Env env{*this, features - featureMPTokensV1};
            Account const alice("alice");  // issuer
            Account const carol("carol");
            auto const usd = alice["USD"];

            env.fund(XRP(1'000), alice);
            env.fund(XRP(1'000), carol);
            STAmount const mpt{MPTIssue{makeMptID(1, alice)}, UINT64_C(100)};

            json::Value jv;
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
            auto const mpt = mptAlice["MPT"];

            mptAlice.authorize({.account = bob});

            auto err = !features[featureMPTokensV2] ? Ter(temINVALID_FLAG) : Ter(temRIPPLE_EMPTY);
            env(pay(alice, bob, mpt(10)), Txflags(tfNoRippleDirect), err);
            err = !features[featureMPTokensV2] ? Ter(temINVALID_FLAG) : Ter(tesSUCCESS);
            env(pay(alice, bob, mpt(10)), Txflags(tfLimitQuality), err);
        }

        // Invalid combination of send, sendMax, deliverMin, paths
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const carol("carol");

            MPTTester mptAlice(env, alice, {.holders = {carol}});

            mptAlice.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});

            mptAlice.authorize({.account = carol});

            // sendMax and DeliverMin are valid XRP amount,
            // but is invalid combination with MPT amount
            auto const mpt = mptAlice["MPT"];
            auto err = !mpTokensV2 ? Ter(temMALFORMED) : Ter(tecPATH_PARTIAL);
            env(pay(alice, carol, mpt(100)), Sendmax(XRP(100)), err);
            env(pay(alice, carol, mpt(100)), DeliverMin(XRP(100)), Ter(temBAD_AMOUNT));
            // sendMax MPT is invalid with IOU or XRP
            auto const usd = alice["USD"];
            err = !mpTokensV2 ? Ter(temMALFORMED) : Ter(tecPATH_DRY);
            env(pay(alice, carol, usd(100)), Sendmax(mpt(100)), err);
            err = !mpTokensV2 ? Ter(temMALFORMED) : Ter(tecPATH_PARTIAL);
            env(pay(alice, carol, XRP(100)), Sendmax(mpt(100)), err);
            env(pay(alice, carol, usd(100)), DeliverMin(mpt(100)), Ter(temBAD_AMOUNT));
            env(pay(alice, carol, XRP(100)), DeliverMin(mpt(100)), Ter(temBAD_AMOUNT));
            // sendmax and amount are different MPT issue
            test::jtx::MPT const mpT1("MPT", makeMptID(env.seq(alice) + 10, alice));
            err = !mpTokensV2 ? Ter(temMALFORMED) : Ter(tecOBJECT_NOT_FOUND);
            env(pay(alice, carol, mpT1(100)), Sendmax(mpt(100)), err);
            // "paths" is invalid in V1
            err = !mpTokensV2 ? Ter(temMALFORMED) : Ter(tesSUCCESS);
            env(pay(alice, carol, mpt(100)), Path(~usd), err);
        }

        // build_path is invalid if MPT
        {
            Env env{*this, features - featureMPTokensV2};
            Account const alice("alice");
            Account const carol("carol");

            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create({.ownerCount = 1, .holderCount = 0});
            auto const mpt = mptAlice["MPT"];

            mptAlice.authorize({.account = carol});

            json::Value payment;
            payment[jss::secret] = alice.name();
            payment[jss::tx_json] = pay(alice, carol, mpt(100));

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
            auto const mpt = mptAlice["MPT"];

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});

            mptAlice.pay(alice, bob, -1, temBAD_AMOUNT);

            mptAlice.pay(bob, carol, -1, temBAD_AMOUNT);

            mptAlice.pay(bob, alice, -1, temBAD_AMOUNT);

            env(pay(alice, bob, mpt(10)), Sendmax(mpt(-1)), Ter(temBAD_AMOUNT));
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
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTRequireAuth | tfMPTCanTransfer});

            mptAlice.authorize({.account = bob});

            mptAlice.pay(alice, bob, 100, tecNO_AUTH);
        }

        // If RequireAuth is enabled, Payment fails if the sender is not
        // authorized
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTRequireAuth | tfMPTCanTransfer});

            // bob creates an empty MPToken
            mptAlice.authorize({.account = bob});

            // alice authorizes bob to hold funds
            mptAlice.authorize({.account = alice, .holder = bob});

            // alice sends 100 MPT to bob
            mptAlice.pay(alice, bob, 100);

            // alice UNAUTHORIZES bob
            mptAlice.authorize({.account = alice, .holder = bob, .flags = tfMPTUnauthorize});

            // bob fails to send back to alice because he is no longer
            // authorize to move his funds!
            mptAlice.pay(bob, alice, 100, tecNO_AUTH);
        }

        if (features[featureSingleAssetVault] && features[featurePermissionedDomains])
        {
            // If RequireAuth is enabled and domain is a match, payment succeeds
            {
                Env env{*this, features};
                std::string const credType = "credential";
                Account const credIssuer1{"credIssuer1"};
                env.fund(XRP(1000), credIssuer1, bob);

                auto const domainId1 = [&]() {
                    pdomain::Credentials const credentials1{
                        {.issuer = credIssuer1, .credType = credType}};

                    env(pdomain::setTx(credIssuer1, credentials1));
                    return [&]() {
                        auto tx = env.tx()->getJson(JsonOptions::Values::None);
                        return pdomain::getNewDomain(env.meta());
                    }();
                }();
                // bob is authorized via domain
                env(credentials::create(bob, credIssuer1, credType));
                env(credentials::accept(bob, credIssuer1, credType));
                env.close();

                MPTTester mptAlice(env, alice, MPTInit{});
                env.close();

                mptAlice.create({
                    .ownerCount = 1,
                    .holderCount = 0,
                    .flags = tfMPTRequireAuth | tfMPTCanTransfer,
                    .domainID = domainId1,
                });

                mptAlice.authorize({.account = bob});
                env.close();

                // bob is authorized via domain
                mptAlice.pay(alice, bob, 100);
                mptAlice.set({.domainID = uint256{}});

                // bob is no longer authorized
                mptAlice.pay(alice, bob, 100, tecNO_AUTH);
            }

            {
                Env env{*this, features};
                std::string const credType = "credential";
                Account const credIssuer1{"credIssuer1"};
                env.fund(XRP(1000), credIssuer1, bob);

                auto const domainId1 = [&]() {
                    pdomain::Credentials const credentials1{
                        {.issuer = credIssuer1, .credType = credType}};

                    env(pdomain::setTx(credIssuer1, credentials1));
                    return [&]() {
                        auto tx = env.tx()->getJson(JsonOptions::Values::None);
                        return pdomain::getNewDomain(env.meta());
                    }();
                }();
                // bob is authorized via domain
                env(credentials::create(bob, credIssuer1, credType));
                env(credentials::accept(bob, credIssuer1, credType));
                env.close();

                MPTTester mptAlice(env, alice, MPTInit{});
                env.close();

                mptAlice.create({
                    .ownerCount = 1,
                    .holderCount = 0,
                    .flags = tfMPTRequireAuth | tfMPTCanTransfer,
                    .domainID = domainId1,
                });

                // bob creates an empty MPToken
                mptAlice.authorize({.account = bob});

                // alice authorizes bob to hold funds
                mptAlice.authorize({.account = alice, .holder = bob});

                // alice sends 100 MPT to bob
                mptAlice.pay(alice, bob, 100);

                // alice UNAUTHORIZES bob
                mptAlice.authorize({.account = alice, .holder = bob, .flags = tfMPTUnauthorize});

                // bob is still authorized, via domain
                mptAlice.pay(bob, alice, 10);

                mptAlice.set({.domainID = uint256{}});

                // bob fails to send back to alice because he is no longer
                // authorize to move his funds!
                mptAlice.pay(bob, alice, 10, tecNO_AUTH);
            }

            {
                Env env{*this, features};
                std::string const credType = "credential";
                // credIssuer1 is the owner of domainId1 and a credential issuer
                Account const credIssuer1{"credIssuer1"};
                // credIssuer2 is the owner of domainId2 and a credential issuer
                // Note, domainId2 also lists credentials issued by credIssuer1
                Account const credIssuer2{"credIssuer2"};
                env.fund(XRP(1000), credIssuer1, credIssuer2, bob, carol);

                auto const domainId1 = [&]() {
                    pdomain::Credentials const credentials{
                        {.issuer = credIssuer1, .credType = credType}};

                    env(pdomain::setTx(credIssuer1, credentials));
                    return [&]() {
                        auto tx = env.tx()->getJson(JsonOptions::Values::None);
                        return pdomain::getNewDomain(env.meta());
                    }();
                }();

                auto const domainId2 = [&]() {
                    pdomain::Credentials const credentials{
                        {.issuer = credIssuer1, .credType = credType},
                        {.issuer = credIssuer2, .credType = credType}};

                    env(pdomain::setTx(credIssuer2, credentials));
                    return [&]() {
                        auto tx = env.tx()->getJson(JsonOptions::Values::None);
                        return pdomain::getNewDomain(env.meta());
                    }();
                }();

                // bob is authorized via credIssuer1 which is recognized by both
                // domainId1 and domainId2
                env(credentials::create(bob, credIssuer1, credType));
                env(credentials::accept(bob, credIssuer1, credType));
                env.close();

                // carol is authorized via credIssuer2, only recognized by
                // domainId2
                env(credentials::create(carol, credIssuer2, credType));
                env(credentials::accept(carol, credIssuer2, credType));
                env.close();

                MPTTester mptAlice(env, alice, MPTInit{});
                env.close();

                mptAlice.create({
                    .ownerCount = 1,
                    .holderCount = 0,
                    .flags = tfMPTRequireAuth | tfMPTCanTransfer,
                    .domainID = domainId1,
                });

                // bob and carol create an empty MPToken
                mptAlice.authorize({.account = bob});
                mptAlice.authorize({.account = carol});
                env.close();

                // alice sends 50 MPT to bob but cannot send to carol
                mptAlice.pay(alice, bob, 50);
                mptAlice.pay(alice, carol, 50, tecNO_AUTH);
                env.close();

                // bob cannot send to carol because they are not on the same
                // domain (since credIssuer2 is not recognized by domainId1)
                mptAlice.pay(bob, carol, 10, tecNO_AUTH);
                env.close();

                // alice updates domainID to domainId2 which recognizes both
                // credIssuer1 and credIssuer2
                mptAlice.set({.domainID = domainId2});
                // alice can now send to carol
                mptAlice.pay(alice, carol, 10);
                env.close();

                // bob can now send to carol because both are in the same
                // domain
                mptAlice.pay(bob, carol, 10);
                env.close();

                // bob loses his authorization and can no longer send MPT
                env(credentials::deleteCred(credIssuer1, bob, credIssuer1, credType));
                env.close();

                mptAlice.pay(bob, carol, 10, tecNO_AUTH);
                mptAlice.pay(bob, alice, 10, tecNO_AUTH);
            }
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

            mptAlice.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});

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

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanLock | tfMPTCanTransfer});

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});

            mptAlice.pay(alice, bob, 100);
            mptAlice.pay(alice, carol, 100);

            auto const err =
                env.current()->rules().enabled(featureMPTokensV2) ? tecPATH_DRY : tecLOCKED;

            // Global lock
            mptAlice.set({.account = alice, .flags = tfMPTLock});
            // Can't send between holders
            mptAlice.pay(bob, carol, 1, err);
            mptAlice.pay(carol, bob, 2, err);
            // Issuer can send
            mptAlice.pay(alice, bob, 3);
            // Holder can send back to issuer
            mptAlice.pay(bob, alice, 4);

            // Global unlock
            mptAlice.set({.account = alice, .flags = tfMPTUnlock});
            // Individual lock
            mptAlice.set({.account = alice, .holder = bob, .flags = tfMPTLock});
            // Can't send between holders
            mptAlice.pay(bob, carol, 5, err);
            mptAlice.pay(carol, bob, 6, err);
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

            auto const mpt = mptAlice["MPT"];
            // SendMax doesn't cover the fee
            env(pay(bob, carol, mpt(100)), Sendmax(mpt(109)), Ter(tecPATH_PARTIAL));

            // Payment succeeds if sufficient SendMax is included.
            // 100 to carol, 10 to issuer
            env(pay(bob, carol, mpt(100)), Sendmax(mpt(110)));
            // 100 to carol, 10 to issuer
            env(pay(bob, carol, mpt(100)), Sendmax(mpt(115)));
            BEAST_EXPECT(mptAlice.checkMPTokenAmount(bob, 780));
            BEAST_EXPECT(mptAlice.checkMPTokenAmount(carol, 200));
            // Payment succeeds if partial payment even if
            // SendMax is less than deliver amount
            env(pay(bob, carol, mpt(100)), Sendmax(mpt(90)), Txflags(tfPartialPayment));
            // 82 to carol, 8 to issuer (90 / 1.1 ~ 81.81 (rounded to nearest) =
            // 82)
            BEAST_EXPECT(mptAlice.checkMPTokenAmount(bob, 690));
            // In V2 the payments are executed via the payment engine and
            // the rounding results in a higher quality trade
            BEAST_EXPECT(
                mptAlice.checkMPTokenAmount(carol, !features[featureMPTokensV2] ? 282 : 281));
        }

        // Insufficient SendMax with no transfer fee
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});

            // Holders create MPToken
            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});
            mptAlice.pay(alice, bob, 1'000);

            auto const mpt = mptAlice["MPT"];
            // SendMax is less than the amount
            env(pay(bob, carol, mpt(100)), Sendmax(mpt(99)), Ter(tecPATH_PARTIAL));
            env(pay(bob, alice, mpt(100)), Sendmax(mpt(99)), Ter(tecPATH_PARTIAL));

            // Payment succeeds if sufficient SendMax is included.
            env(pay(bob, carol, mpt(100)), Sendmax(mpt(100)));
            BEAST_EXPECT(mptAlice.checkMPTokenAmount(carol, 100));
            // Payment succeeds if partial payment
            env(pay(bob, carol, mpt(100)), Sendmax(mpt(99)), Txflags(tfPartialPayment));
            BEAST_EXPECT(mptAlice.checkMPTokenAmount(carol, 199));
        }

        // DeliverMin
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});

            // Holders create MPToken
            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});
            mptAlice.pay(alice, bob, 1'000);

            auto const mpt = mptAlice["MPT"];
            // Fails even with the partial payment because
            // deliver amount < deliverMin
            env(pay(bob, alice, mpt(100)),
                Sendmax(mpt(99)),
                DeliverMin(mpt(100)),
                Txflags(tfPartialPayment),
                Ter(tecPATH_PARTIAL));
            // Payment succeeds if deliver amount >= deliverMin
            env(pay(bob, alice, mpt(100)),
                Sendmax(mpt(99)),
                DeliverMin(mpt(99)),
                Txflags(tfPartialPayment));
        }

        // Issuer fails trying to send more than the maximum amount allowed
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.maxAmt = 100, .ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});

            mptAlice.authorize({.account = bob});

            // issuer sends holder the max amount allowed
            mptAlice.pay(alice, bob, 100);

            // issuer tries to exceed max amount
            auto const err = mpTokensV2 ? tecPATH_DRY : tecPATH_PARTIAL;
            mptAlice.pay(alice, bob, 1, err);
        }

        // Issuer fails trying to send more than the default maximum
        // amount allowed
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .holderCount = 0});

            mptAlice.authorize({.account = bob});

            // issuer sends holder the default max amount allowed
            mptAlice.pay(alice, bob, kMaxMpTokenAmount);

            // issuer tries to exceed max amount
            auto const err = mpTokensV2 ? tecPATH_DRY : tecPATH_PARTIAL;
            mptAlice.pay(alice, bob, 1, err);
        }

        // Pay more than max amount fails in the json parser before
        // transactor is called
        {
            Env env{*this, features};
            env.fund(XRP(1'000), alice, bob);
            STAmount const mpt{MPTIssue{makeMptID(1, alice)}, UINT64_C(100)};
            json::Value jv;
            jv[jss::secret] = alice.name();
            jv[jss::tx_json] = pay(alice, bob, mpt);
            jv[jss::tx_json][jss::Amount][jss::value] = std::to_string(kMaxMpTokenAmount + 1);
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
            auto const mpt = mptAlice["MPT"];

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});

            // issuer sends holder the max amount allowed
            mptAlice.pay(alice, bob, 10'000);

            // payment between the holders
            env(pay(bob, carol, mpt(10'000)), Sendmax(mpt(10'000)), Txflags(tfPartialPayment));
            // Verify the metadata
            auto const meta =
                env.meta()->getJson(JsonOptions::Values::None)[sfAffectedNodes.fieldName];
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
            BEAST_EXPECT(!meta[2u][sfModifiedNode.fieldName][sfFinalFields.fieldName].isMember(
                sfMPTAmount.fieldName));

            // payment between the holders fails without
            // partial payment
            auto const err = mpTokensV2 ? tecPATH_DRY : tecPATH_PARTIAL;
            env(pay(bob, carol, mpt(10'000)), Sendmax(mpt(10'000)), Ter(err));
        }

        // Pay maximum allowed amount
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create(
                {.maxAmt = kMaxMpTokenAmount,
                 .ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanTransfer});
            auto const mpt = mptAlice["MPT"];

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});

            // issuer sends holder the max amount allowed
            mptAlice.pay(alice, bob, kMaxMpTokenAmount);
            BEAST_EXPECT(mptAlice.checkMPTokenOutstandingAmount(kMaxMpTokenAmount));

            // payment between the holders
            mptAlice.pay(bob, carol, kMaxMpTokenAmount);
            BEAST_EXPECT(mptAlice.checkMPTokenOutstandingAmount(kMaxMpTokenAmount));
            // holder pays back to the issuer
            mptAlice.pay(carol, alice, kMaxMpTokenAmount);
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
            Env env{*this, features};  // NOLINT TODO

            env.fund(XRP(1'000), alice, bob);

            STAmount const mpt{MPTID{0}, 100};
            auto const err =
                !features[featureMPTokensV2] ? Ter(tecOBJECT_NOT_FOUND) : Ter(temBAD_CURRENCY);
            env(pay(alice, bob, mpt), err);
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

            mptAlice.create({.maxAmt = 100, .ownerCount = 1, .flags = tfMPTCanTransfer});

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

            mptAlice.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});

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
    testDepositPreauth(FeatureBitset features)
    {
        using namespace test::jtx;
        Account const alice("alice");  // issuer
        Account const bob("bob");      // holder
        Account const diana("diana");
        Account const dpIssuer("dpIssuer");  // holder

        char const credType[] = "abcde";

        if (features[featureCredentials])
        {
            testcase("DepositPreauth");

            Env env(*this, features);

            env.fund(XRP(50000), diana, dpIssuer);
            env.close();

            MPTTester mptAlice(env, alice, {.holders = {bob}});
            mptAlice.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTRequireAuth | tfMPTCanTransfer});

            env(pay(diana, bob, XRP(500)));
            env.close();

            // bob creates an empty MPToken
            mptAlice.authorize({.account = bob});
            // alice authorizes bob to hold funds
            mptAlice.authorize({.account = alice, .holder = bob});

            // Bob require pre-authorization
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
            auto const jv = credentials::ledgerEntry(env, alice, dpIssuer, credType);
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
            env(deposit::authCredentials(bob, {{.issuer = dpIssuer, .credType = credType}}));
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
            Env env(*this, testableAmendments() - featureCredentials);

            std::string const credIdx =
                "D007AE4B6E1274B4AF872588267B810C2F82716726351D1C7D38D3E5499FC6"
                "E2";

            env.fund(XRP(50000), diana, dpIssuer);
            env.close();

            MPTTester mptAlice(env, alice, {.holders = {bob}});
            mptAlice.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTRequireAuth | tfMPTCanTransfer});

            env(pay(diana, bob, XRP(500)));
            env.close();

            // bob creates an empty MPToken
            mptAlice.authorize({.account = bob});
            // alice authorizes bob to hold funds
            mptAlice.authorize({.account = alice, .holder = bob});

            // Bob require pre-authorization
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
        testcase("MPT Issue Invalid in Transaction");
        using namespace test::jtx;

        // Validate that every transaction with an amount/issue field,
        // which doesn't support MPT, fails.

        // keyed by transaction + amount/issue field
        std::set<std::string> txWithAmounts;
        for (auto const& format : TxFormats::getInstance())
        {
            for (auto const& e : format.getSOTemplate())
            {
                // Transaction has amount/issue fields.
                // Exclude pseudo-transaction SetFee. Don't consider
                // the Fee field since it's included in every transaction.
                if (e.supportMPT() == SoeMptNotSupported && e.sField().getName() != jss::Fee &&
                    format.getName() != std::string("SetFee"))
                {
                    txWithAmounts.insert(format.getName() + e.sField().fieldName);
                    break;
                }
            }
        }

        Account const alice("alice");
        auto const usd = alice["USD"];
        Account const carol("carol");
        MPTIssue const issue(makeMptID(1, alice));
        STAmount mpt{issue, UINT64_C(100)};
        auto const jvb = bridge(alice, usd, alice, usd);
        for (auto const& feature : {features, features - featureMPTokensV1})
        {
            Env env{*this, feature};
            env.fund(XRP(1'000), alice);
            env.fund(XRP(1'000), carol);
            auto test = [&](json::Value const& jv, std::string const& mptField) {
                txWithAmounts.erase(jv[jss::TransactionType].asString() + mptField);

                // tx is signed
                auto jtx = env.jt(jv);
                Serializer s;
                jtx.stx->add(s);
                auto jrr = env.rpc("submit", strHex(s.slice()));
                BEAST_EXPECT(jrr[jss::result][jss::error] == "invalidTransaction");

                // tx is unsigned
                json::Value jv1;
                jv1[jss::secret] = alice.name();
                jv1[jss::tx_json] = jv;
                jrr = env.rpc("json", "submit", to_string(jv1));
                BEAST_EXPECT(jrr[jss::result][jss::error] == "invalidParams");

                jrr = env.rpc("json", "sign", to_string(jv1));
                BEAST_EXPECT(jrr[jss::result][jss::error] == "invalidParams");
            };
            auto setMPTFields = [&](SField const& field, json::Value& jv, bool withAmount = true) {
                jv[jss::Asset] = toJson(xrpIssue());
                jv[jss::Asset2] = toJson(usd.issue());
                if (withAmount)
                    jv[field.fieldName] = usd(10).value().getJson(JsonOptions::Values::None);
                if (field == sfAsset)
                {
                    jv[jss::Asset] = toJson(mpt.get<MPTIssue>());
                }
                else if (field == sfAsset2)
                {
                    jv[jss::Asset2] = toJson(mpt.get<MPTIssue>());
                }
                else
                {
                    jv[field.fieldName] = mpt.getJson(JsonOptions::Values::None);
                }
            };
            // All transactions with sfAmount, which don't support MPT.
            // Transactions with amount fields, which can't be MPT.
            // Transactions with issue fields, which can't be MPT.

            // AMMDeposit
            auto ammDeposit = [&](SField const& field) {
                json::Value jv;
                jv[jss::TransactionType] = jss::AMMDeposit;
                jv[jss::Account] = alice.human();
                jv[jss::Flags] = tfSingleAsset;
                setMPTFields(field, jv);
                test(jv, field.fieldName);
            };
            for (SField const& field : {std::ref(sfEPrice), std::ref(sfLPTokenOut)})
                ammDeposit(field);
            // AMMWithdraw
            auto ammWithdraw = [&](SField const& field) {
                json::Value jv;
                jv[jss::TransactionType] = jss::AMMWithdraw;
                jv[jss::Account] = alice.human();
                jv[jss::Flags] = tfSingleAsset;
                setMPTFields(field, jv);
                test(jv, field.fieldName);
            };
            for (SField const& field : {std::ref(sfEPrice), std::ref(sfLPTokenIn)})
                ammWithdraw(field);
            // AMMBid
            auto ammBid = [&](SField const& field) {
                json::Value jv;
                jv[jss::TransactionType] = jss::AMMBid;
                jv[jss::Account] = alice.human();
                setMPTFields(field, jv);
                test(jv, field.fieldName);
            };
            ammBid(sfBidMin);
            ammBid(sfBidMax);
            // PaymentChannelCreate
            {
                json::Value jv;
                jv[jss::TransactionType] = jss::PaymentChannelCreate;
                jv[jss::Account] = alice.human();
                jv[jss::Destination] = carol.human();
                jv[jss::SettleDelay] = 1;
                jv[sfPublicKey.fieldName] = strHex(alice.pk().slice());
                jv[jss::Amount] = mpt.getJson(JsonOptions::Values::None);
                test(jv, jss::Amount.cStr());
            }
            // PaymentChannelFund
            {
                json::Value jv;
                jv[jss::TransactionType] = jss::PaymentChannelFund;
                jv[jss::Account] = alice.human();
                jv[sfChannel.fieldName] = to_string(uint256{1});
                jv[jss::Amount] = mpt.getJson(JsonOptions::Values::None);
                test(jv, jss::Amount.cStr());
            }
            // PaymentChannelClaim
            {
                json::Value jv;
                jv[jss::TransactionType] = jss::PaymentChannelClaim;
                jv[jss::Account] = alice.human();
                jv[sfChannel.fieldName] = to_string(uint256{1});
                jv[jss::Amount] = mpt.getJson(JsonOptions::Values::None);
                test(jv, jss::Amount.cStr());
            }
            // NFTokenCreateOffer
            {
                json::Value jv;
                jv[jss::TransactionType] = jss::NFTokenCreateOffer;
                jv[jss::Account] = alice.human();
                jv[sfNFTokenID.fieldName] = to_string(uint256{1});
                jv[jss::Amount] = mpt.getJson(JsonOptions::Values::None);
                test(jv, jss::Amount.cStr());
            }
            // NFTokenAcceptOffer
            {
                json::Value jv;
                jv[jss::TransactionType] = jss::NFTokenAcceptOffer;
                jv[jss::Account] = alice.human();
                jv[sfNFTokenBrokerFee.fieldName] = mpt.getJson(JsonOptions::Values::None);
                test(jv, sfNFTokenBrokerFee.fieldName);
            }
            // NFTokenMint
            {
                json::Value jv;
                jv[jss::TransactionType] = jss::NFTokenMint;
                jv[jss::Account] = alice.human();
                jv[sfNFTokenTaxon.fieldName] = 1;
                jv[jss::Amount] = mpt.getJson(JsonOptions::Values::None);
                test(jv, jss::Amount.cStr());
            }
            // TrustSet
            auto trustSet = [&](SField const& field) {
                json::Value jv;
                jv[jss::TransactionType] = jss::TrustSet;
                jv[jss::Account] = alice.human();
                jv[jss::Flags] = 0;
                jv[field.fieldName] = mpt.getJson(JsonOptions::Values::None);
                test(jv, field.fieldName);
            };
            trustSet(sfLimitAmount);
            trustSet(sfFee);
            // XChainCommit
            {
                json::Value const jv = xchainCommit(alice, jvb, 1, mpt);
                test(jv, jss::Amount.cStr());
            }
            // XChainClaim
            {
                json::Value const jv = xchainClaim(alice, jvb, 1, mpt, alice);
                test(jv, jss::Amount.cStr());
            }
            // XChainCreateClaimID
            {
                json::Value const jv = xchainCreateClaimId(alice, jvb, mpt, alice);
                test(jv, sfSignatureReward.fieldName);
            }
            // XChainAddClaimAttestation
            {
                json::Value const jv =
                    claimAttestation(alice, jvb, alice, mpt, alice, true, 1, alice, Signer(alice));
                test(jv, jss::Amount.cStr());
            }
            // XChainAddAccountCreateAttestation
            {
                json::Value jv = createAccountAttestation(
                    alice, jvb, alice, mpt, XRP(10), alice, false, 1, alice, Signer(alice));
                for (auto const& field : {sfAmount.fieldName, sfSignatureReward.fieldName})
                {
                    jv[field] = mpt.getJson(JsonOptions::Values::None);
                    test(jv, field);
                }
            }
            // XChainAccountCreateCommit
            {
                json::Value jv = sidechainXchainAccountCreate(alice, jvb, alice, mpt, XRP(10));
                for (auto const& field : {sfAmount.fieldName, sfSignatureReward.fieldName})
                {
                    jv[field] = mpt.getJson(JsonOptions::Values::None);
                    test(jv, field);
                }
            }
            // XChain[Create|Modify]Bridge
            auto bridgeTx = [&](json::StaticString const& tt,
                                STAmount const& rewardAmount,
                                STAmount const& minAccountAmount,
                                std::string const& field) {
                json::Value jv;
                jv[jss::TransactionType] = tt;
                jv[jss::Account] = alice.human();
                jv[sfXChainBridge.fieldName] = jvb;
                jv[sfSignatureReward.fieldName] = rewardAmount.getJson(JsonOptions::Values::None);
                jv[sfMinAccountCreateAmount.fieldName] =
                    minAccountAmount.getJson(JsonOptions::Values::None);
                test(jv, field);
            };
            auto reward = STAmount{sfSignatureReward, mpt};
            auto minAmount = STAmount{sfMinAccountCreateAmount, usd(10)};
            for (SField const& field :
                 {std::ref(sfSignatureReward), std::ref(sfMinAccountCreateAmount)})
            {
                bridgeTx(jss::XChainCreateBridge, reward, minAmount, field.fieldName);
                bridgeTx(jss::XChainModifyBridge, reward, minAmount, field.fieldName);
                reward = STAmount{sfSignatureReward, usd(10)};
                minAmount = STAmount{sfMinAccountCreateAmount, mpt};
            }
        }
        BEAST_EXPECT(txWithAmounts.empty());
    }

    void
    testNonCanonicalMPTAmountCleanup(FeatureBitset features)
    {
        using namespace test::jtx;
        using namespace std::literals;
        FeatureBitset const withoutFix = features - fixCleanup3_2_0;
        FeatureBitset const withFix = features | fixCleanup3_2_0;
        FeatureBitset const withoutFixAndV2 = withoutFix - featureMPTokensV2;
        FeatureBitset const withFixAndWithoutV2 = withFix - featureMPTokensV2;

        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const gw{"gw"};

        using MPTValue = MPTAmount::value_type;
        MPTValue const mptMin = std::numeric_limits<MPTValue>::min();
        MPTValue const mptMax = std::numeric_limits<MPTValue>::max();
        std::uint64_t const u64Max = std::numeric_limits<std::uint64_t>::max();
        std::uint64_t const firstInvalidMPTMantissa = static_cast<std::uint64_t>(mptMax) + 1;
        MPTValue const alice0 = 10'000;
        MPTValue const gw0 = -20'000;
        TER const success = tesSUCCESS;
        TER const invariantFailed = tecINVARIANT_FAILED;
        TER const pathPartial = tecPATH_PARTIAL;
        TER const badAmountTer = temBAD_AMOUNT;

        struct BadMPTAmount
        {
            std::string_view name;
            std::uint64_t mantissa;
            bool negative;
            MPTValue mptValue;
            TER issuerToHolderPreFixTer;
            TER holderSourcePreFixTer;
            MPTValue issuerToHolderAliceAfterPreFix;
            MPTValue issuerToHolderIssuerAfterPreFix;
            MPTValue issuerToHolderAliceAfterPostFix;
            MPTValue issuerToHolderIssuerAfterPostFix;
        };
        // clang-format off
        std::array<BadMPTAmount, 7> const badMPTAmounts = {{
            {   .name="INT64_MAX + 1",        .mantissa=firstInvalidMPTMantissa,     .negative=false, .mptValue=mptMin,           .issuerToHolderPreFixTer=invariantFailed, .holderSourcePreFixTer=pathPartial,  .issuerToHolderAliceAfterPreFix=alice0,         .issuerToHolderIssuerAfterPreFix=gw0,           .issuerToHolderAliceAfterPostFix=alice0 - 1,      .issuerToHolderIssuerAfterPostFix=gw0 + 1},
            {   .name="INT64_MAX + 10",       .mantissa=firstInvalidMPTMantissa + 9, .negative=false, .mptValue=mptMin + 9,       .issuerToHolderPreFixTer=invariantFailed, .holderSourcePreFixTer=pathPartial,  .issuerToHolderAliceAfterPreFix=alice0,         .issuerToHolderIssuerAfterPreFix=gw0,           .issuerToHolderAliceAfterPostFix=alice0 - 1,      .issuerToHolderIssuerAfterPostFix=gw0 + 1},
            {   .name="UINT64_MAX - 9998",    .mantissa=u64Max - 9'998,              .negative=false, .mptValue=MPTValue{-9'999}, .issuerToHolderPreFixTer=success,         .holderSourcePreFixTer=pathPartial,  .issuerToHolderAliceAfterPreFix=alice0 - 9'999, .issuerToHolderIssuerAfterPreFix=gw0 + 9'999,   .issuerToHolderAliceAfterPostFix=alice0 - 10'000, .issuerToHolderIssuerAfterPostFix=gw0 + 10'000},
            {   .name="UINT64_MAX - 9",       .mantissa=u64Max - 9,                  .negative=false, .mptValue=MPTValue{-10},    .issuerToHolderPreFixTer=success,         .holderSourcePreFixTer=pathPartial,  .issuerToHolderAliceAfterPreFix=alice0 - 10,    .issuerToHolderIssuerAfterPreFix=gw0 + 10,      .issuerToHolderAliceAfterPostFix=alice0 - 11,     .issuerToHolderIssuerAfterPostFix=gw0 + 11},
            {   .name="UINT64_MAX - 1",       .mantissa=u64Max - 1,                  .negative=false, .mptValue=MPTValue{-2},     .issuerToHolderPreFixTer=success,         .holderSourcePreFixTer=pathPartial,  .issuerToHolderAliceAfterPreFix=alice0 - 2,     .issuerToHolderIssuerAfterPreFix=gw0 + 2,       .issuerToHolderAliceAfterPostFix=alice0 - 3,      .issuerToHolderIssuerAfterPostFix=gw0 + 3},
            {   .name="UINT64_MAX",           .mantissa=u64Max,                      .negative=false, .mptValue=MPTValue{-1},     .issuerToHolderPreFixTer=success,         .holderSourcePreFixTer=pathPartial,  .issuerToHolderAliceAfterPreFix=alice0 - 1,     .issuerToHolderIssuerAfterPreFix=gw0 + 1,       .issuerToHolderAliceAfterPostFix=alice0 - 2,      .issuerToHolderIssuerAfterPostFix=gw0 + 2},
            {   .name="-2",                   .mantissa=std::uint64_t{2},            .negative=true,  .mptValue=MPTValue{-2},     .issuerToHolderPreFixTer=badAmountTer,    .holderSourcePreFixTer=badAmountTer, .issuerToHolderAliceAfterPreFix=alice0,         .issuerToHolderIssuerAfterPreFix=gw0,           .issuerToHolderAliceAfterPostFix=alice0 - 1,      .issuerToHolderIssuerAfterPostFix=gw0 + 1}
        }};
        // clang-format on
        auto const badMPTAmount = [&](MPTIssue const& issue, BadMPTAmount const& bad) {
            return STAmount{issue, bad.mantissa, 0, bad.negative, STAmount::Unchecked{}};
        };
        auto const makeIssue = [&](Env& env) {
            MPTTester const mpt{
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 10'000,
                 .flags = tfMPTCanTransfer | tfMPTCanTrade | tfMPTCanEscrow | tfMPTCanClawback}};
            return MPTIssue{mpt.issuanceID()};
        };
        auto const withNonCanonicalMPTAmount =
            [](JTx jt, SField const& field, STAmount const& amount, Account const& signer) {
                STTx tx{*jt.stx};
                tx.setFieldAmount(field, amount);
                tx.sign(signer.pk(), signer.sk());
                jt.stx = std::make_shared<STTx const>(tx);
                return jt;
            };
        auto const roundTrip = [](STTx const& tx) {
            Serializer s;
            tx.add(s);
            SerialIter sit{s.slice()};
            return STTx{sit};
        };
        auto const expectRoundTripBadMPT =
            [&](JTx const& jt, SField const& field, BadMPTAmount const& bad) {
                auto const roundTripped = roundTrip(*jt.stx);
                auto const persisted = roundTripped.getFieldAmount(field);
                BEAST_EXPECT(persisted.holds<MPTIssue>());
                BEAST_EXPECT(persisted.mantissa() == bad.mantissa);
                BEAST_EXPECT(persisted.exponent() == 0);
                BEAST_EXPECT(persisted.negative() == bad.negative);
                BEAST_EXPECT(persisted.mpt().value() == bad.mptValue);
                if (!bad.negative)
                    BEAST_EXPECT(persisted.mantissa() > kMaxMpTokenAmount);
            };

        for (auto const& bad : badMPTAmounts)
        {
            testcase("fixCleanup3_2_0 rejects non-canonical MPT Payment amounts");
            {
                Env env{*this, withoutFixAndV2};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badAmount = badMPTAmount(issue, bad);
                auto malformedHolderToHolder = withNonCanonicalMPTAmount(
                    env.jt(pay(alice, bob, STAmount{issue, std::uint64_t{1}})),
                    sfAmount,
                    badAmount,
                    alice);
                expectRoundTripBadMPT(malformedHolderToHolder, sfAmount, bad);
                malformedHolderToHolder.ter = bad.holderSourcePreFixTer;
                env.submit(malformedHolderToHolder);
                env.close();
                BEAST_EXPECT(
                    (env.balance(alice, issue).value() == STAmount{MPTAmount{10'000}, issue}));
                BEAST_EXPECT(
                    (env.balance(bob, issue).value() == STAmount{MPTAmount{10'000}, issue}));
                BEAST_EXPECT(
                    (env.balance(gw, issue).value() == STAmount{MPTAmount{-20'000}, issue}));

                env.enableFeature(fixCleanup3_2_0);
                env.close();
                env(env.jt(pay(bob, alice, STAmount{issue, std::uint64_t{1}})), Ter{tesSUCCESS});
                env.close();
                BEAST_EXPECT(
                    (env.balance(alice, issue).value() == STAmount{MPTAmount{10'001}, issue}));
                BEAST_EXPECT(
                    (env.balance(bob, issue).value() == STAmount{MPTAmount{9'999}, issue}));
                BEAST_EXPECT(
                    (env.balance(gw, issue).value() == STAmount{MPTAmount{-20'000}, issue}));
            }
            {
                Env env{*this, envconfig(), withoutFixAndV2, nullptr, beast::Severity::Disabled};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badAmount = badMPTAmount(issue, bad);
                auto malformedIssuerToHolder = withNonCanonicalMPTAmount(
                    env.jt(pay(gw, alice, STAmount{issue, std::uint64_t{1}})),
                    sfAmount,
                    badAmount,
                    gw);
                expectRoundTripBadMPT(malformedIssuerToHolder, sfAmount, bad);
                malformedIssuerToHolder.ter = bad.issuerToHolderPreFixTer;
                env.submit(malformedIssuerToHolder);
                env.close();
                BEAST_EXPECT(
                    (env.balance(alice, issue).value() ==
                     STAmount{MPTAmount{bad.issuerToHolderAliceAfterPreFix}, issue}));
                BEAST_EXPECT(
                    (env.balance(bob, issue).value() == STAmount{MPTAmount{10'000}, issue}));
                BEAST_EXPECT(
                    (env.balance(gw, issue).value() ==
                     STAmount{MPTAmount{bad.issuerToHolderIssuerAfterPreFix}, issue}));

                env.enableFeature(fixCleanup3_2_0);
                env.close();
                env(env.jt(pay(alice, gw, STAmount{issue, std::uint64_t{1}})), Ter{tesSUCCESS});
                env.close();
                BEAST_EXPECT(
                    (env.balance(alice, issue).value() ==
                     STAmount{MPTAmount{bad.issuerToHolderAliceAfterPostFix}, issue}));
                BEAST_EXPECT(
                    (env.balance(bob, issue).value() == STAmount{MPTAmount{10'000}, issue}));
                BEAST_EXPECT(
                    (env.balance(gw, issue).value() ==
                     STAmount{MPTAmount{bad.issuerToHolderIssuerAfterPostFix}, issue}));
            }
            {
                Env env{*this, withoutFixAndV2};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badAmount = badMPTAmount(issue, bad);
                auto malformedHolderToIssuer = withNonCanonicalMPTAmount(
                    env.jt(pay(alice, gw, STAmount{issue, std::uint64_t{1}})),
                    sfAmount,
                    badAmount,
                    alice);
                expectRoundTripBadMPT(malformedHolderToIssuer, sfAmount, bad);
                malformedHolderToIssuer.ter = bad.holderSourcePreFixTer;
                env.submit(malformedHolderToIssuer);
                env.close();
                BEAST_EXPECT(
                    (env.balance(alice, issue).value() == STAmount{MPTAmount{10'000}, issue}));
                BEAST_EXPECT(
                    (env.balance(bob, issue).value() == STAmount{MPTAmount{10'000}, issue}));
                BEAST_EXPECT(
                    (env.balance(gw, issue).value() == STAmount{MPTAmount{-20'000}, issue}));

                env.enableFeature(fixCleanup3_2_0);
                env.close();
                env(env.jt(pay(gw, alice, STAmount{issue, std::uint64_t{1}})), Ter{tesSUCCESS});
                env.close();
                BEAST_EXPECT(
                    (env.balance(alice, issue).value() == STAmount{MPTAmount{10'001}, issue}));
                BEAST_EXPECT(
                    (env.balance(bob, issue).value() == STAmount{MPTAmount{10'000}, issue}));
                BEAST_EXPECT(
                    (env.balance(gw, issue).value() == STAmount{MPTAmount{-20'001}, issue}));
            }
            {
                Env env{*this, withFixAndWithoutV2};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badAmount = badMPTAmount(issue, bad);
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(pay(alice, bob, STAmount{issue, std::uint64_t{1}})),
                    sfAmount,
                    badAmount,
                    alice);
                tx.ter = temBAD_AMOUNT;
                env.submit(tx);
            }
            {
                Env env{*this, withFixAndWithoutV2};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badAmount = badMPTAmount(issue, bad);
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(pay(gw, alice, STAmount{issue, std::uint64_t{1}})),
                    sfAmount,
                    badAmount,
                    gw);
                tx.ter = temBAD_AMOUNT;
                env.submit(tx);
            }
            {
                Env env{*this, withFixAndWithoutV2};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badAmount = badMPTAmount(issue, bad);
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(pay(alice, gw, STAmount{issue, std::uint64_t{1}})),
                    sfAmount,
                    badAmount,
                    alice);
                tx.ter = temBAD_AMOUNT;
                env.submit(tx);
            }

            testcase("fixCleanup3_2_0 rejects non-canonical MPT Check amounts");
            {
                Env env{*this, envconfig(), withoutFix, nullptr, beast::Severity::Disabled};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badSendMax = badMPTAmount(issue, bad);
                auto const checkSeq = env.seq(alice);
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(check::create(alice, bob, STAmount{issue, std::uint64_t{10}})),
                    sfSendMax,
                    badSendMax,
                    alice);
                tx.ter = bad.negative ? TER{temBAD_AMOUNT} : TER{tesSUCCESS};
                env.submit(tx);
                env.close();

                auto const checkKeylet = keylet::check(alice.id(), checkSeq);
                auto const sleCheck = env.le(checkKeylet);
                BEAST_EXPECT((sleCheck != nullptr) == !bad.negative);
                if (sleCheck && !bad.negative)
                {
                    auto const persisted = sleCheck->getFieldAmount(sfSendMax);
                    BEAST_EXPECT(persisted.holds<MPTIssue>());
                    BEAST_EXPECT(persisted.mantissa() == bad.mantissa);
                    BEAST_EXPECT(persisted.negative() == bad.negative);
                }
            }
            {
                Env env{*this, envconfig(), withoutFix, nullptr, beast::Severity::Disabled};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badSendMax = badMPTAmount(issue, bad);
                auto const checkSeq = env.seq(alice);
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(check::create(alice, bob, STAmount{issue, std::uint64_t{10}})),
                    sfSendMax,
                    badSendMax,
                    alice);
                tx.ter = bad.negative ? TER{temBAD_AMOUNT} : TER{tesSUCCESS};
                env.submit(tx);
                env.close();

                auto const checkKeylet = keylet::check(alice.id(), checkSeq);
                BEAST_EXPECT((env.le(checkKeylet) != nullptr) == !bad.negative);
                if (!bad.negative)
                {
                    // CheckCancel has no amount fields, but it must be able to
                    // remove a malformed legacy Check while the fix is disabled.
                    env(env.jt(check::cancel(alice, checkKeylet.key)), Ter{tesSUCCESS});
                    env.close();
                    BEAST_EXPECT(env.le(checkKeylet) == nullptr);
                }
            }
            {
                Env env{*this, envconfig(), withoutFix, nullptr, beast::Severity::Disabled};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badSendMax = badMPTAmount(issue, bad);
                auto const checkSeq = env.seq(alice);
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(check::create(alice, bob, STAmount{issue, std::uint64_t{10}})),
                    sfSendMax,
                    badSendMax,
                    alice);
                tx.ter = bad.negative ? TER{temBAD_AMOUNT} : TER{tesSUCCESS};
                env.submit(tx);
                env.close();

                auto const checkKeylet = keylet::check(alice.id(), checkSeq);
                BEAST_EXPECT((env.le(checkKeylet) != nullptr) == !bad.negative);
                if (!bad.negative)
                {
                    env.enableFeature(fixCleanup3_2_0);
                    env.close();

                    // Once the fix is enabled, CheckCancel should still remove
                    // a legacy Check because it does not consume the bad amount.
                    env(env.jt(check::cancel(alice, checkKeylet.key)), Ter{tesSUCCESS});
                    env.close();
                    BEAST_EXPECT(env.le(checkKeylet) == nullptr);
                }
            }
            {
                Env env{*this, envconfig(), withoutFix, nullptr, beast::Severity::Disabled};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badSendMax = badMPTAmount(issue, bad);
                auto const checkSeq = env.seq(alice);
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(check::create(alice, bob, STAmount{issue, std::uint64_t{10}})),
                    sfSendMax,
                    badSendMax,
                    alice);
                tx.ter = bad.negative ? TER{temBAD_AMOUNT} : TER{tesSUCCESS};
                env.submit(tx);
                env.close();

                auto const checkKeylet = keylet::check(alice.id(), checkSeq);
                BEAST_EXPECT((env.le(checkKeylet) != nullptr) == !bad.negative);
                if (!bad.negative)
                {
                    env.enableFeature(fixCleanup3_2_0);
                    env.close();

                    auto const cashAmount = STAmount{sfAmount, issue, std::uint64_t{1}, 0, false};
                    env(env.jt(check::cash(bob, checkKeylet.key, cashAmount)), Ter{tefBAD_LEDGER});
                    env.close();
                    BEAST_EXPECT(env.le(checkKeylet) != nullptr);
                }
            }
            {
                Env env{*this, withoutFix};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const sendMax = STAmount{sfSendMax, issue, std::uint64_t{10}, 0, false};
                auto const checkSeq = env.seq(alice);
                env(env.jt(check::create(alice, bob, sendMax)), Ter{tesSUCCESS});
                env.close();

                auto const badCashAmount = badMPTAmount(issue, bad);
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(
                        check::cash(
                            bob,
                            keylet::check(alice.id(), checkSeq).key,
                            STAmount{issue, std::uint64_t{1}})),
                    sfAmount,
                    badCashAmount,
                    bob);
                expectRoundTripBadMPT(tx, sfAmount, bad);
                tx.ter = bad.holderSourcePreFixTer;
                env.submit(tx);
                env.close();
                BEAST_EXPECT(env.le(keylet::check(alice.id(), checkSeq)) != nullptr);
                BEAST_EXPECT(
                    (env.balance(alice, issue).value() == STAmount{MPTAmount{10'000}, issue}));
                BEAST_EXPECT(
                    (env.balance(bob, issue).value() == STAmount{MPTAmount{10'000}, issue}));
                BEAST_EXPECT(
                    (env.balance(gw, issue).value() == STAmount{MPTAmount{-20'000}, issue}));
            }
            {
                Env env{*this, withFix};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const sendMax = STAmount{sfSendMax, issue, std::uint64_t{10}, 0, false};
                auto const checkSeq = env.seq(alice);
                env(env.jt(check::create(alice, bob, sendMax)), Ter{tesSUCCESS});
                env.close();

                auto const badCashAmount = badMPTAmount(issue, bad);
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(
                        check::cash(
                            bob,
                            keylet::check(alice.id(), checkSeq).key,
                            STAmount{issue, std::uint64_t{1}})),
                    sfAmount,
                    badCashAmount,
                    bob);
                tx.ter = temBAD_AMOUNT;
                env.submit(tx);
            }

            testcase("fixCleanup3_2_0 rejects non-canonical MPT Escrow amounts");
            {
                Env env{*this, withoutFix};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const escrowSeq = env.seq(alice);
                auto const badAmount = badMPTAmount(issue, bad);
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(
                        escrow::create(alice, bob, STAmount{issue, std::uint64_t{1}}),
                        escrow::kFinishTime(env.now() + 1s)),
                    sfAmount,
                    badAmount,
                    alice);
                tx.ter = bad.negative ? TER{temBAD_AMOUNT} : TER{tecINSUFFICIENT_FUNDS};
                env.submit(tx);
                env.close();
                BEAST_EXPECT(env.le(keylet::escrow(alice.id(), escrowSeq)) == nullptr);
            }
            {
                Env env{*this, withFix};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badAmount = badMPTAmount(issue, bad);
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(
                        escrow::create(alice, bob, STAmount{issue, std::uint64_t{1}}),
                        escrow::kFinishTime(env.now() + 1s)),
                    sfAmount,
                    badAmount,
                    alice);
                tx.ter = temBAD_AMOUNT;
                env.submit(tx);
            }

            testcase("fixCleanup3_2_0 rejects non-canonical MPT Clawback amounts");
            {
                Env env{*this, withoutFix};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badAmount = badMPTAmount(issue, bad);
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(claw(gw, STAmount{issue, std::uint64_t{1}}, bob)),
                    sfAmount,
                    badAmount,
                    gw);
                expectRoundTripBadMPT(tx, sfAmount, bad);
                tx.ter = bad.negative ? TER{temBAD_AMOUNT} : TER{tesSUCCESS};
                env.submit(tx);
                env.close();

                MPTValue const bobAfter = bad.negative ? MPTValue{10'000} : MPTValue{0};
                MPTValue const gwAfter = bad.negative ? MPTValue{-20'000} : MPTValue{-10'000};
                BEAST_EXPECT(
                    (env.balance(alice, issue).value() == STAmount{MPTAmount{10'000}, issue}));
                BEAST_EXPECT(
                    (env.balance(bob, issue).value() == STAmount{MPTAmount{bobAfter}, issue}));
                BEAST_EXPECT(
                    (env.balance(gw, issue).value() == STAmount{MPTAmount{gwAfter}, issue}));
            }
            {
                Env env{*this, withFix};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badAmount = badMPTAmount(issue, bad);
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(claw(gw, STAmount{issue, std::uint64_t{1}}, bob)),
                    sfAmount,
                    badAmount,
                    gw);
                tx.ter = temBAD_AMOUNT;
                env.submit(tx);
                env.close();

                BEAST_EXPECT(
                    (env.balance(alice, issue).value() == STAmount{MPTAmount{10'000}, issue}));
                BEAST_EXPECT(
                    (env.balance(bob, issue).value() == STAmount{MPTAmount{10'000}, issue}));
                BEAST_EXPECT(
                    (env.balance(gw, issue).value() == STAmount{MPTAmount{-20'000}, issue}));
            }

            testcase("featureMPTokensV2 disabled rejects MPT OfferCreate amounts");
            {
                Env env{*this, withoutFixAndV2};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badTakerPays = badMPTAmount(issue, bad);
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(offer(alice, STAmount{issue, std::uint64_t{1}}, XRP(10))),
                    sfTakerPays,
                    badTakerPays,
                    alice);
                expectRoundTripBadMPT(tx, sfTakerPays, bad);
                tx.ter = temDISABLED;
                env.submit(tx);
            }
            {
                Env env{*this, withFixAndWithoutV2};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badTakerPays = badMPTAmount(issue, bad);
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(offer(alice, STAmount{issue, std::uint64_t{1}}, XRP(10))),
                    sfTakerPays,
                    badTakerPays,
                    alice);
                tx.ter = temDISABLED;
                env.submit(tx);
            }
            {
                // sfTakerPays is MPT: both amendments active. Negative offers
                // fail in OfferCreate::preflight() before the universal check;
                // positive non-canonical amounts fail in the universal check.
                Env env{*this, withFix};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badTakerPays = badMPTAmount(issue, bad);
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(offer(alice, STAmount{issue, std::uint64_t{1}}, XRP(10))),
                    sfTakerPays,
                    badTakerPays,
                    alice);
                tx.ter = TER{temBAD_AMOUNT};
                env.submit(tx);
            }
            {
                // sfTakerGets is MPT: both amendments active. Negative offers
                // fail in OfferCreate::preflight() before the universal check;
                // positive non-canonical amounts fail in the universal check.
                Env env{*this, withFix};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badTakerGets = badMPTAmount(issue, bad);
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(offer(alice, XRP(10), STAmount{issue, std::uint64_t{1}})),
                    sfTakerGets,
                    badTakerGets,
                    alice);
                tx.ter = TER{temBAD_AMOUNT};
                env.submit(tx);
            }

            testcase("featureMPTokensV2 disabled rejects MPT AMMCreate amounts");
            {
                Env env{*this, withoutFixAndV2};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badAmount = badMPTAmount(issue, bad);
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(
                        AMM::createJv(alice.id(), STAmount{issue, std::uint64_t{1}}, XRP(1), 0),
                        Fee(static_cast<std::uint64_t>(env.current()->fees().increment.drops()))),
                    sfAmount,
                    badAmount,
                    alice);
                expectRoundTripBadMPT(tx, sfAmount, bad);
                tx.ter = temDISABLED;
                env.submit(tx);
            }
            {
                Env env{*this, withFixAndWithoutV2};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badAmount = badMPTAmount(issue, bad);
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(
                        AMM::createJv(alice.id(), STAmount{issue, std::uint64_t{1}}, XRP(1), 0),
                        Fee(static_cast<std::uint64_t>(env.current()->fees().increment.drops()))),
                    sfAmount,
                    badAmount,
                    alice);
                tx.ter = temDISABLED;
                env.submit(tx);
            }
            {
                // sfAmount is MPT: both amendments active, expect temBAD_AMOUNT
                Env env{*this, withFix};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badAmount = badMPTAmount(issue, bad);
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(
                        AMM::createJv(alice.id(), STAmount{issue, std::uint64_t{1}}, XRP(1), 0),
                        Fee(static_cast<std::uint64_t>(env.current()->fees().increment.drops()))),
                    sfAmount,
                    badAmount,
                    alice);
                tx.ter = temBAD_AMOUNT;
                env.submit(tx);
            }

            testcase("featureMPTokensV2 disabled rejects MPT AMMDeposit amounts");
            {
                Env env{*this, withoutFixAndV2};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badAmount = badMPTAmount(issue, bad);
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(
                        AMM::depositJv(
                            {.account = alice,
                             .asset1In = STAmount{issue, std::uint64_t{1}},
                             .assets = std::make_pair(Asset{issue}, Asset{xrpIssue()})}),
                        Fee(static_cast<std::uint64_t>(env.current()->fees().increment.drops()))),
                    sfAmount,
                    badAmount,
                    alice);
                expectRoundTripBadMPT(tx, sfAmount, bad);
                tx.ter = temDISABLED;
                env.submit(tx);
            }
            {
                Env env{*this, withFixAndWithoutV2};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badAmount = badMPTAmount(issue, bad);
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(
                        AMM::depositJv(
                            {.account = alice,
                             .asset1In = STAmount{issue, std::uint64_t{1}},
                             .assets = std::make_pair(Asset{issue}, Asset{xrpIssue()})}),
                        Fee(static_cast<std::uint64_t>(env.current()->fees().increment.drops()))),
                    sfAmount,
                    badAmount,
                    alice);
                tx.ter = temDISABLED;
                env.submit(tx);
            }
            {
                // sfAmount is MPT: both amendments active, expect temBAD_AMOUNT
                Env env{*this, withFix};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badAmount = badMPTAmount(issue, bad);
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(
                        AMM::depositJv(
                            {.account = alice,
                             .asset1In = STAmount{issue, std::uint64_t{1}},
                             .assets = std::make_pair(Asset{issue}, Asset{xrpIssue()})}),
                        Fee(static_cast<std::uint64_t>(env.current()->fees().increment.drops()))),
                    sfAmount,
                    badAmount,
                    alice);
                tx.ter = temBAD_AMOUNT;
                env.submit(tx);
            }

            testcase("featureMPTokensV2 disabled rejects MPT AMMWithdraw amounts");
            {
                Env env{*this, withoutFixAndV2};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badAmount = badMPTAmount(issue, bad);
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(
                        AMM::withdrawJv(
                            {.account = alice,
                             .asset1Out = STAmount{issue, std::uint64_t{1}},
                             .assets = std::make_pair(Asset{issue}, Asset{xrpIssue()})}),
                        Fee(static_cast<std::uint64_t>(env.current()->fees().increment.drops()))),
                    sfAmount,
                    badAmount,
                    alice);
                expectRoundTripBadMPT(tx, sfAmount, bad);
                tx.ter = temDISABLED;
                env.submit(tx);
            }
            {
                Env env{*this, withFixAndWithoutV2};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badAmount = badMPTAmount(issue, bad);
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(
                        AMM::withdrawJv(
                            {.account = alice,
                             .asset1Out = STAmount{issue, std::uint64_t{1}},
                             .assets = std::make_pair(Asset{issue}, Asset{xrpIssue()})}),
                        Fee(static_cast<std::uint64_t>(env.current()->fees().increment.drops()))),
                    sfAmount,
                    badAmount,
                    alice);
                tx.ter = temDISABLED;
                env.submit(tx);
            }
            {
                // sfAmount is MPT: both amendments active, expect temBAD_AMOUNT
                Env env{*this, withFix};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badAmount = badMPTAmount(issue, bad);
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(
                        AMM::withdrawJv(
                            {.account = alice,
                             .asset1Out = STAmount{issue, std::uint64_t{1}},
                             .assets = std::make_pair(Asset{issue}, Asset{xrpIssue()})}),
                        Fee(static_cast<std::uint64_t>(env.current()->fees().increment.drops()))),
                    sfAmount,
                    badAmount,
                    alice);
                tx.ter = temBAD_AMOUNT;
                env.submit(tx);
            }

            testcase("featureMPTokensV2 disabled rejects MPT AMMClawback amounts");
            {
                Env env{*this, withoutFixAndV2};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badAmount = badMPTAmount(issue, bad);
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(
                        amm::ammClawback(
                            gw,
                            alice,
                            Asset{issue},
                            Asset{xrpIssue()},
                            std::make_optional(STAmount{issue, std::uint64_t{1}})),
                        Fee(static_cast<std::uint64_t>(env.current()->fees().increment.drops()))),
                    sfAmount,
                    badAmount,
                    gw);
                expectRoundTripBadMPT(tx, sfAmount, bad);
                tx.ter = temDISABLED;
                env.submit(tx);
            }
            {
                Env env{*this, withFixAndWithoutV2};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badAmount = badMPTAmount(issue, bad);
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(
                        amm::ammClawback(
                            gw,
                            alice,
                            Asset{issue},
                            Asset{xrpIssue()},
                            std::make_optional(STAmount{issue, std::uint64_t{1}})),
                        Fee(static_cast<std::uint64_t>(env.current()->fees().increment.drops()))),
                    sfAmount,
                    badAmount,
                    gw);
                tx.ter = temDISABLED;
                env.submit(tx);
            }
            {
                // sfAmount is MPT: both amendments active, expect temBAD_AMOUNT
                Env env{*this, withFix};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badAmount = badMPTAmount(issue, bad);
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(
                        amm::ammClawback(
                            gw,
                            alice,
                            Asset{issue},
                            Asset{xrpIssue()},
                            std::make_optional(STAmount{issue, std::uint64_t{1}})),
                        Fee(static_cast<std::uint64_t>(env.current()->fees().increment.drops()))),
                    sfAmount,
                    badAmount,
                    gw);
                tx.ter = temBAD_AMOUNT;
                env.submit(tx);
            }

            testcase("fixCleanup3_2_0 rejects non-canonical MPT VaultClawback amounts");
            {
                Env env{*this, withFix};
                env.fund(XRP(100'000), alice, bob, gw);
                env.close();
                auto const issue = makeIssue(env);

                auto const badAmount = badMPTAmount(issue, bad);
                uint256 const fakeVaultId = keylet::vault(gw.id(), 1).key;
                auto tx = withNonCanonicalMPTAmount(
                    env.jt(
                        Vault::clawback(
                            {.issuer = gw,
                             .id = fakeVaultId,
                             .holder = alice,
                             .amount = STAmount{issue, std::uint64_t{1}}}),
                        Fee(static_cast<std::uint64_t>(env.current()->fees().increment.drops()))),
                    sfAmount,
                    badAmount,
                    gw);
                tx.ter = temBAD_AMOUNT;
                env.submit(tx);
            }
        }
    }

    void
    testTxJsonMetaFields(FeatureBitset features)
    {
        // checks synthetically injected mptissuanceid from  `tx` response
        testcase("Test synthetic fields from tx response");

        using namespace test::jtx;

        Account const alice{"alice"};

        auto cfg = envconfig();
        cfg->fees.referenceFee = 10;
        Env env{*this, std::move(cfg), features};
        MPTTester mptAlice(env, alice);

        mptAlice.create();

        std::string const txHash{
            env.tx()->getJson(JsonOptions::Values::None)[jss::hash].asString()};
        BEAST_EXPECTS(
            txHash ==
                "E11F0E0CA14219922B7881F060B9CEE67CFBC87E4049A441ED2AE348FF8FAC"
                "0E",
            txHash);
        json::Value const meta = env.rpc("tx", txHash)[jss::result][jss::meta];
        auto const id = meta[jss::mpt_issuance_id].asString();
        // Expect mpt_issuance_id field
        BEAST_EXPECT(meta.isMember(jss::mpt_issuance_id));
        BEAST_EXPECT(id == to_string(mptAlice.issuanceID()));
        BEAST_EXPECTS(id == "00000004AE123A8556F3CF91154711376AFB0F894F832B3D", id);
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

            auto const usd = alice["USD"];
            auto const mpt = xrpl::test::jtx::MPT(alice.name(), makeMptID(env.seq(alice), alice));

            env(claw(alice, bob["USD"](5), bob), Ter(temMALFORMED));
            env.close();

            env(claw(alice, mpt(5)), Ter(temDISABLED));
            env.close();

            env(claw(alice, mpt(5), bob), Ter(temDISABLED));
            env.close();
        }

        // Test preflight
        {
            Env env(*this, features);
            Account const alice{"alice"};
            Account const bob{"bob"};

            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const usd = alice["USD"];
            auto const mpt = xrpl::test::jtx::MPT(alice.name(), makeMptID(env.seq(alice), alice));

            // clawing back IOU from a MPT holder fails
            env(claw(alice, bob["USD"](5), bob), Ter(temMALFORMED));
            env.close();

            // clawing back MPT without specifying a holder fails
            env(claw(alice, mpt(5)), Ter(temMALFORMED));
            env.close();

            // clawing back zero amount fails
            env(claw(alice, mpt(0), bob), Ter(temBAD_AMOUNT));
            env.close();

            // alice can't claw back from herself
            env(claw(alice, mpt(5), alice), Ter(temMALFORMED));
            env.close();

            // can't clawback negative amount
            env(claw(alice, mpt(-1), bob), Ter(temBAD_AMOUNT));
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
            env.require(Flags(alice, asfAllowTrustLineClawback));

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

            auto const fakeMpt =
                xrpl::test::jtx::MPT(alice.name(), makeMptID(env.seq(alice), alice));

            // issuer tries to clawback MPT where issuance doesn't exist
            env(claw(alice, fakeMpt(5), bob), Ter(tecOBJECT_NOT_FOUND));
            env.close();

            // alice creates issuance
            mptAlice.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanClawback});

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

            auto const mpt = xrpl::test::jtx::MPT(alice.name(), makeMptID(env.seq(alice), alice));

            json::Value jv = claw(alice, mpt(1), bob);
            jv[jss::Amount][jss::value] = std::to_string(kMaxMpTokenAmount + 1);
            json::Value jv1;
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
            mptAlice.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanClawback});

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
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanLock | tfMPTCanClawback});

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
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanLock | tfMPTCanClawback});

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
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanClawback | tfMPTRequireAuth});

            // bob creates a MPToken
            mptAlice.authorize({.account = bob});

            // alice authorizes bob
            mptAlice.authorize({.account = alice, .holder = bob});

            // alice pays bob 100 tokens
            mptAlice.pay(alice, bob, 100);

            // alice unauthorizes bob
            mptAlice.authorize({.account = alice, .holder = bob, .flags = tfMPTUnauthorize});

            mptAlice.claw(alice, bob, 100);
        }
    }

    void
    testTokensEquality()
    {
        using namespace test::jtx;
        testcase("Tokens Equality");
        Currency const cur1{toCurrency("CU1")};
        Currency const cur2{toCurrency("CU2")};
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
            testcase("Test STAmount MPT arithmetic");
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
            testcase("Test MPTAmount arithmetic");
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
            json::Value const jv = toJson(issue1);
            BEAST_EXPECT(jv[jss::mpt_issuance_id] == to_string(asset1.get<MPTIssue>()));
            BEAST_EXPECT(issue1 == mptIssueFromJson(jv));
        }

        {
            testcase("Test Asset from/to Json");
            json::Value const jv = toJson(asset1);
            BEAST_EXPECT(jv[jss::mpt_issuance_id] == to_string(asset1.get<MPTIssue>()));
            BEAST_EXPECT(
                to_string(jv) ==
                "{\"mpt_issuance_id\":"
                "\"00000001A407AF5856CCF3C42619DAA925813FC955C72983\"}");
            BEAST_EXPECT(asset1 == assetFromJson(jv));
        }
    }

    void
    testInvalidCreateDynamic(FeatureBitset features)
    {
        testcase("invalid MPTokenIssuanceCreate for DynamicMPT");

        using namespace test::jtx;
        Account const alice("alice");

        // Can not provide MutableFlags when DynamicMPT amendment is not enabled
        {
            Env env{*this, features - featureDynamicMPT};
            MPTTester mptAlice(env, alice);
            mptAlice.create({.ownerCount = 0, .mutableFlags = 2, .err = temDISABLED});
            mptAlice.create({.ownerCount = 0, .mutableFlags = 0, .err = temDISABLED});
        }

        // MutableFlags contains invalid values
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice);

            // Value 1 is reserved for MPT lock.
            mptAlice.create({.ownerCount = 0, .mutableFlags = 1, .err = temINVALID_FLAG});
            mptAlice.create({.ownerCount = 0, .mutableFlags = 17, .err = temINVALID_FLAG});
            mptAlice.create({.ownerCount = 0, .mutableFlags = 65535, .err = temINVALID_FLAG});

            // MutableFlags can not be 0
            mptAlice.create({.ownerCount = 0, .mutableFlags = 0, .err = temINVALID_FLAG});
        }
    }

    void
    testInvalidSetDynamic(FeatureBitset features)
    {
        testcase("invalid MPTokenIssuanceSet for DynamicMPT");

        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");

        // Can not provide MutableFlags, MPTokenMetadata or TransferFee when
        // DynamicMPT amendment is not enabled
        {
            Env env{*this, features - featureDynamicMPT};
            MPTTester mptAlice(env, alice, {.holders = {bob}});
            auto const mptID = makeMptID(env.seq(alice), alice);

            // MutableFlags is not allowed when DynamicMPT is not enabled
            mptAlice.set({.account = alice, .id = mptID, .mutableFlags = 2, .err = temDISABLED});
            mptAlice.set({.account = alice, .id = mptID, .mutableFlags = 0, .err = temDISABLED});

            // MPTokenMetadata is not allowed when DynamicMPT is not enabled
            mptAlice.set({.account = alice, .id = mptID, .metadata = "test", .err = temDISABLED});
            mptAlice.set({.account = alice, .id = mptID, .metadata = "", .err = temDISABLED});

            // TransferFee is not allowed when DynamicMPT is not enabled
            mptAlice.set({.account = alice, .id = mptID, .transferFee = 100, .err = temDISABLED});
            mptAlice.set({.account = alice, .id = mptID, .transferFee = 0, .err = temDISABLED});
        }

        // Can not provide holder when MutableFlags, MPTokenMetadata or
        // TransferFee is present
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}});
            auto const mptID = makeMptID(env.seq(alice), alice);

            // Holder is not allowed when MutableFlags is present
            mptAlice.set(
                {.account = alice,
                 .holder = bob,
                 .id = mptID,
                 .mutableFlags = 2,
                 .err = temMALFORMED});

            // Holder is not allowed when MPTokenMetadata is present
            mptAlice.set(
                {.account = alice,
                 .holder = bob,
                 .id = mptID,
                 .metadata = "test",
                 .err = temMALFORMED});

            // Holder is not allowed when TransferFee is present
            mptAlice.set(
                {.account = alice,
                 .holder = bob,
                 .id = mptID,
                 .transferFee = 100,
                 .err = temMALFORMED});
        }

        // Can not set Flags when MutableFlags, MPTokenMetadata or
        // TransferFee is present
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}});
            mptAlice.create(
                {.ownerCount = 1,
                 .mutableFlags = tmfMPTCanMutateMetadata | tmfMPTCanEnableCanLock |
                     tmfMPTCanMutateTransferFee});

            // Setting flags is not allowed when MutableFlags is present
            mptAlice.set(
                {.account = alice, .flags = tfMPTCanLock, .mutableFlags = 2, .err = temMALFORMED});

            // Setting flags is not allowed when MPTokenMetadata is present
            mptAlice.set(
                {.account = alice, .flags = tfMPTCanLock, .metadata = "test", .err = temMALFORMED});

            // setting flags is not allowed when TransferFee is present
            mptAlice.set(
                {.account = alice, .flags = tfMPTCanLock, .transferFee = 100, .err = temMALFORMED});
        }

        // Flags being 0 or tfFullyCanonicalSig is fine
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.transferFee = 10,
                 .ownerCount = 1,
                 .flags = tfMPTCanTransfer,
                 .mutableFlags = tmfMPTCanMutateTransferFee | tmfMPTCanMutateMetadata});

            mptAlice.set({.account = alice, .flags = 0, .transferFee = 100, .metadata = "test"});
            mptAlice.set(
                {.account = alice,
                 .flags = tfFullyCanonicalSig,
                 .transferFee = 200,
                 .metadata = "test2"});
        }

        // Invalid MutableFlags
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}});
            auto const mptID = makeMptID(env.seq(alice), alice);

            for (auto const flags : {10000, 0, 5000})
            {
                mptAlice.set(
                    {.account = alice, .id = mptID, .mutableFlags = flags, .err = temINVALID_FLAG});
            }
        }

        // Can not mutate flag which is not mutable
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1});

            auto const mutableFlags = {
                tmfMPTSetCanLock,
                tmfMPTSetRequireAuth,
                tmfMPTSetCanEscrow,
                tmfMPTSetCanTrade,
                tmfMPTSetCanTransfer,
                tmfMPTSetCanClawback};

            for (auto const& mutableFlag : mutableFlags)
            {
                mptAlice.set(
                    {.account = alice, .mutableFlags = mutableFlag, .err = tecNO_PERMISSION});
            }
        }

        // Metadata exceeding max length
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .mutableFlags = tmfMPTCanMutateMetadata});

            std::string const metadata(kMaxMpTokenMetadataLength + 1, 'a');
            mptAlice.set({.account = alice, .metadata = metadata, .err = temMALFORMED});
        }

        // Can not mutate metadata when it is not mutable
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1});
            mptAlice.set({.account = alice, .metadata = "test", .err = tecNO_PERMISSION});
        }

        // Transfer fee exceeding the max value
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}});
            auto const mptID = makeMptID(env.seq(alice), alice);

            mptAlice.create({.ownerCount = 1, .mutableFlags = tmfMPTCanMutateTransferFee});

            mptAlice.set(
                {.account = alice,
                 .id = mptID,
                 .transferFee = kMaxTransferFee + 1,
                 .err = temBAD_TRANSFER_FEE});
        }

        // Can not set non-zero transfer fee when MPTCanTransfer is not set
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.ownerCount = 1,
                 .mutableFlags = tmfMPTCanMutateTransferFee | tmfMPTCanEnableCanTransfer});

            mptAlice.set({.account = alice, .transferFee = 100, .err = tecNO_PERMISSION});

            // Can not set transfer fee even when trying to set MPTCanTransfer
            // at the same time. MPTCanTransfer must be set first, then transfer
            // fee can be set in a separate transaction.
            mptAlice.set(
                {.account = alice,
                 .mutableFlags = tmfMPTSetCanTransfer,
                 .transferFee = 100,
                 .err = tecNO_PERMISSION});
        }

        // Can not mutate transfer fee when it is not mutable
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.transferFee = 10, .ownerCount = 1, .flags = tfMPTCanTransfer});

            mptAlice.set({.account = alice, .transferFee = 100, .err = tecNO_PERMISSION});

            mptAlice.set({.account = alice, .transferFee = 0, .err = tecNO_PERMISSION});
        }

        // Set some flags mutable. Can not mutate the others
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.ownerCount = 1,
                 .mutableFlags = tmfMPTCanEnableCanTrade | tmfMPTCanEnableCanTransfer |
                     tmfMPTCanMutateMetadata});

            // Can not mutate transfer fee
            mptAlice.set({.account = alice, .transferFee = 100, .err = tecNO_PERMISSION});

            auto const invalidFlags = {
                tmfMPTSetCanLock, tmfMPTSetRequireAuth, tmfMPTSetCanEscrow, tmfMPTSetCanClawback};

            // Can not mutate flags which are not mutable
            for (auto const& mutableFlag : invalidFlags)
            {
                mptAlice.set(
                    {.account = alice, .mutableFlags = mutableFlag, .err = tecNO_PERMISSION});
            }

            // Can mutate MPTCanTrade
            mptAlice.set({.account = alice, .mutableFlags = tmfMPTSetCanTrade});

            // Can mutate MPTCanTransfer
            mptAlice.set({.account = alice, .mutableFlags = tmfMPTSetCanTransfer});

            // Can mutate metadata
            mptAlice.set({.account = alice, .metadata = "test"});
            mptAlice.set({.account = alice, .metadata = ""});
        }
    }

    void
    testMutateMPT(FeatureBitset features)
    {
        testcase("Mutate MPT");
        using namespace test::jtx;

        Account const alice("alice");

        // Mutate metadata
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice);
            mptAlice.create(
                {.metadata = "test", .ownerCount = 1, .mutableFlags = tmfMPTCanMutateMetadata});

            std::vector<std::string> const metadatas = {
                "mutate metadata",
                "mutate metadata 2",
                "mutate metadata 3",
                "mutate metadata 3",
                "test",
                "mutate metadata"};

            for (auto const& metadata : metadatas)
            {
                mptAlice.set({.account = alice, .metadata = metadata});
                BEAST_EXPECT(mptAlice.checkMetadata(metadata));
            }

            // Metadata being empty will remove the field
            mptAlice.set({.account = alice, .metadata = ""});
            BEAST_EXPECT(!mptAlice.isMetadataPresent());
        }

        // Mutate transfer fee
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice);
            mptAlice.create(
                {.transferFee = 100,
                 .metadata = "test",
                 .ownerCount = 1,
                 .flags = tfMPTCanTransfer,
                 .mutableFlags = tmfMPTCanMutateTransferFee});

            for (std::uint16_t const fee :
                 std::initializer_list<std::uint16_t>{1, 10, 100, 200, 500, 1000, kMaxTransferFee})
            {
                mptAlice.set({.account = alice, .transferFee = fee});
                BEAST_EXPECT(mptAlice.checkTransferFee(fee));
            }

            // Setting TransferFee to zero will remove the field
            mptAlice.set({.account = alice, .transferFee = 0});
            BEAST_EXPECT(!mptAlice.isTransferFeePresent());

            // Set transfer fee again
            mptAlice.set({.account = alice, .transferFee = 10});
            BEAST_EXPECT(mptAlice.checkTransferFee(10));
        }

        // Test mutable flag enablement
        {
            auto testFlagSet = [&](std::uint32_t createFlags, std::uint32_t setFlags) {
                Env env{*this, features};
                MPTTester mptAlice(env, alice);

                // Create the MPT object with the specified initial flags
                mptAlice.create({.metadata = "test", .ownerCount = 1, .mutableFlags = createFlags});

                // Setting the same mutable capability more than once is harmless.
                mptAlice.set({.account = alice, .mutableFlags = setFlags});
                mptAlice.set({.account = alice, .mutableFlags = setFlags});
            };

            testFlagSet(tmfMPTCanEnableCanLock, tmfMPTSetCanLock);
            testFlagSet(tmfMPTCanEnableRequireAuth, tmfMPTSetRequireAuth);
            testFlagSet(tmfMPTCanEnableCanEscrow, tmfMPTSetCanEscrow);
            testFlagSet(tmfMPTCanEnableCanTrade, tmfMPTSetCanTrade);
            testFlagSet(tmfMPTCanEnableCanTransfer, tmfMPTSetCanTransfer);
            testFlagSet(tmfMPTCanEnableCanClawback, tmfMPTSetCanClawback);
        }
    }

    void
    testMutateCanLock(FeatureBitset features)
    {
        testcase("Mutate MPTCanLock");
        using namespace test::jtx;

        Account const alice("alice");
        Account const bob("bob");

        // Individual lock
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}});
            mptAlice.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanLock | tfMPTCanTransfer,
                 .mutableFlags = tmfMPTCanEnableCanLock | tmfMPTCanEnableCanTrade |
                     tmfMPTCanMutateTransferFee});
            mptAlice.authorize({.account = bob, .holderCount = 1});

            // Lock bob's mptoken
            mptAlice.set({.account = alice, .holder = bob, .flags = tfMPTLock});

            // Can mutate the mutable flags and fields
            mptAlice.set({.account = alice, .mutableFlags = tmfMPTSetCanLock});
            mptAlice.set({.account = alice, .mutableFlags = tmfMPTSetCanTrade});
            mptAlice.set({.account = alice, .transferFee = 200});
        }

        // Global lock
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}});
            mptAlice.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanLock,
                 .mutableFlags = tmfMPTCanEnableCanLock | tmfMPTCanEnableCanClawback |
                     tmfMPTCanMutateMetadata});
            mptAlice.authorize({.account = bob, .holderCount = 1});

            // Lock issuance
            mptAlice.set({.account = alice, .flags = tfMPTLock});

            // Can mutate the mutable flags and fields
            mptAlice.set({.account = alice, .mutableFlags = tmfMPTSetCanLock});
            mptAlice.set({.account = alice, .mutableFlags = tmfMPTSetCanClawback});
            mptAlice.set({.account = alice, .metadata = "mutate"});
        }

        // Test lock and unlock after enabling MPTCanLock
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}});
            mptAlice.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .mutableFlags = tmfMPTCanEnableCanLock | tmfMPTCanEnableCanClawback |
                     tmfMPTCanMutateMetadata});
            mptAlice.authorize({.account = bob, .holderCount = 1});

            // Can not lock or unlock before MPTCanLock is enabled
            mptAlice.set({.account = alice, .flags = tfMPTLock, .err = tecNO_PERMISSION});
            mptAlice.set({.account = alice, .flags = tfMPTUnlock, .err = tecNO_PERMISSION});
            mptAlice.set(
                {.account = alice, .holder = bob, .flags = tfMPTLock, .err = tecNO_PERMISSION});
            mptAlice.set(
                {.account = alice, .holder = bob, .flags = tfMPTUnlock, .err = tecNO_PERMISSION});

            // Set MPTCanLock
            mptAlice.set({.account = alice, .mutableFlags = tmfMPTSetCanLock});

            // Can lock and unlock
            mptAlice.set({.account = alice, .flags = tfMPTLock});
            mptAlice.set({.account = alice, .holder = bob, .flags = tfMPTLock});
            mptAlice.set({.account = alice, .flags = tfMPTUnlock});
            mptAlice.set({.account = alice, .holder = bob, .flags = tfMPTUnlock});
        }
    }

    void
    testMutateRequireAuth(FeatureBitset features)
    {
        testcase("Mutate MPTRequireAuth");
        using namespace test::jtx;

        // test enabling RequireAuth flag on the issuance and its effect on payment
        // authorization
        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");

        MPTTester mptAlice(env, alice, {.holders = {bob}});
        mptAlice.create(
            {.ownerCount = 1,
             .flags = tfMPTCanTransfer,
             .mutableFlags = tmfMPTCanEnableRequireAuth});

        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 1000);

        // Set RequireAuth because it is mutable.
        mptAlice.set({.account = alice, .mutableFlags = tmfMPTSetRequireAuth});

        // This should fail because bob is not authorized yet.
        mptAlice.pay(alice, bob, 1000, tecNO_AUTH);

        // Issuer authorizes bob and pay should succeed.
        mptAlice.authorize({.account = alice, .holder = bob});
        mptAlice.pay(alice, bob, 1000);
    }

    void
    testMutateCanEscrow(FeatureBitset features)
    {
        testcase("Mutate MPTCanEscrow");
        using namespace test::jtx;
        using namespace std::literals;

        Env env{*this, features};
        auto const baseFee = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");

        MPTTester mptAlice(env, alice, {.holders = {carol, bob}});
        mptAlice.create(
            {.ownerCount = 1,
             .holderCount = 0,
             .flags = tfMPTCanTransfer,
             .mutableFlags = tmfMPTCanEnableCanEscrow});
        mptAlice.authorize({.account = carol});
        mptAlice.authorize({.account = bob});

        auto const mpt = mptAlice["MPT"];
        env(pay(alice, carol, mpt(10'000)));
        env(pay(alice, bob, mpt(10'000)));
        env.close();

        // MPTCanEscrow is not enabled
        env(escrow::create(carol, bob, mpt(3)),
            escrow::kCondition(escrow::kCb1),
            escrow::kFinishTime(env.now() + 1s),
            Fee(baseFee * 150),
            Ter(tecNO_PERMISSION));

        // MPTCanEscrow is enabled now
        mptAlice.set({.account = alice, .mutableFlags = tmfMPTSetCanEscrow});
        env(escrow::create(carol, bob, mpt(3)),
            escrow::kCondition(escrow::kCb1),
            escrow::kFinishTime(env.now() + 1s),
            Fee(baseFee * 150));
    }

    void
    testMutateCanTransfer(FeatureBitset features)
    {
        testcase("Mutate MPTCanTransfer");

        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");

        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});
            mptAlice.create(
                {.ownerCount = 1,
                 .mutableFlags = tmfMPTCanEnableCanTransfer | tmfMPTCanMutateTransferFee});

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});

            // Pay to bob
            mptAlice.pay(alice, bob, 1000);

            // Bob can not pay carol since MPTCanTransfer is not set
            mptAlice.pay(bob, carol, 50, tecNO_AUTH);

            // Can not set non-zero transfer fee when MPTCanTransfer is not set
            mptAlice.set({.account = alice, .transferFee = 100, .err = tecNO_PERMISSION});

            // Can not set non-zero transfer fee even when trying to set
            // MPTCanTransfer at the same time
            mptAlice.set(
                {.account = alice,
                 .mutableFlags = tmfMPTSetCanTransfer,
                 .transferFee = 100,
                 .err = tecNO_PERMISSION});

            // Alice sets MPTCanTransfer
            mptAlice.set({.account = alice, .mutableFlags = tmfMPTSetCanTransfer});

            // Can set transfer fee now
            BEAST_EXPECT(!mptAlice.isTransferFeePresent());
            mptAlice.set({.account = alice, .transferFee = 100});
            BEAST_EXPECT(mptAlice.isTransferFeePresent());

            // Bob can pay carol
            MPT const mptc = mptAlice;
            if (!features[featureMPTokensV2])
            {
                mptAlice.pay(bob, carol, 50);
                BEAST_EXPECT(env.balance(carol, mptc) == mptc(50));
            }
            else
            {
                // The difference is due to the rounding in MPT/DEX.
                // 1 MPTC is the transfer fee paid by bob to the issuer.
                env(pay(bob, carol, mptAlice(50)), Txflags(tfPartialPayment));
                BEAST_EXPECT(env.balance(carol, mptc) == mptc(49));
            }
        }

        // Can set transfer fee to zero when tmfMPTCanMutateTransferFee is set.
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});
            mptAlice.create(
                {.transferFee = 100,
                 .ownerCount = 1,
                 .flags = tfMPTCanTransfer,
                 .mutableFlags = tmfMPTCanMutateTransferFee});

            BEAST_EXPECT(mptAlice.checkTransferFee(100));

            // Setting transfer fee to zero removes the field.
            mptAlice.set({.account = alice, .transferFee = 0});
            BEAST_EXPECT(!mptAlice.isTransferFeePresent());
        }
    }

    void
    testMutateCanClawback(FeatureBitset features)
    {
        testcase("Mutate MPTCanClawback");

        using namespace test::jtx;
        Env env(*this, features);
        Account const alice{"alice"};
        Account const bob{"bob"};

        MPTTester mptAlice(env, alice, {.holders = {bob}});

        mptAlice.create(
            {.ownerCount = 1, .holderCount = 0, .mutableFlags = tmfMPTCanEnableCanClawback});

        // Bob creates an MPToken
        mptAlice.authorize({.account = bob});

        // Alice pays bob 100 tokens
        mptAlice.pay(alice, bob, 100);

        // MPTCanClawback is not enabled
        mptAlice.claw(alice, bob, 1, tecNO_PERMISSION);

        // Enable MPTCanClawback
        mptAlice.set({.account = alice, .mutableFlags = tmfMPTSetCanClawback});

        // Can clawback now
        mptAlice.claw(alice, bob, 1);
    }

    void
    testMultiSendMaximumAmount(FeatureBitset features)
    {
        // Verify that directSendNoLimitMultiMPT correctly enforces MaximumAmount
        // when the issuer sends to multiple receivers. Pre-fixCleanup3_1_3,
        // a stale view.read() snapshot caused per-iteration checks to miss
        // aggregate overflows. Post-fix, a running total is used instead.
        testcase("Multi-send MaximumAmount enforcement");

        using namespace test::jtx;

        Account const issuer("issuer");
        Account const alice("alice");
        Account const bob("bob");

        static constexpr std::uint64_t kMaxAmt = 150;
        Env env{*this, features};

        MPTTester mptTester(env, issuer, {.holders = {alice, bob}});
        mptTester.create({.maxAmt = kMaxAmt, .ownerCount = 1, .flags = tfMPTCanTransfer});
        mptTester.authorize({.account = alice});
        mptTester.authorize({.account = bob});

        Asset const asset{MPTIssue{mptTester.issuanceID()}};

        // Each test case creates a fresh ApplyView and calls
        // accountSendMulti from the issuer to the given receivers.
        auto const runTest = [&](MultiplePaymentDestinations const& receivers,
                                 TER expectedTer,
                                 std::optional<std::uint64_t> expectedOutstanding,
                                 std::string const& label) {
            ApplyViewImpl av(&*env.current(), TapNone);
            auto const ter =
                accountSendMulti(av, issuer.id(), asset, receivers, env.app().getJournal("View"));
            BEAST_EXPECTS(ter == expectedTer, label);

            // Only verify OutstandingAmount on success — on error the
            // view may contain partial state and must be discarded.
            if (expectedOutstanding)
            {
                auto const sle = av.peek(keylet::mptIssuance(mptTester.issuanceID()));
                if (!BEAST_EXPECT(sle))
                    return;
                BEAST_EXPECTS(sle->getFieldU64(sfOutstandingAmount) == *expectedOutstanding, label);
            }
        };

        using R = MultiplePaymentDestinations;

        // Post-amendment: aggregate check with running total
        runTest(
            R{{alice.id(), 100}, {bob.id(), 100}},
            tecPATH_DRY,
            std::nullopt,
            "aggregate exceeds max");

        runTest(R{{alice.id(), 75}, {bob.id(), 75}}, tesSUCCESS, kMaxAmt, "aggregate at boundary");

        runTest(R{{alice.id(), 50}, {bob.id(), 50}}, tesSUCCESS, 100, "aggregate within limit");

        runTest(
            R{{alice.id(), 150}, {bob.id(), 0}},
            tesSUCCESS,
            kMaxAmt,
            "one receiver at max, other zero");

        runTest(
            R{{alice.id(), 151}, {bob.id(), 0}},
            tecPATH_DRY,
            std::nullopt,
            "one receiver exceeds max, other zero");

        // Issue 50 tokens so outstandingAmount is nonzero, then verify
        // the third condition: outstandingAmount > maximumAmount - sendAmount - totalSendAmount
        mptTester.pay(issuer, alice, 50);
        env.close();

        // maxAmt=150, outstanding=50, so 100 more available
        runTest(
            R{{alice.id(), 50}, {bob.id(), 50}},
            tesSUCCESS,
            kMaxAmt,
            "nonzero outstanding, aggregate at boundary");

        runTest(
            R{{alice.id(), 50}, {bob.id(), 51}},
            tecPATH_DRY,
            std::nullopt,
            "nonzero outstanding, aggregate exceeds max");

        runTest(
            R{{alice.id(), 100}, {bob.id(), 0}},
            tesSUCCESS,
            kMaxAmt,
            "nonzero outstanding, single send at remaining capacity");

        runTest(
            R{{alice.id(), 101}, {bob.id(), 0}},
            tecPATH_DRY,
            std::nullopt,
            "nonzero outstanding, single send exceeds remaining capacity");

        // Pre-amendment: the stale per-iteration check allows each
        // individual send (100 <= 150) even though the aggregate (200)
        // exceeds MaximumAmount. Preserved for ledger replay.
        {
            // KNOWN BUG (pre-fixCleanup3_1_3): preserved for ledger replay only
            env.disableFeature(fixCleanup3_1_3);
            runTest(
                R{{alice.id(), 100}, {bob.id(), 100}},
                tesSUCCESS,
                250,
                "pre-amendment allows over-send");
            env.enableFeature(fixCleanup3_1_3);
        }
    }

    void
    testOfferCrossing(FeatureBitset features)
    {
        testcase("Offer Crossing");
        using namespace test::jtx;
        Account const gw = Account("gw");
        Account const alice = Account("alice");
        Account const carol = Account("carol");
        auto const usd = gw["USD"];

        // Blocking flags
        for (auto flags :
             {tfMPTCanTrade | tfMPTCanLock | tfMPTCanClawback,  // global lock - holder, issuer fail
              tfMPTCanTrade | tfMPTRequireAuth,                 // not authorized - holder fails
              tfMPTCanTrade,                                    // holder, issuer succeed
              tfMPTCanTrade | tfMPTCanLock,                     // local lock - holder fails
              tfMPTCanTransfer})                                // can't trade - holder, issuer fail
        {
            Env env{*this, features};
            env.fund(XRP(1'000), gw, alice);
            env.close();

            // Use CanClawback flag to distinguish global from local lock
            bool const lockMPToken = (flags & (tfMPTCanLock | tfMPTCanClawback)) == tfMPTCanLock;
            bool const lockMPTIssue =
                (flags & (tfMPTCanLock | tfMPTCanClawback)) == (tfMPTCanLock | tfMPTCanClawback);
            bool const requireAuth = (flags & tfMPTRequireAuth) != 0u;

            auto mptTester = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 1'000,
                 .flags = flags,
                 .authHolder = true});
            MPT const btc = mptTester;

            if (requireAuth)
                mptTester.authorize({.account = gw, .holder = alice, .flags = tfMPTUnauthorize});
            if (lockMPToken)
            {
                mptTester.set({.holder = alice, .flags = tfMPTLock});
            }
            else if (lockMPTIssue)
            {
                mptTester.set({.flags = tfMPTLock});
            }

            auto testOffer =
                [&](Account const& account, auto const& buy, auto const& sell, bool buyUSD) {
                    auto error = [&](auto const err) -> TER {
                        if (account == gw)
                            return tesSUCCESS;
                        return err;
                    };
                    auto const [errBuy, errSell] = [&]() -> std::pair<TER, TER> {
                        // Global lock
                        if (lockMPTIssue)
                            return std::make_pair(tecLOCKED, tecLOCKED);
                        // Local lock
                        if (lockMPToken)
                            return std::make_pair(error(tecLOCKED), error(tecUNFUNDED_OFFER));
                        // MPToken doesn't exist
                        if (requireAuth)
                            return std::make_pair(error(tecNO_AUTH), error(tecUNFUNDED_OFFER));
                        if (flags & tfMPTCanTransfer)
                            return std::make_pair(tecNO_PERMISSION, tecNO_PERMISSION);
                        return std::make_pair(tesSUCCESS, tesSUCCESS);
                    }();

                    auto const err = buyUSD ? errBuy : errSell;

                    auto seq(env.seq(account));
                    env(offer(account, buy(10), sell(10)), Ter(err));
                    env(offerCancel(account, seq));
                    env.close();
                };

            auto testOffers = [&](Account const& account) {
                testOffer(account, XRP, btc, false);
                testOffer(account, btc, XRP, true);
            };
            testOffers(alice);
            testOffers(gw);
        }

        // MPTokenV2 is disabled
        {
            Env env{*this, features - featureMPTokensV2};

            MPTTester mptTester(env, gw, {.holders = {alice}});

            mptTester.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});

            mptTester.authorize({.account = alice});
            mptTester.pay(gw, alice, 200);

            env(offer(alice, XRP(100), mptTester.mpt(101)), Ter(temDISABLED));
            env.close();
        }

        // MPTokenIssuance object doesn't exist
        {
            Env env(*this);
            env.fund(XRP(1'000), gw, alice);
            env.close();
            MPT const btc = MPTTester({.env = env, .issuer = gw, .holders = {alice}, .pay = 100});
            MPT const eth = MPT(gw, 1);

            env(offer(alice, eth(10), btc(10)), Ter(tecOBJECT_NOT_FOUND));
            env(offer(alice, btc(10), eth(10)), Ter(tecUNFUNDED_OFFER));
        }

        // MPToken object doesn't exist and the account is not the issuer of MPT
        {
            Env env(*this);
            env.fund(XRP(1'000), gw, alice);
            MPTTester const btc({.env = env, .issuer = gw, .holders = {alice}, .pay = 100});
            MPTTester const eth({.env = env, .issuer = gw});

            env(offer(alice, eth(10), btc(10)));
            env(offer(alice, btc(10), eth(10)), Ter(tecUNFUNDED_OFFER));
        }

        // MPTLock flag is set: MPT/MPT offer crossing with independent issuers.
        // gw2 issues BTC and gw issues ETH so each asset can be frozen independently.
        // Passive setup: bob sells BTC (offer(bob, ETH, BTC)); dan sells ETH (offer(dan, BTC,
        // ETH)).
        {
            Account const gw2 = Account("gw2");
            Account const bob = Account("bob");
            Account const dan = Account("dan");
            Env env(*this);
            env.fund(XRP(1'000), gw, gw2, alice, carol, bob, dan);
            MPTTester btc(
                {.env = env,
                 .issuer = gw2,
                 .holders = {alice, carol, bob, dan, gw},
                 .pay = 100,
                 .flags = tfMPTCanLock | kMptDexFlags});
            MPTTester eth(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol, bob, dan, gw2},
                 .pay = 100,
                 .flags = tfMPTCanLock | kMptDexFlags});

            // bob sells BTC (takerGets=BTC); dan sells ETH (takerGets=ETH)
            env(offer(bob, eth(10), btc(10)), Txflags(tfPassive));
            env(offer(dan, btc(10), eth(10)), Txflags(tfPassive));
            env.close();

            // --- Individual lock on BTC ---
            // alice sells locked BTC (takerGets): balance zeroed, offer unfunded
            btc.set({.holder = alice, .flags = tfMPTLock});
            env(offer(alice, eth(1), btc(1)), Ter(tecUNFUNDED_OFFER));
            btc.set({.holder = alice, .flags = tfMPTUnlock});
            // carol buys locked BTC (takerPays): locked MPToken cannot receive
            btc.set({.holder = carol, .flags = tfMPTLock});
            env(offer(carol, btc(1), eth(1)), Ter(tecLOCKED));
            btc.set({.holder = carol, .flags = tfMPTUnlock});
            // gw2 is BTC issuer: individual lock on holders does not affect issuer
            env(offer(gw2, eth(1), btc(1)));

            // --- Individual lock on ETH ---
            // alice sells locked ETH (takerGets): balance zeroed, offer unfunded
            eth.set({.holder = alice, .flags = tfMPTLock});
            env(offer(alice, btc(1), eth(1)), Ter(tecUNFUNDED_OFFER));
            eth.set({.holder = alice, .flags = tfMPTUnlock});
            // carol buys locked ETH (takerPays): locked MPToken cannot receive
            eth.set({.holder = carol, .flags = tfMPTLock});
            env(offer(carol, eth(1), btc(1)), Ter(tecLOCKED));
            eth.set({.holder = carol, .flags = tfMPTUnlock});
            // gw is ETH issuer: individual lock on holders does not affect issuer
            env(offer(gw, btc(1), eth(1)));

            // --- Global lock on BTC ---
            // All accounts fail regardless of role: global lock is checked in OfferCreate
            // before offer crossing, so it applies even to the issuer.
            btc.set({.flags = tfMPTLock});
            env(offer(alice, eth(1), btc(1)), Ter(tecLOCKED));  // alice sells BTC
            env(offer(alice, btc(1), eth(1)), Ter(tecLOCKED));  // alice buys BTC
            env(offer(gw2, eth(1), btc(1)), Ter(tecLOCKED));    // gw2 is BTC issuer, still fails
            btc.set({.flags = tfMPTUnlock});

            // --- Global lock on ETH ---
            eth.set({.flags = tfMPTLock});
            env(offer(alice, btc(1), eth(1)), Ter(tecLOCKED));  // alice sells ETH
            env(offer(alice, eth(1), btc(1)), Ter(tecLOCKED));  // alice buys ETH
            env(offer(gw, btc(1), eth(1)), Ter(tecLOCKED));     // gw is ETH issuer, still fails
            eth.set({.flags = tfMPTUnlock});

            // --- After all locks cleared: normal crossing succeeds ---
            env(offer(alice, eth(1), btc(1)));
            env(offer(carol, btc(1), eth(1)));
        }

        // MPTRequireAuth flag is set and the account is not authorized
        {
            Env env(*this);
            env.fund(XRP(1'000), gw, alice);
            MPTTester btc(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 100,
                 .flags = tfMPTRequireAuth | kMptDexFlags,
                 .authHolder = true});
            MPTTester const eth(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 100,
                 .flags = tfMPTRequireAuth | kMptDexFlags,
                 .authHolder = true});

            btc.authorize({.account = gw, .holder = alice, .flags = tfMPTUnauthorize});

            env(offer(alice, eth(10), btc(10)), Ter(tecUNFUNDED_OFFER));

            // issuer can create

            env(offer(gw, eth(10), btc(10)));
            env.close();
        }

        // MPTCanTransfer is not set and the account is not the issuer of MPT
        {
            Env env(*this);
            env.fund(XRP(1'000), gw, alice, carol);
            MPTTester const btc(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .pay = 100,
                 .flags = tfMPTCanTrade | tfMPTCanTransfer});
            MPTTester const eth(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .pay = 100,
                 .flags = tfMPTCanTrade});

            // Can create
            env(offer(alice, eth(10), btc(10)), Txflags(tfPassive));
            BEAST_EXPECT(getAccountOffers(env, alice)[jss::offers].size() == 1);

            // issuer can create
            env(offer(gw, eth(10), btc(10)), Txflags(tfPassive));
            env.close();

            // can cross issuer's offer, other offers are removed
            env(offer(carol, btc(10), eth(10)));
            BEAST_EXPECT(expectOffers(env, alice, 0));
            BEAST_EXPECT(expectOffers(env, gw, 0));
            BEAST_EXPECT(expectOffers(env, carol, 0));
            // can't cross holder's offer, holder's offer is removed
            env(offer(alice, eth(10), btc(10)), Txflags(tfPassive));
            env(offer(carol, btc(10), eth(10)));
            BEAST_EXPECT(expectOffers(env, alice, 0));
            BEAST_EXPECT(expectOffers(env, carol, 1));
        }

        // MPTCanTrade is disabled
        {
            Env env(*this);
            env.fund(XRP(1'000), gw, alice, carol);
            MPTTester const btc(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .pay = 100,
                 .flags = tfMPTCanTransfer,
                 .mutableFlags = tmfMPTCanEnableCanTrade});
            MPTTester const eth(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .pay = 100,
                 .flags = tfMPTCanTrade,
                 .mutableFlags = tmfMPTCanEnableCanTrade});

            // Can't create
            env(offer(gw, eth(10), btc(10)), Ter(tecNO_PERMISSION));
            env.close();
        }

        // XRP/MPT
        {
            Env env{*this, features};

            MPTTester mptTester(env, gw, {.holders = {alice, carol}});

            mptTester.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt = mptTester["MPT"];

            mptTester.authorize({.account = alice});
            mptTester.pay(gw, alice, 200);

            mptTester.authorize({.account = carol});
            mptTester.pay(gw, carol, 200);

            env(offer(alice, XRP(100), mpt(101)));
            env.close();
            BEAST_EXPECT(expectOffers(env, alice, 1, {{Amounts{XRP(100), mpt(101)}}}));

            env(offer(carol, mpt(101), XRP(100)));
            env.close();
            BEAST_EXPECT(expectOffers(env, alice, 0));
            BEAST_EXPECT(expectOffers(env, carol, 0));
            BEAST_EXPECT(mptTester.checkMPTokenOutstandingAmount(400));
            BEAST_EXPECT(mptTester.checkMPTokenAmount(alice, 99));
            BEAST_EXPECT(mptTester.checkMPTokenAmount(carol, 301));
        }

        // IOU/MPT
        {
            Env env{*this, features};

            MPTTester mptTester(env, gw, {.holders = {alice, carol}});

            mptTester.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt = mptTester["MPT"];

            env(trust(alice, usd(2'000)));
            env(pay(gw, alice, usd(1'000)));
            env.close();

            env(trust(carol, usd(2'000)));
            env(pay(gw, carol, usd(1'000)));
            env.close();

            mptTester.authorize({.account = alice});
            mptTester.pay(gw, alice, 200);

            mptTester.authorize({.account = carol});
            mptTester.pay(gw, carol, 200);

            env(offer(alice, usd(100), mpt(101)));
            env.close();
            BEAST_EXPECT(expectOffers(env, alice, 1, {{Amounts{usd(100), mpt(101)}}}));

            env(offer(carol, mpt(101), usd(100)));
            env.close();

            BEAST_EXPECT(env.balance(alice, usd) == usd(1'100));
            BEAST_EXPECT(env.balance(carol, usd) == usd(900));
            BEAST_EXPECT(expectOffers(env, alice, 0));
            BEAST_EXPECT(expectOffers(env, carol, 0));
            BEAST_EXPECT(mptTester.checkMPTokenOutstandingAmount(400));
            BEAST_EXPECT(mptTester.checkMPTokenAmount(alice, 99));
            BEAST_EXPECT(mptTester.checkMPTokenAmount(carol, 301));
        }

        // MPT/MPT
        {
            Env env{*this, features};

            MPTTester mptTester1(env, gw, {.holders = {alice, carol}});
            mptTester1.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt1 = mptTester1["MPT1"];

            MPTTester mptTester2(env, gw, {.holders = {alice, carol}, .fund = false});
            mptTester2.create(
                {.ownerCount = 2, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt2 = mptTester2["MPT2"];

            mptTester1.authorize({.account = alice});
            mptTester1.authorize({.account = carol});
            mptTester1.pay(gw, alice, 200);
            mptTester1.pay(gw, carol, 200);

            mptTester2.authorize({.account = alice});
            mptTester2.authorize({.account = carol});
            mptTester2.pay(gw, alice, 200);
            mptTester2.pay(gw, carol, 200);

            env(offer(alice, mpt2(100), mpt1(101)));
            env.close();
            BEAST_EXPECT(expectOffers(env, alice, 1, {{Amounts{mpt2(100), mpt1(101)}}}));

            env(offer(carol, mpt1(101), mpt2(100)));
            env.close();

            BEAST_EXPECT(expectOffers(env, alice, 0));
            BEAST_EXPECT(expectOffers(env, carol, 0));
            BEAST_EXPECT(mptTester1.checkMPTokenOutstandingAmount(400));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(alice, 99));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(carol, 301));
            BEAST_EXPECT(mptTester2.checkMPTokenOutstandingAmount(400));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(alice, 300));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(carol, 100));
        }
    }

    void
    testCrossAssetPayment(FeatureBitset features)
    {
        testcase("Cross Asset Payment");
        using namespace test::jtx;
        Account const gw = Account("gw");
        Account const gw2 = Account("gw2");
        Account const alice = Account("alice");
        Account const carol = Account("carol");
        Account const bob = Account("bob");
        auto const usd = gw["USD"];

        // Loop
        {
            Env env{*this, features};
            MPTTester mptTester(env, gw, {.holders = {carol, bob}});

            mptTester.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt = mptTester["MPT"];

            mptTester.authorize({.account = carol});
            mptTester.pay(gw, carol, 200);

            mptTester.authorize({.account = bob});

            // holder to holder
            env(pay(carol, bob, mpt(1)),
                test::jtx::Path(~mpt, ~usd, ~mpt),
                Sendmax(XRP(1)),
                Txflags(tfPartialPayment),
                Ter(temBAD_PATH_LOOP));
            env.close();

            // issuer to holder
            env(pay(gw, bob, mpt(1)),
                test::jtx::Path(~mpt, ~usd, ~mpt),
                Sendmax(XRP(1)),
                Txflags(tfPartialPayment),
                Ter(temBAD_PATH_LOOP));
            env.close();

            // holder to issuer
            env(pay(bob, gw, mpt(1)),
                test::jtx::Path(~mpt, ~usd, ~mpt),
                Sendmax(XRP(1)),
                Txflags(tfPartialPayment),
                Ter(temBAD_PATH_LOOP));
            env.close();
        }

        // Rippling
        {
            Env env{*this, features};
            MPTTester mptTester(env, gw, {.holders = {carol, bob}});

            mptTester.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt = mptTester["MPT"];

            mptTester.authorize({.account = carol});
            mptTester.pay(gw, carol, 200);

            mptTester.authorize({.account = bob});

            // holder to holder
            env(pay(carol, bob, mpt(1)),
                test::jtx::Path(~mpt, gw),
                Sendmax(XRP(1)),
                Txflags(tfPartialPayment),
                Ter(temBAD_PATH));
            env.close();

            // issuer to holder
            env(pay(gw, bob, mpt(1)),
                test::jtx::Path(~mpt, carol),
                Sendmax(XRP(1)),
                Txflags(tfPartialPayment),
                Ter(temBAD_PATH));
            env.close();

            // holder to issuer
            env(pay(bob, gw, mpt(1)),
                test::jtx::Path(~mpt, carol),
                Sendmax(XRP(1)),
                Txflags(tfPartialPayment),
                Ter(temBAD_PATH));
            env.close();
        }

        // MPTokenV2 is disabled
        {
            Env env{*this, features - featureMPTokensV2};

            MPTTester mptTester(env, gw, {.holders = {alice}});

            mptTester.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt = mptTester["MPT"];

            mptTester.authorize({.account = alice});

            env(pay(gw, alice, mpt(101)),
                test::jtx::Path(~mpt),
                Sendmax(XRP(100)),
                Txflags(tfPartialPayment),
                Ter(temMALFORMED));
        }

        {
            auto const ed = Account{"ed"};
            Env env{*this, features};
            env.fund(XRP(1'000), gw, alice, carol, bob, ed);
            MPTTester btc(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol, bob},
                 .pay = 1'000,
                 .flags = tfMPTCanLock | kMptDexFlags,
                 .mutableFlags = tmfMPTCanEnableRequireAuth | tmfMPTCanEnableCanTrade |
                     tmfMPTCanEnableCanTransfer});
            MPTTester eth(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol, bob},
                 .pay = 1'000,
                 .flags = tfMPTCanLock | kMptDexFlags,
                 .mutableFlags = tmfMPTCanEnableCanTransfer});
            MPTTester const usd(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol, bob},
                 .pay = 1'000,
                 .flags = kMptDexFlags | tfMPTCanLock,
                 .mutableFlags = tmfMPTCanEnableCanTransfer});
            MPTTester const cad(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol, bob},
                 .pay = 1'000,
                 .flags = kMptDexFlags | tfMPTCanLock,
                 .mutableFlags = tmfMPTCanEnableCanTransfer});

            env(offer(bob, eth(1'000), btc(1'000)), Txflags(tfPassive));
            env.close();
            env(offer(bob, btc(1'000), eth(1'000)), Txflags(tfPassive));
            env.close();

            // MPTokenIssuance doesn't exist

            env(pay(alice, carol, MPT(gw, 1'000)(10)), Sendmax(eth(10)), Ter(tecOBJECT_NOT_FOUND));
            env.close();
            env(pay(alice, carol, eth(10)), Sendmax(MPT(gw)(10)), Ter(tecOBJECT_NOT_FOUND));
            env.close();

            // MPToken object doesn't exist

            // holder and issuer fail
            env(pay(ed, carol, btc(10)), Sendmax(eth(10)), Ter(tecNO_AUTH));
            env(pay(carol, ed, btc(10)), Sendmax(eth(10)), Ter(tecNO_AUTH));
            env(pay(ed, gw, btc(10)), Sendmax(eth(10)), Ter(tecNO_AUTH));
            env(pay(gw, ed, btc(10)), Sendmax(eth(10)), Ter(tecNO_AUTH));
            env.close();

            // MPTRequireAuth is set

            btc.authorize({.account = ed});
            eth.authorize({.account = ed});
            env(pay(gw, ed, eth(100)));
            env(pay(gw, ed, btc(100)));
            env.close();
            btc.set({.mutableFlags = tmfMPTSetRequireAuth});
            // authorize bob to enable the offers trading
            btc.authorize({.account = gw, .holder = bob});
            env.close();
            env(pay(ed, carol, btc(10)), Path(~btc), Sendmax(eth(10)), Ter(tecNO_AUTH));
            env(pay(carol, ed, btc(10)), Path(~btc), Sendmax(eth(10)), Ter(tecNO_AUTH));
            // BTC is transferred from bob to ed, ed is not authorized
            env(pay(gw, ed, btc(10)), Path(~btc), Sendmax(eth(10)), Ter(tecNO_AUTH));
            // BTC is transferred from bob to issuer
            env(pay(ed, gw, btc(10)), Path(~btc), Sendmax(eth(10)));
            // BTC is transferred from issuer to bob
            env(pay(gw, ed, eth(10)), Path(~eth), Sendmax(btc(10)));
            //
            // BTC is transferred from ed to bob, ed is not authorized
            env(pay(ed, gw, eth(10)), Path(~eth), Sendmax(btc(10)), Ter(tecNO_AUTH));
            env.close();
        }

        // MPTCanTransfer is not set.
        {
            auto const ed = Account{"ed"};
            Env env{*this, features};
            env.fund(XRP(1'000), gw, alice, carol, bob, ed);
            MPTTester const btc(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol, bob, ed},
                 .pay = 1'000,
                 .flags = tfMPTCanTrade});
            MPTTester const eth(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol, bob, ed},
                 .pay = 1'000,
                 .flags = kMptDexFlags});

            env(offer(bob, eth(1'000), btc(1'000)), Txflags(tfPassive));
            env.close();
            env(offer(bob, btc(1'000), eth(1'000)), Txflags(tfPassive));
            env.close();

            // Fail regardless if source/destination is the issuer or
            // not since the offer is owned by a holder.
            env(pay(ed, carol, btc(10)), Path(~btc), Sendmax(eth(10)), Ter(tecPATH_PARTIAL));
            env(pay(carol, ed, btc(10)), Path(~btc), Sendmax(eth(10)), Ter(tecPATH_PARTIAL));
            env(pay(ed, carol, eth(10)), Path(~eth), Sendmax(btc(10)), Ter(tecPATH_PARTIAL));
            env(pay(carol, ed, eth(10)), Path(~eth), Sendmax(btc(10)), Ter(tecPATH_PARTIAL));
            // Fail because BTC, which has CanTransfer disabled, is sent to bob
            env(pay(ed, gw, eth(10)), Path(~eth), Sendmax(btc(10)), Ter(tecPATH_PARTIAL));
            env(pay(ed, gw, btc(10)), Path(~btc), Sendmax(eth(10)), Ter(tesSUCCESS));
            env(pay(gw, ed, eth(10)), Path(~eth), Sendmax(btc(10)), Ter(tesSUCCESS));
            // Fail because BTC, which has CanTransfer disabled, is sent to ed
            env(pay(gw, ed, btc(10)), Path(~btc), Sendmax(eth(10)), Ter(tecPATH_PARTIAL));
            env.close();
            env(offer(gw, eth(100), btc(100)), Txflags(tfPassive));
            env.close();
            env(offer(gw, btc(100), eth(100)), Txflags(tfPassive));
            env.close();
            BEAST_EXPECT(expectOffers(env, bob, 2));
            env(pay(ed, carol, btc(10)), Path(~btc), Sendmax(eth(10)), Ter(tesSUCCESS));
            env(pay(ed, carol, eth(10)), Path(~eth), Sendmax(btc(10)), Ter(tesSUCCESS));
            env(pay(gw, carol, btc(10)), Path(~btc), Sendmax(eth(10)), Ter(tesSUCCESS));
            env.close();
            env(pay(ed, gw, btc(10)), Path(~btc), Sendmax(eth(10)));
            env.close();
        }

        // Multiple steps: CAD/USD, USD/BTC, BTC/ETH.
        // takerGets can transfer if:
        //  - CanTransfer is set
        //  - The offer's owner is the issuer
        //  - BookStep is the last step, which means strand's destination is
        //    the issuer
        // takerPays can transfer if
        //  - BookStep is the first step, which means strand's source is
        //    the issuer
        //  - The offer's owner is the issuer
        //  - Previous step is BookStep, which transfers per above
        //  - CanTransfer is set
        {
            // enum to indicate which MPT doesn't set CanTransfer flag.
            enum class NoTransferMPT { BTC, ETH, USD, CAD };

            // Lambda to test multi-step payment with one of the MPTs not setting CanTransfer flag.
            auto const testMultiStepMPTCanTransfer = [&](NoTransferMPT const noTransferMPT,
                                                         auto const& test) {
                auto const getFlags = [&](NoTransferMPT const mpt) {
                    return mpt == noTransferMPT ? tfMPTCanTrade : kMptDexFlags;
                };

                Env env{*this, features};
                env.fund(XRP(1'000), gw, alice, carol, bob);
                env.close();
                MPTTester const btc(
                    {.env = env,
                     .issuer = gw,
                     .holders = {alice, carol, bob},
                     .pay = 1'000,
                     .flags = getFlags(NoTransferMPT::BTC)});
                MPTTester const eth(
                    {.env = env,
                     .issuer = gw,
                     .holders = {alice, carol, bob},
                     .pay = 1'000,
                     .flags = getFlags(NoTransferMPT::ETH)});
                MPTTester const usd(
                    {.env = env,
                     .issuer = gw,
                     .holders = {alice, carol, bob},
                     .pay = 1'000,
                     .flags = getFlags(NoTransferMPT::USD)});
                MPTTester const cad(
                    {.env = env,
                     .issuer = gw,
                     .holders = {alice, carol, bob},
                     .pay = 1'000,
                     .flags = getFlags(NoTransferMPT::CAD)});

                env(offer(bob, cad(100), usd(100)), Txflags(tfPassive));
                env(offer(bob, usd(100), btc(100)), Txflags(tfPassive));
                env(offer(bob, btc(100), eth(100)), Txflags(tfPassive));
                env.close();

                test(env, btc, eth, usd, cad);
            };

            // USD starts without MPTCanTransfer.
            testMultiStepMPTCanTransfer(
                NoTransferMPT::USD,
                [&](Env& env,
                    MPTTester const& btc,
                    MPTTester const& eth,
                    MPTTester const& usd,
                    MPTTester const& cad) {
                    BEAST_EXPECT(expectOffers(env, bob, 3));

                    // fail - CAD/USD is owned by bob
                    env(pay(alice, carol, eth(1)),
                        Path(~usd, ~btc, ~eth),
                        Sendmax(cad(1)),
                        Ter(tecPATH_PARTIAL));

                    auto seq(env.seq(gw));
                    env(offer(gw, usd(1), btc(1)), Txflags(tfPassive));
                    env.close();
                    // fail - CAD/USD is owned by bob
                    env(pay(alice, carol, eth(1)),
                        Path(~usd, ~btc, ~eth),
                        Sendmax(cad(1)),
                        Ter(tecPATH_PARTIAL));
                    env.close();
                    env(offerCancel(gw, seq));
                    env(offer(gw, cad(1), usd(1)), Txflags(tfPassive));
                    env.close();
                    BEAST_EXPECT(expectOffers(env, bob, 3));
                    // succeed - CAD/USD is owned by issuer
                    env(pay(alice, carol, eth(1)), Path(~usd, ~btc, ~eth), Sendmax(cad(1)));
                    env.close();
                    // bob's CAD/USD is deleted.
                    BEAST_EXPECT(expectOffers(env, bob, 2));
                    env(offer(bob, cad(100), usd(100)), Txflags(tfPassive));
                    BEAST_EXPECT(expectOffers(env, gw, 0));
                });

            // ETH starts without MPTCanTransfer.
            testMultiStepMPTCanTransfer(
                NoTransferMPT::ETH,
                [&](Env& env,
                    MPTTester const& btc,
                    MPTTester const& eth,
                    MPTTester const& usd,
                    MPTTester const& cad) {
                    // fail - BTC/ETH is owned by bob, destination is carol
                    env(pay(alice, carol, eth(1)),
                        Path(~usd, ~btc, ~eth),
                        Sendmax(cad(1)),
                        Ter(tecPATH_PARTIAL));
                    env.close();
                    BEAST_EXPECT(expectOffers(env, bob, 3));

                    // succeed - destination is an issuer
                    env(pay(alice, gw, eth(1)), Path(~usd, ~btc, ~eth), Sendmax(cad(1)));
                    env.close();
                    BEAST_EXPECT(expectOffers(env, bob, 3));
                });

            // CAD starts without MPTCanTransfer.
            testMultiStepMPTCanTransfer(
                NoTransferMPT::CAD,
                [&](Env& env,
                    MPTTester const& btc,
                    MPTTester const& eth,
                    MPTTester const& usd,
                    MPTTester const& cad) {
                    // fail - CAD/USD is owned by bob, source is alice
                    env(pay(alice, carol, eth(1)),
                        Path(~usd, ~btc, ~eth),
                        Sendmax(cad(1)),
                        Ter(tecPATH_PARTIAL));
                    // succeed - source is the issuer
                    env(pay(gw, carol, eth(1)), Path(~usd, ~btc, ~eth), Sendmax(cad(1)));
                    env.close();
                    env(offer(gw, cad(1), usd(1)), Txflags(tfPassive));
                    env.close();
                    // succeed - CAD/USD is owned by issuer
                    env(pay(alice, carol, eth(1)), Path(~usd, ~btc, ~eth), Sendmax(cad(1)));
                    env.close();
                    BEAST_EXPECT(expectOffers(env, gw, 0));
                    BEAST_EXPECT(expectOffers(env, bob, 2));
                });

            // BTC starts without MPTCanTransfer.
            testMultiStepMPTCanTransfer(
                NoTransferMPT::BTC,
                [&](Env& env,
                    MPTTester const& btc,
                    MPTTester const& eth,
                    MPTTester const& usd,
                    MPTTester const& cad) {
                    env(offer(gw, usd(1), btc(1)), Txflags(tfPassive));
                    env.close();
                    // succeed - USD/BTC is owned by issuer
                    env(pay(alice, carol, eth(1)), Path(~usd, ~btc, ~eth), Sendmax(cad(1)));
                    env.close();
                    BEAST_EXPECT(expectOffers(env, gw, 0));
                });
        }

        // MPTCanTrade is not set
        {
            Env env{*this, features};
            env.fund(XRP(1'000), gw, alice, carol, bob);
            env.close();
            MPTTester btc(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol, bob},
                 .pay = 1'000,
                 .flags = tfMPTCanTransfer,
                 .mutableFlags = tmfMPTCanEnableCanTrade});
            MPTTester const eth(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol, bob},
                 .pay = 1'000,
                 .flags = kMptDexFlags});
            MPTTester const usd(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol, bob},
                 .pay = 1'000,
                 .flags = kMptDexFlags});

            env(pay(alice, carol, eth(1)), Path(~eth), Sendmax(btc(1)), Ter(tecNO_PERMISSION));
            env(pay(alice, carol, btc(1)), Path(~btc), Sendmax(eth(1)), Ter(tecNO_PERMISSION));
            env.close();

            // Enable MPTCanTrade so BTC can be crossed through offers.
            btc.set({.mutableFlags = tmfMPTSetCanTrade});
            env(offer(bob, XRP(1), btc(1)));
            env(offer(bob, btc(1), eth(1)));
            env(offer(bob, eth(1), usd(1)));
            env.close();
            BEAST_EXPECT(expectOffers(env, bob, 3));

            env(pay(gw, carol, usd(1)),
                Path(~btc, ~eth, ~usd),
                Sendmax(XRP(1)),
                Txflags(tfPartialPayment | tfNoRippleDirect));
            env.close();
            BEAST_EXPECT(expectOffers(env, bob, 0));
        }

        // Holders are locked
        {
            enum class LockType { Global, Individual, None };
            struct TestArg
            {
                Account src;
                Account dst;
                Account offerOwner;
                LockType srcFlag = LockType::None;
                LockType dstFlag = LockType::None;
                LockType offerFlagBuy = LockType::None;
                LockType offerFlagSell = LockType::None;
                LockType globalFlagBuy = LockType::None;
                LockType globalFlagSell = LockType::None;
                TER err = tesSUCCESS;
                std::optional<TER> errIOU = std::nullopt;
            };
            auto getErr = [&]<typename Token>(Token const&, TestArg const& arg) {
                if constexpr (std::is_same_v<Token, IOU>)
                {
                    return arg.errIOU.value_or(arg.err);
                }
                else if constexpr (std::is_same_v<Token, MPTTester>)
                {
                    return arg.err;
                }
            };
            auto getMPT = [&](Env& env) {
                MPTTester const btc(
                    {.env = env,
                     .issuer = gw2,
                     .holders = {alice, carol, bob, gw},
                     .pay = 100,
                     .flags = tfMPTCanLock | kMptDexFlags});
                MPTTester const eth(
                    {.env = env,
                     .issuer = gw,
                     .holders = {alice, carol, bob, gw2},
                     .pay = 100,
                     .flags = tfMPTCanLock | kMptDexFlags});
                return std::make_pair(btc, eth);
            };
            auto getIOU = [&](Env& env) {
                for (auto const& a : {alice, carol, bob})
                {
                    env(fset(a, asfDefaultRipple));
                    env.close();
                    env(trust(a, gw["ETH"](200)));
                    env(pay(gw, a, gw["ETH"](100)));
                    env(trust(a, gw2["BTC"](200)));
                    env(pay(gw2, a, gw2["BTC"](100)));
                    env.close();
                }
                // gw2 needs an ETH trust line to receive ETH when its offers fill
                // gw needs BTC to sell BTC
                env(trust(gw2, gw["ETH"](200)));
                env(trust(gw, gw2["BTC"](200)));
                env(pay(gw2, gw, gw2["BTC"](100)));
                env.close();
                return std::make_pair(gw2["BTC"], gw["ETH"]);
            };
            auto lock = [&]<typename Token>(
                            Env& env, Account const& account, Token& token, LockType lock) {
                if (lock == LockType::None)
                    return;
                if constexpr (std::is_same_v<Token, IOU>)
                {
                    if (lock == LockType::Global)
                    {
                        env(fset(token.account, asfGlobalFreeze));
                    }
                    else
                    {
                        IOU const iou{account, token.currency};
                        env(trust(token.account, iou(0), tfSetFreeze));
                    }
                }
                else if constexpr (std::is_same_v<Token, MPTTester>)
                {
                    if (lock == LockType::Global)
                    {
                        token.set({.flags = tfMPTLock});
                    }
                    else if (token.issuer() != account)
                    {
                        token.set({.holder = account, .flags = tfMPTLock});
                    }
                }
            };
            auto test = [&](auto&& getTokens, TestArg const& arg) {
                Env env(*this);
                env.fund(XRP(1'000), gw, gw2, alice, carol, bob);

                auto [btc, eth] = getTokens(env);

                env(offer(arg.offerOwner, eth(10), btc(10)), Txflags(tfPassive));
                env.close();

                if (arg.globalFlagBuy != LockType::None)
                {
                    lock(env, gw, eth, LockType::Global);
                }
                else
                {
                    lock(env, arg.offerOwner, eth, arg.offerFlagBuy);
                    lock(env, arg.src, eth, arg.srcFlag);
                }
                if (arg.globalFlagSell != LockType::None)
                {
                    lock(env, gw2, btc, LockType::Global);
                }
                else
                {
                    lock(env, arg.offerOwner, btc, arg.offerFlagSell);
                    lock(env, arg.dst, btc, arg.dstFlag);
                }

                auto const err = getErr(eth, arg);
                env(pay(arg.src, arg.dst, btc(1)),
                    Path(~btc),
                    Txflags(tfNoRippleDirect),
                    Sendmax(eth(1)),
                    Ter(err));
                env.close();
            };
            // clang-format off
            std::vector<TestArg> const tests = {
                // ----- src=alice (holder), dst=carol (holder), offerOwner=bob (holder) -----
                // alice's ETH locked: caught in check()
                {.src = alice, .dst = carol, .offerOwner = bob, .srcFlag = LockType::Individual, .err = tecPATH_DRY},
                // carol's BTC locked: caught in MPT check(); IOU dst can still receive when frozen
                {.src = alice, .dst = carol, .offerOwner = bob, .dstFlag = LockType::Individual, .err = tecPATH_DRY, .errIOU = tesSUCCESS},
                // ETH globally locked: caught in check()
                {.src = alice, .dst = carol, .offerOwner = bob, .globalFlagBuy = LockType::Global, .err = tecPATH_DRY},
                // BTC globally locked: bob's offer unfunded in OfferStream
                {.src = alice, .dst = carol, .offerOwner = bob, .globalFlagSell = LockType::Global, .err = tecPATH_PARTIAL},
                // bob's ETH (takerPays) locked: MPT offer unfunded in OfferStream (locked holder cannot receive); IOU can still receive
                {.src = alice, .dst = carol, .offerOwner = bob, .offerFlagBuy = LockType::Individual, .err = tecPATH_PARTIAL, .errIOU = tesSUCCESS},
                // bob's BTC (takerGets) locked: offer unfunded in OfferStream
                {.src = alice, .dst = carol, .offerOwner = bob, .offerFlagSell = LockType::Individual, .err = tecPATH_PARTIAL},
                // ----- src=alice (holder), dst=carol (holder), offerOwner=gw2 (BTC issuer) -----
                // alice's ETH locked: caught in check()
                {.src = alice, .dst = carol, .offerOwner = gw2, .srcFlag = LockType::Individual, .err = tecPATH_DRY},
                // carol's BTC locked: caught in MPT check(); IOU dst can still receive when frozen
                {.src = alice, .dst = carol, .offerOwner = gw2, .dstFlag = LockType::Individual, .err = tecPATH_DRY, .errIOU = tesSUCCESS},
                // ETH globally locked: caught in check()
                {.src = alice, .dst = carol, .offerOwner = gw2, .globalFlagBuy = LockType::Global, .err = tecPATH_DRY},
                // BTC globally locked: gw2 is the BTC issuer, offer always permitted regardless of global freeze
                {.src = alice, .dst = carol, .offerOwner = gw2, .globalFlagSell = LockType::Global, .err = tesSUCCESS},
                // ----- src=alice (holder), dst=carol (holder), offerOwner=gw (ETH issuer, BTC holder) -----
                // alice's ETH locked: caught in check()
                {.src = alice, .dst = carol, .offerOwner = gw, .srcFlag = LockType::Individual, .err = tecPATH_DRY},
                // carol's BTC locked: caught in MPT check(); IOU dst can still receive when frozen
                {.src = alice, .dst = carol, .offerOwner = gw, .dstFlag = LockType::Individual, .err = tecPATH_DRY, .errIOU = tesSUCCESS},
                // ETH globally locked: caught in check()
                {.src = alice, .dst = carol, .offerOwner = gw, .globalFlagBuy = LockType::Global, .err = tecPATH_DRY},
                // BTC globally locked: gw holds BTC as a holder (not BTC issuer), offer unfunded in OfferStream
                {.src = alice, .dst = carol, .offerOwner = gw, .globalFlagSell = LockType::Global, .err = tecPATH_PARTIAL},
                // ----- src=gw (ETH issuer), dst=carol (holder), offerOwner=bob (holder) -----
                // ETH globally locked, src is ETH issuer: no first MPTEndpointStep so check() passes;
                // MPT offer unfunded in OfferStream (globally-locked ETH cannot flow to holder via DEX); IOU issuer can still issue
                {.src = gw, .dst = carol, .offerOwner = bob, .srcFlag = LockType::Global, .err = tecPATH_PARTIAL, .errIOU = tesSUCCESS},
                // BTC globally locked: last MPTEndpointStep only checks individual freeze, check() passes; offer unfunded in OfferStream
                {.src = gw, .dst = carol, .offerOwner = bob, .dstFlag = LockType::Global, .err = tecPATH_PARTIAL},
                // carol's BTC locked: caught in MPT check(); IOU dst can still receive when frozen
                {.src = gw, .dst = carol, .offerOwner = bob, .dstFlag = LockType::Individual, .err = tecPATH_DRY, .errIOU = tesSUCCESS},
                // bob's ETH (takerPays) locked: MPT offer unfunded in OfferStream (locked holder cannot receive); IOU can still receive
                {.src = gw, .dst = carol, .offerOwner = bob, .offerFlagBuy = LockType::Individual, .err = tecPATH_PARTIAL, .errIOU = tesSUCCESS},
                // bob's BTC (takerGets) locked: offer unfunded in OfferStream
                {.src = gw, .dst = carol, .offerOwner = bob, .offerFlagSell = LockType::Individual, .err = tecPATH_PARTIAL},
                // ----- src=alice (holder), dst=gw2 (BTC issuer), offerOwner=bob (holder) -----
                // alice's ETH locked: caught in check()
                {.src = alice, .dst = gw2, .offerOwner = bob, .srcFlag = LockType::Individual, .err = tecPATH_DRY},
                // BTC globally locked, dst is BTC issuer: no last MPTEndpointStep so check() passes; offer unfunded in OfferStream
                {.src = alice, .dst = gw2, .offerOwner = bob, .dstFlag = LockType::Global, .err = tecPATH_PARTIAL},
                // bob's ETH (takerPays) locked: MPT offer unfunded in OfferStream (locked holder cannot receive); IOU can still receive
                {.src = alice, .dst = gw2, .offerOwner = bob, .offerFlagBuy = LockType::Individual, .err = tecPATH_PARTIAL, .errIOU = tesSUCCESS},
                // bob's BTC (takerGets) locked: offer unfunded in OfferStream
                {.src = alice, .dst = gw2, .offerOwner = bob, .offerFlagSell = LockType::Individual, .err = tecPATH_PARTIAL},
            };
            // clang-format on

            for (auto const& t : tests)
            {
                test(getMPT, t);
                test(getIOU, t);
            }
        }
        {
            Env env(*this);
            auto const usd = gw["USD"];
            env.fund(XRP(1'000), gw, alice, carol, bob);
            MPTTester btc(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol, bob},
                 .pay = 100,
                 .flags = tfMPTCanLock | kMptDexFlags});
            MPTTester eth(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol, bob},
                 .pay = 100,
                 .flags = tfMPTCanLock | kMptDexFlags});

            env(trust(alice, usd(100)));
            env(pay(gw, alice, usd(100)));
            env(trust(carol, usd(100)));

            env(offer(alice, XRP(10), eth(10)));
            env(offer(bob, eth(10), btc(10)));
            env(offer(alice, btc(10), usd(10)));
            env.close();

            btc.set({.holder = bob, .flags = tfMPTLock});

            // Bob's offer is unfunded
            env(pay(alice, carol, usd(1)),
                Path(~(MPT)eth, ~(MPT)btc, ~usd),
                Txflags(tfNoRippleDirect | tfPartialPayment),
                Sendmax(XRP(1)),
                Ter(tecPATH_DRY));
            env.close();

            btc.set({.holder = bob, .flags = tfMPTUnlock});
            eth.set({.holder = bob, .flags = tfMPTLock});

            env(pay(alice, carol, usd(1)),
                Path(~(MPT)eth, ~(MPT)btc, ~usd),
                Txflags(tfNoRippleDirect | tfPartialPayment),
                Sendmax(XRP(1)),
                Ter(tecPATH_DRY));
        }

        // A domain payment should only consume a USD/MPT offer with a domain.
        // It must not consume a regular USD/MPT offer.
        {
            Env env(*this, features);
            Account const domainOwner("DomainOwner");
            env.fund(XRP(1'000), gw, alice, carol, bob);
            auto const domainID =
                setupDomain(env, {alice, bob, carol, gw}, domainOwner, "permdex-cred");

            MPTTester btc({.env = env, .issuer = gw, .holders = {alice, carol, bob}, .pay = 100});
            MPTTester eth({.env = env, .issuer = gw, .holders = {alice, carol, bob}, .pay = 100});

            auto test = [&](bool withDomain) {
                if (withDomain)
                {
                    env(offer(bob, eth(1), btc(1)), Domain(domainID));
                }
                else
                {
                    env(offer(bob, eth(1), btc(1)));
                }

                auto const err = withDomain ? Ter(tesSUCCESS) : Ter(tecPATH_DRY);
                env(pay(alice, carol, btc(1)),
                    Path(~(MPT)btc),
                    Txflags(tfPartialPayment),
                    Sendmax(eth(1)),
                    Domain(domainID),
                    err);
            };
            test(true);
            test(false);
        }

        // A hybrid USD/MPT domain offer should still be consumable by
        // a regular payment.
        {
            Env env(*this, features);
            Account const domainOwner("DomainOwner");
            env.fund(XRP(1'000), gw, alice, carol, bob);
            auto const domainID =
                setupDomain(env, {alice, bob, carol, gw}, domainOwner, "permdex-cred");

            MPTTester btc({.env = env, .issuer = gw, .holders = {alice, carol, bob}, .pay = 100});
            MPTTester eth({.env = env, .issuer = gw, .holders = {alice, carol, bob}, .pay = 100});

            auto test = [&](bool isHybrid) {
                auto const flags = isHybrid ? tfHybrid : 0;
                env(offer(bob, eth(1), btc(1)), Txflags(flags), Domain(domainID));

                auto const err = isHybrid ? Ter(tesSUCCESS) : Ter(tecPATH_DRY);
                env(pay(alice, carol, btc(1)),
                    Path(~(MPT)btc),
                    Txflags(tfPartialPayment),
                    Sendmax(eth(1)),
                    err);
            };
            test(true);
            test(false);
        }

        // MPT/XRP
        {
            Env env{*this, features};
            MPTTester mptTester(env, gw, {.holders = {alice, carol, bob}});

            mptTester.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt = mptTester["MPT"];

            mptTester.authorize({.account = alice});
            mptTester.pay(gw, alice, 200);

            mptTester.authorize({.account = carol});
            mptTester.pay(gw, carol, 200);

            mptTester.authorize({.account = bob});

            env(offer(alice, XRP(100), mpt(101)));
            env.close();
            BEAST_EXPECT(expectOffers(env, alice, 1, {{Amounts{XRP(100), mpt(101)}}}));

            env(pay(carol, bob, mpt(101)),
                test::jtx::Path(~mpt),
                Sendmax(XRP(100)),
                Txflags(tfPartialPayment));
            env.close();

            BEAST_EXPECT(expectOffers(env, alice, 0));
            BEAST_EXPECT(mptTester.checkMPTokenOutstandingAmount(400));
            BEAST_EXPECT(mptTester.checkMPTokenAmount(alice, 99));
            BEAST_EXPECT(mptTester.checkMPTokenAmount(bob, 101));
        }

        // MPT/IOU
        {
            Env env{*this, features};

            MPTTester mptTester(env, gw, {.holders = {alice, carol, bob}});

            mptTester.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt = mptTester["MPT"];

            env(trust(alice, usd(2'000)));
            env(pay(gw, alice, usd(1'000)));
            env(trust(bob, usd(2'000)));
            env(pay(gw, bob, usd(1'000)));
            env(trust(carol, usd(2'000)));
            env(pay(gw, carol, usd(1'000)));
            env.close();

            mptTester.authorize({.account = alice});
            mptTester.pay(gw, alice, 200);

            mptTester.authorize({.account = carol});
            mptTester.pay(gw, carol, 200);

            mptTester.authorize({.account = bob});

            env(offer(alice, usd(100), mpt(101)));
            env.close();
            BEAST_EXPECT(expectOffers(env, alice, 1, {{Amounts{usd(100), mpt(101)}}}));

            env(pay(carol, bob, mpt(101)),
                test::jtx::Path(~mpt),
                Sendmax(usd(100)),
                Txflags(tfPartialPayment));
            env.close();

            BEAST_EXPECT(expectOffers(env, alice, 0));
            BEAST_EXPECT(env.balance(carol, usd) == usd(900));
            BEAST_EXPECT(mptTester.checkMPTokenOutstandingAmount(400));
            BEAST_EXPECT(mptTester.checkMPTokenAmount(alice, 99));
            BEAST_EXPECT(mptTester.checkMPTokenAmount(bob, 101));
        }

        // IOU/MPT
        {
            Env env{*this, features};

            MPTTester mptTester(env, gw, {.holders = {alice, carol, bob}});

            mptTester.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt = mptTester["MPT"];

            env(trust(alice, usd(2'000)), Txflags(tfClearNoRipple));
            env(pay(gw, alice, usd(1'000)));
            env(trust(bob, usd(2'000)), Txflags(tfClearNoRipple));
            env.close();

            mptTester.authorize({.account = alice});
            env(pay(gw, alice, mpt(200)));

            mptTester.authorize({.account = carol});
            env(pay(gw, carol, mpt(200)));

            env(offer(alice, mpt(101), usd(100)));
            env.close();
            BEAST_EXPECT(expectOffers(env, alice, 1, {{Amounts{mpt(101), usd(100)}}}));

            env(pay(carol, bob, usd(100)),
                test::jtx::Path(~usd),
                Sendmax(mpt(101)),
                Txflags(tfPartialPayment | tfNoRippleDirect));
            env.close();

            BEAST_EXPECT(expectOffers(env, alice, 0));
            BEAST_EXPECT(env.balance(alice, usd) == usd(900));
            BEAST_EXPECT(mptTester.checkMPTokenAmount(alice, 301));
            BEAST_EXPECT(mptTester.checkMPTokenOutstandingAmount(400));
            BEAST_EXPECT(mptTester.checkMPTokenAmount(carol, 99));
            BEAST_EXPECT(env.balance(bob, usd) == usd(100));
        }

        // MPT/MPT
        {
            Env env{*this, features};

            MPTTester mptTester1(env, gw, {.holders = {alice, carol, bob}});
            mptTester1.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt1 = mptTester1["MPT1"];

            MPTTester mptTester2(env, gw, {.holders = {alice, carol, bob}, .fund = false});
            mptTester2.create(
                {.ownerCount = 2, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt2 = mptTester2["MPT2"];

            mptTester1.authorize({.account = alice});
            mptTester1.pay(gw, alice, 200);
            mptTester2.authorize({.account = alice});

            mptTester2.authorize({.account = carol});
            mptTester2.pay(gw, carol, 200);

            mptTester1.authorize({.account = bob});
            mptTester2.authorize({.account = bob});
            mptTester2.pay(gw, bob, 200);

            env(offer(alice, mpt2(100), mpt1(100)));
            env.close();
            BEAST_EXPECT(
                expectOffers(env, alice, 1, {{Amounts{mptTester2(100), mptTester1(100)}}}));

            // holder to holder
            env(pay(carol, bob, mpt1(10)),
                test::jtx::Path(~mpt1),
                Sendmax(mpt2(10)),
                Txflags(tfPartialPayment));
            env.close();

            BEAST_EXPECT(expectOffers(env, alice, 1));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(alice, 190));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(alice, 10));
            BEAST_EXPECT(mptTester1.checkMPTokenOutstandingAmount(200));
            BEAST_EXPECT(mptTester2.checkMPTokenOutstandingAmount(400));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(carol, 190));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(bob, 10));

            // issuer to holder
            env(pay(gw, bob, mpt1(20)),
                test::jtx::Path(~mpt1),
                Sendmax(mpt2(20)),
                Txflags(tfPartialPayment));
            env.close();

            BEAST_EXPECT(expectOffers(env, alice, 1));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(alice, 170));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(alice, 30));
            BEAST_EXPECT(mptTester1.checkMPTokenOutstandingAmount(200));
            BEAST_EXPECT(mptTester2.checkMPTokenOutstandingAmount(420));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(carol, 190));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(bob, 30));

            // holder to issuer
            env(pay(bob, gw, mpt1(70)),
                test::jtx::Path(~mpt1),
                Sendmax(mpt2(70)),
                Txflags(tfPartialPayment));
            env.close();

            BEAST_EXPECT(expectOffers(env, alice, 0));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(alice, 100));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(alice, 100));
            BEAST_EXPECT(mptTester1.checkMPTokenOutstandingAmount(130));
            BEAST_EXPECT(mptTester2.checkMPTokenOutstandingAmount(420));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(carol, 190));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(bob, 30));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(bob, 130));
        }

        // MPT/MPT, issuer owns the offer
        {
            Env env{*this, features};

            MPTTester mptTester1(env, gw, {.holders = {carol, bob}});
            mptTester1.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt1 = mptTester1["MPT1"];

            MPTTester mptTester2(env, gw, {.holders = {carol, bob}, .fund = false});
            mptTester2.create(
                {.ownerCount = 2, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt2 = mptTester2["MPT2"];

            mptTester2.authorize({.account = carol});
            mptTester2.pay(gw, carol, 200);

            mptTester1.authorize({.account = bob});
            mptTester2.authorize({.account = bob});
            mptTester2.pay(gw, bob, 200);

            env(offer(gw, mpt2(100), mpt1(100)));
            env.close();
            BEAST_EXPECT(expectOffers(env, gw, 1, {{Amounts{mpt2(100), mpt1(100)}}}));

            // holder to holder
            env(pay(carol, bob, mpt1(10)),
                test::jtx::Path(~mpt1),
                Sendmax(mpt2(10)),
                Txflags(tfPartialPayment));
            env.close();

            BEAST_EXPECT(expectOffers(env, gw, 1));
            BEAST_EXPECT(mptTester1.checkMPTokenOutstandingAmount(10));
            BEAST_EXPECT(mptTester2.checkMPTokenOutstandingAmount(390));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(carol, 190));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(bob, 10));

            // issuer to holder
            env(pay(gw, bob, mpt1(20)),
                test::jtx::Path(~mpt1),
                Sendmax(mpt2(20)),
                Txflags(tfPartialPayment));
            env.close();

            BEAST_EXPECT(expectOffers(env, gw, 1));
            BEAST_EXPECT(mptTester1.checkMPTokenOutstandingAmount(30));
            BEAST_EXPECT(mptTester2.checkMPTokenOutstandingAmount(390));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(carol, 190));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(bob, 30));

            // holder to issuer
            env(pay(bob, gw, mpt1(70)),
                test::jtx::Path(~mpt1),
                Sendmax(mpt2(70)),
                Txflags(tfPartialPayment));
            env.close();

            BEAST_EXPECT(expectOffers(env, gw, 0));
            BEAST_EXPECT(mptTester1.checkMPTokenOutstandingAmount(30));
            BEAST_EXPECT(mptTester2.checkMPTokenOutstandingAmount(320));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(carol, 190));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(bob, 30));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(bob, 130));
        }

        // MPT/MPT, different issuer
        {
            Env env{*this, features};
            Account const gw1{"gw1"};

            MPTTester mptTester1(env, gw, {.holders = {alice, carol, bob}});
            mptTester1.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt1 = mptTester1["MPT1"];

            env.fund(XRP(1'000), gw1);
            MPTTester mptTester2(env, gw1, {.holders = {alice, carol, bob}, .fund = false});
            mptTester2.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt2 = mptTester2["MPT2"];

            mptTester1.authorize({.account = alice});
            mptTester1.pay(gw, alice, 200);
            mptTester2.authorize({.account = alice});

            mptTester2.authorize({.account = carol});
            mptTester2.pay(gw1, carol, 200);

            mptTester1.authorize({.account = bob});
            mptTester1.pay(gw, bob, 200);
            mptTester2.authorize({.account = bob});
            mptTester2.pay(gw1, bob, 200);

            mptTester1.authorize({.account = gw1});
            mptTester1.pay(gw, gw1, 200);

            mptTester2.authorize({.account = gw});
            mptTester2.pay(gw1, gw, 200);

            env(offer(alice, mpt2(100), mpt1(100)));
            env.close();
            BEAST_EXPECT(expectOffers(env, alice, 1, {{Amounts{mpt2(100), mpt1(100)}}}));

            env(pay(carol, bob, mpt1(10)),
                test::jtx::Path(~mpt1),
                Sendmax(mpt2(10)),
                Txflags(tfPartialPayment));
            env.close();
            BEAST_EXPECT(expectOffers(env, alice, 1));
            BEAST_EXPECT(mptTester1.checkMPTokenOutstandingAmount(600));
            BEAST_EXPECT(mptTester2.checkMPTokenOutstandingAmount(600));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(gw1, 200));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(gw, 200));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(carol, 190));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(bob, 210));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(bob, 200));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(alice, 190));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(alice, 10));

            env(pay(bob, gw, mpt1(10)),
                test::jtx::Path(~mpt1),
                Sendmax(mpt2(10)),
                Txflags(tfPartialPayment));
            env.close();
            BEAST_EXPECT(expectOffers(env, alice, 1));
            BEAST_EXPECT(mptTester1.checkMPTokenOutstandingAmount(590));
            BEAST_EXPECT(mptTester2.checkMPTokenOutstandingAmount(600));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(gw1, 200));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(gw, 200));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(bob, 210));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(bob, 190));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(alice, 180));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(alice, 20));

            env(pay(gw, bob, mpt1(10)),
                test::jtx::Path(~mpt1),
                Sendmax(mpt2(10)),
                Txflags(tfPartialPayment));
            env.close();
            BEAST_EXPECT(expectOffers(env, alice, 1));
            BEAST_EXPECT(mptTester1.checkMPTokenOutstandingAmount(590));
            BEAST_EXPECT(mptTester2.checkMPTokenOutstandingAmount(600));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(gw1, 200));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(gw, 190));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(bob, 220));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(bob, 190));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(alice, 170));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(alice, 30));

            env(pay(bob, gw1, mpt1(10)),
                test::jtx::Path(~mpt1),
                Sendmax(mpt2(10)),
                Txflags(tfPartialPayment));
            env.close();
            BEAST_EXPECT(expectOffers(env, alice, 1));
            BEAST_EXPECT(mptTester1.checkMPTokenOutstandingAmount(590));
            BEAST_EXPECT(mptTester2.checkMPTokenOutstandingAmount(600));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(gw1, 210));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(gw, 190));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(bob, 220));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(bob, 180));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(alice, 160));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(alice, 40));

            env(pay(gw1, bob, mpt1(10)),
                test::jtx::Path(~mpt1),
                Sendmax(mpt2(10)),
                Txflags(tfPartialPayment));
            env.close();
            BEAST_EXPECT(expectOffers(env, alice, 1));
            BEAST_EXPECT(mptTester1.checkMPTokenOutstandingAmount(590));
            BEAST_EXPECT(mptTester2.checkMPTokenOutstandingAmount(610));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(gw1, 210));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(gw, 190));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(bob, 230));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(bob, 180));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(alice, 150));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(alice, 50));

            env(pay(gw, gw1, mpt1(10)),
                test::jtx::Path(~mpt1),
                Sendmax(mpt2(10)),
                Txflags(tfPartialPayment));
            env.close();
            BEAST_EXPECT(expectOffers(env, alice, 1));
            BEAST_EXPECT(mptTester1.checkMPTokenOutstandingAmount(590));
            BEAST_EXPECT(mptTester2.checkMPTokenOutstandingAmount(610));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(gw1, 220));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(gw, 180));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(alice, 140));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(alice, 60));

            env(pay(gw1, gw, mpt1(40)),
                test::jtx::Path(~mpt1),
                Sendmax(mpt2(40)),
                Txflags(tfPartialPayment));
            env.close();
            BEAST_EXPECT(expectOffers(env, alice, 0));
            BEAST_EXPECT(mptTester1.checkMPTokenOutstandingAmount(550));
            BEAST_EXPECT(mptTester2.checkMPTokenOutstandingAmount(650));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(gw1, 220));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(gw, 180));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(alice, 100));
            BEAST_EXPECT(mptTester2.checkMPTokenAmount(alice, 100));
        }

        // MPT/IOU IOU/mpt1
        {
            Env env = pathTestEnv(*this);
            Account const gw1{"gw1"};
            Account const gw2{"gw2"};
            Account const dan{"dan"};
            env.fund(XRP(1'000), gw2);
            auto const usd = gw2["USD"];

            MPTTester mptTester(env, gw, {.holders = {alice, carol}});
            mptTester.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt = mptTester["MPT"];
            mptTester.authorize({.account = alice});
            mptTester.authorize({.account = carol});
            mptTester.pay(gw, carol, 200);

            MPTTester mptTester1(env, gw1, {.holders = {bob, dan}});
            mptTester1.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt1 = mptTester1["MPT1"];
            mptTester1.authorize({.account = bob});
            mptTester1.pay(gw1, bob, 200);
            mptTester1.authorize({.account = dan});

            env(trust(alice, usd(400)));
            env(pay(gw2, alice, usd(200)));
            env(trust(bob, usd(400)));

            env(offer(alice, mpt(100), usd(100)));
            env(offer(bob, usd(100), mpt1(100)));
            env.close();

            env(pay(carol, dan, mpt1(100)),
                Sendmax(mpt(100)),
                Path(~usd, ~mpt1),
                Txflags(tfPartialPayment | tfNoRippleDirect));
            env.close();
            BEAST_EXPECT(expectOffers(env, alice, 0));
            BEAST_EXPECT(expectOffers(env, bob, 0));
            BEAST_EXPECT(mptTester.checkMPTokenAmount(carol, 100));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(dan, 100));
        }

        // XRP/MPT AMM
        {
            Env env{*this, features};

            fund(env, gw, {alice, carol, bob}, XRP(11'000), {usd(20'000)});

            MPTTester mptTester(env, gw, {.fund = false});

            mptTester.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt = mptTester["MPT"];

            mptTester.authorize({.account = alice});
            mptTester.authorize({.account = bob});
            mptTester.pay(gw, alice, 10'100);

            AMM const amm(env, alice, XRP(10'000), mpt(10'100));

            env(pay(carol, bob, mpt(100)),
                test::jtx::Path(~mpt),
                Sendmax(XRP(100)),
                Txflags(tfPartialPayment | tfNoRippleDirect));
            env.close();

            BEAST_EXPECT(amm.expectBalances(XRP(10'100), mpt(10'000), amm.tokens()));
            BEAST_EXPECT(mptTester.checkMPTokenAmount(bob, 100));
        }

        // IOU/MPT AMM
        {
            Env env{*this, features};

            fund(env, gw, {alice, carol, bob}, XRP(11'000), {usd(20'000)});

            MPTTester mptTester(env, gw, {.fund = false});

            mptTester.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt = mptTester["MPT"];

            mptTester.authorize({.account = alice});
            mptTester.authorize({.account = bob});
            mptTester.pay(gw, alice, 10'100);

            AMM const amm(env, alice, usd(10'000), mpt(10'100));

            env(pay(carol, bob, mpt(100)),
                test::jtx::Path(~mpt),
                Sendmax(usd(100)),
                Txflags(tfPartialPayment | tfNoRippleDirect));
            env.close();

            BEAST_EXPECT(amm.expectBalances(usd(10'100), mpt(10'000), amm.tokens()));
            BEAST_EXPECT(mptTester.checkMPTokenAmount(bob, 100));
        }

        // MPT/MPT AMM cross-asset payment
        {
            Env env{*this, features};
            env.fund(XRP(20'000), gw, alice, carol, bob);
            env.close();

            MPTTester mptTester1(env, gw, {.fund = false});
            mptTester1.create({.flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt1 = mptTester1["MPT1"];
            mptTester1.authorize({.account = alice});
            mptTester1.authorize({.account = bob});
            mptTester1.pay(gw, alice, 10'100);

            MPTTester mptTester2(env, gw, {.fund = false});
            mptTester2.create({.flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt2 = mptTester2["MPT1"];
            mptTester2.authorize({.account = alice});
            mptTester2.authorize({.account = bob});
            mptTester2.authorize({.account = carol});
            mptTester2.pay(gw, alice, 10'100);
            mptTester2.pay(gw, carol, 100);

            AMM const amm(env, alice, mpt2(10'000), mpt1(10'100));

            env(pay(carol, bob, mpt1(100)),
                test::jtx::Path(~mpt1),
                Sendmax(mpt2(100)),
                Txflags(tfPartialPayment | tfNoRippleDirect));
            env.close();

            BEAST_EXPECT(amm.expectBalances(mpt2(10'100), mpt1(10'000), amm.tokens()));
            BEAST_EXPECT(mptTester1.checkMPTokenAmount(bob, 100));
        }

        // Multi-steps with AMM
        // EUR/MPT1 MPT1/MPT2 MPT2/USD USD/CRN AMM:CRN/MPT MPT/YAN
        {
            Env env{*this, features};
            auto const usd = gw["USD"];
            auto const eur = gw["EUR"];
            auto const crn = gw["CRN"];
            auto const yan = gw["YAN"];

            fund(
                env,
                gw,
                {alice, carol, bob},
                XRP(1'000),
                {usd(1'000), eur(1'000), crn(2'000), yan(1'000)});

            auto createMPT = [&]() -> std::pair<MPTTester, MPT> {
                MPTTester mptTester(env, gw, {.fund = false});
                mptTester.create({.flags = tfMPTCanTransfer | tfMPTCanTrade});
                mptTester.authorize({.account = alice});
                mptTester.pay(gw, alice, 2'000);
                return {mptTester, mptTester["MPT"]};
            };

            auto const [mptTester1, mpt1] = createMPT();
            auto const [mptTester2, mpt2] = createMPT();
            auto const [mptTester3, mpt3] = createMPT();

            env(offer(alice, eur(100), mpt1(101)));
            env(offer(alice, mpt1(101), mpt2(102)));
            env(offer(alice, mpt2(102), usd(103)));
            env(offer(alice, usd(103), crn(104)));
            env.close();
            AMM const amm(env, alice, crn(1'000), mpt3(1'104));
            env(offer(alice, mpt3(104), yan(100)));

            env(pay(carol, bob, yan(100)),
                test::jtx::Path(~mpt1, ~mpt2, ~usd, ~crn, ~mpt3, ~yan),
                Sendmax(eur(100)),
                Txflags(tfPartialPayment | tfNoRippleDirect));
            env.close();

            BEAST_EXPECT(env.balance(carol, eur) == eur(900));
            BEAST_EXPECT(env.balance(bob, yan) == yan(1'100));
            BEAST_EXPECT(amm.expectBalances(crn(1'104), mpt3(1'000), amm.tokens()));
            BEAST_EXPECT(expectOffers(env, alice, 0));
        }

        // Multi-steps with AMM and MPT endpoints
        // mpt1/EUR EUR/mpt2 mpt2/USD USD/CRN AMM:CRN/MPT3 MPT3/MPT4
        {
            Env env{*this, features};
            auto const usd = gw["USD"];
            auto const eur = gw["EUR"];
            auto const crn = gw["CRN"];

            fund(env, gw, {alice, carol, bob}, XRP(1'000), {usd(1'000), eur(1'000), crn(2'000)});

            auto createMPT = [&]() -> std::pair<MPTTester, MPT> {
                MPTTester mptTester(env, gw, {.fund = false});
                mptTester.create({.flags = tfMPTCanTransfer | tfMPTCanTrade});
                mptTester.authorize({.account = alice});
                mptTester.pay(gw, alice, 2'000);
                return {mptTester, mptTester["MPT"]};
            };

            auto const [mptTester1, mpt1] = createMPT();
            auto const [mptTester2, mpt2] = createMPT();
            auto const [mptTester3, mpt3] = createMPT();
            auto [mptTester4, mpt4] = createMPT();
            mptTester4.authorize({.account = bob});

            env(offer(alice, eur(100), mpt1(101)));
            env(offer(alice, mpt1(101), mpt2(102)));
            env(offer(alice, mpt2(102), usd(103)));
            env(offer(alice, usd(103), crn(104)));
            env.close();
            AMM const amm(env, alice, crn(1'000), mpt3(1'104));
            env(offer(alice, mpt3(104), mpt4(100)));

            env(pay(carol, bob, mpt4(100)),
                test::jtx::Path(~mpt1, ~mpt2, ~usd, ~crn, ~mpt3, ~mpt4),
                Sendmax(eur(100)),
                Txflags(tfPartialPayment | tfNoRippleDirect));
            env.close();

            BEAST_EXPECT(env.balance(carol, eur) == eur(900));
            BEAST_EXPECT(mptTester4.checkMPTokenAmount(bob, 100));
            BEAST_EXPECT(amm.expectBalances(crn(1'104), mpt3(1'000), amm.tokens()));
            BEAST_EXPECT(expectOffers(env, alice, 0));
        }

        // Check that limiting step reduces maximumAmount returned by
        // MPTEndpointStep::maxPaymentFlow()
        {
            Env env(*this, features);

            env.fund(XRP(1'000), gw, alice, carol, bob);

            MPTTester usdTester(env, gw, {.holders = {alice, carol, bob}, .fund = false});
            usdTester.create(
                {.maxAmt = 1'000,
                 .authorize = MPTCreate::allHolders,
                 .pay = {{{alice}, 1'000}},
                 .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const usd = usdTester["USD"];

            MPTTester eurTester(env, gw, {.holders = {alice, carol, bob}, .fund = false});
            eurTester.create(
                {.maxAmt = 1'000,
                 .authorize = {{alice, carol}},
                 .pay = {{{carol}, 100}},
                 .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const eur = eurTester["EUR"];

            env(offer(alice, eur(10), usd(10)));

            env(pay(carol, bob, usd(10)),
                Sendmax(eur(10)),
                Path(~usd),
                Txflags(tfNoRippleDirect | tfPartialPayment));
        }
        {
            Env env(*this, features);  // NOLINT TODO
            env.fund(XRP(1'000), gw, alice, carol, bob);

            auto musd = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice, carol, bob}, .maxAmt = 1'000});
            MPT const usd = musd;
            env(pay(gw, alice, usd(800)));
            env(offer(gw, XRP(300), usd(300)));
            env(pay(carol, bob, usd(300)),
                Sendmax(XRP(300)),
                Path(~usd),
                Txflags(tfPartialPayment));
            BEAST_EXPECT(musd.checkMPTokenAmount(bob, 200));
            BEAST_EXPECT(musd.checkMPTokenOutstandingAmount(1'000));
            // initial + offer - fees
            BEAST_EXPECT(env.balance(gw) == (XRP(1'000) + XRP(200) - txFee(env, 3)));
        }
        {
            Env env(*this, features);
            auto const eur = gw["EUR"];
            env.fund(XRP(1'000), gw, alice, carol, bob);
            env.close();

            env(trust(alice, eur(1'000)));
            env(pay(gw, alice, eur(300)));
            env(trust(bob, eur(1'000)));

            auto musd = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice, carol, bob}, .maxAmt = 1'000});
            MPT const usd = musd;

            env(pay(gw, alice, usd(800)));
            env(offer(gw, XRP(300), usd(300)));
            env(offer(alice, usd(300), eur(300)));
            env(pay(carol, bob, eur(300)),
                Sendmax(XRP(300)),
                Path(~usd, ~eur),
                Txflags(tfPartialPayment));
            BEAST_EXPECT(musd.checkMPTokenAmount(alice, 1'000));
            BEAST_EXPECT(musd.checkMPTokenOutstandingAmount(1'000));
            // initial + offer - fees
            BEAST_EXPECT(env.balance(gw) == (XRP(1'000) + XRP(200) - txFee(env, 4)));
            BEAST_EXPECT(env.balance(bob, eur) == eur(200));
        }
    }

    void
    testPath(FeatureBitset features)
    {
        testcase("Path");
        using namespace test::jtx;
        Account const gw{"gw"};
        Account const gw1{"gw1"};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const bob{"bob"};
        Account const dan{"dan"};
        auto const usd = gw["USD"];
        auto const eur = gw1["EUR"];

        // MPT can be a mpt end point step or a book-step

        // Direct MPT payment
        {
            Env env = pathTestEnv(*this);

            MPTTester mptTester(env, gw, {.holders = {dan, carol}});
            mptTester.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt = mptTester["MPT"];
            mptTester.authorize({.account = dan});
            mptTester.authorize({.account = carol});
            mptTester.pay(gw, carol, 200);

            auto const [pathSet, srcAmt, dstAmt] = findPaths(env, carol, dan, mpt(-1));
            BEAST_EXPECT(srcAmt == mpt(200));
            BEAST_EXPECT(dstAmt == mpt(200));
            // Direct payment, no path
            BEAST_EXPECT(pathSet.empty());
        }

        // Cross-asset payment via XRP/MPT offer (one step)
        {
            Env env = pathTestEnv(*this);

            env.fund(XRP(1'000), carol);

            MPTTester mptTester(env, gw, {.holders = {alice, dan}});

            mptTester.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt = mptTester["MPT"];

            mptTester.authorize({.account = alice});
            mptTester.authorize({.account = dan});
            mptTester.pay(gw, alice, 200);

            env(offer(alice, XRP(100), mpt(100)));
            env.close();

            auto const [pathSet, srcAmt, dstAmt] = findPaths(env, carol, dan, mpt(-1));
            BEAST_EXPECT(srcAmt == XRP(100));
            BEAST_EXPECT(dstAmt == mpt(100));
            if (BEAST_EXPECT(same(pathSet, stpath(ipe(mptTester.issuanceID())))))
            {
                // validate a payment works with the path
                env(pay(carol, dan, mpt(10)),
                    Path(~mpt),
                    Sendmax(XRP(10)),
                    Txflags(tfNoRippleDirect | tfPartialPayment));
            }
        }

        // Cross-asset payment via IOU/MPT offer (one step)
        {
            Env env = pathTestEnv(*this);

            env.fund(XRP(1'000), carol);
            env.fund(XRP(1'000), gw);

            MPTTester mptTester(env, gw1, {.holders = {alice, dan}});

            mptTester.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt = mptTester["MPT"];

            mptTester.authorize({.account = alice});
            mptTester.authorize({.account = dan});
            mptTester.pay(gw1, alice, 200);

            env(trust(alice, usd(400)));
            env(trust(carol, usd(400)));
            env(pay(gw, carol, usd(200)));

            env(offer(alice, usd(100), mpt(100)));
            env.close();

            // No sendMax
            STPathSet pathSet;
            STAmount srcAmt;
            STAmount dstAmt;
            std::tie(pathSet, srcAmt, dstAmt) = findPaths(env, carol, dan, mpt(-1));
            BEAST_EXPECT(srcAmt == usd(100));
            BEAST_EXPECT(dstAmt == mpt(100));
            if (BEAST_EXPECT(
                    pathSet.size() == 1 && same(pathSet, stpath(gw, ipe(mptTester.issuanceID())))))
            {
                // Validate the payment works with the path
                env(pay(carol, dan, mpt(10)),
                    Path(pathSet[0]),
                    Sendmax(usd(10)),
                    Txflags(tfNoRippleDirect | tfPartialPayment));
            }

            // Include sendMax
            std::tie(pathSet, srcAmt, dstAmt) = findPaths(env, carol, dan, mpt(-1), usd(-1));
            BEAST_EXPECT(srcAmt == usd(90));
            BEAST_EXPECT(dstAmt == mpt(90));
            if (BEAST_EXPECT(
                    pathSet.size() == 1 && same(pathSet, stpath(ipe(mptTester.issuanceID())))))
            {
                // validate a payment works with the path
                env(pay(carol, dan, mpt(10)),
                    Path(pathSet[0]),
                    Sendmax(usd(10)),
                    Txflags(tfNoRippleDirect | tfPartialPayment));
            }

            // Include source token
            std::tie(pathSet, srcAmt, dstAmt) =
                findPaths(env, carol, dan, mpt(-1), std::nullopt, usd.currency);
            BEAST_EXPECT(srcAmt == usd(80));
            BEAST_EXPECT(dstAmt == mpt(80));
            if (BEAST_EXPECT(
                    pathSet.size() == 1 && same(pathSet, stpath(gw, ipe(mptTester.issuanceID())))))
            {
                // validate a payment works with the path
                env(pay(carol, dan, mpt(10)),
                    Path(pathSet[0]),
                    Sendmax(usd(10)),
                    Txflags(tfNoRippleDirect | tfPartialPayment));
            }
        }

        // Cross-asset payment via MPT/IOU offer (one step)
        {
            Env env = pathTestEnv(*this);

            env.fund(XRP(1'000), dan);
            env.fund(XRP(1'000), gw);

            MPTTester mptTester(env, gw1, {.holders = {carol, alice}});

            mptTester.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt = mptTester["MPT"];

            mptTester.authorize({.account = carol});
            mptTester.authorize({.account = alice});
            mptTester.pay(gw1, carol, 200);

            env(trust(dan, usd(400)));
            env(trust(alice, usd(400)));
            env(pay(gw, alice, usd(200)));

            env(offer(alice, mpt(100), usd(100)));
            env.close();

            // No sendMax
            STPathSet pathSet;
            STAmount srcAmt;
            STAmount dstAmt;
            std::tie(pathSet, srcAmt, dstAmt) = findPaths(env, carol, dan, usd(-1));
            BEAST_EXPECT(srcAmt == mpt(100));
            BEAST_EXPECT(dstAmt == usd(100));
            if (BEAST_EXPECT(pathSet.size() == 1 && same(pathSet, stpath(ipe(usd)))))
            {
                // Validate the payment works with the path
                env(pay(carol, dan, usd(10)),
                    Path(pathSet[0]),
                    Sendmax(mpt(10)),
                    Txflags(tfNoRippleDirect | tfPartialPayment));
            }

            // Include sendMax
            std::tie(pathSet, srcAmt, dstAmt) = findPaths(env, carol, dan, usd(-1), mpt(-1));
            BEAST_EXPECT(srcAmt == mpt(90));
            BEAST_EXPECT(dstAmt == usd(90));
            if (BEAST_EXPECT(pathSet.size() == 1 && same(pathSet, stpath(ipe(usd)))))
            {
                // validate a payment works with the path
                env(pay(carol, dan, usd(10)),
                    Path(pathSet[0]),
                    Sendmax(mpt(10)),
                    Txflags(tfNoRippleDirect | tfPartialPayment));
            }

            // Include source token
            std::tie(pathSet, srcAmt, dstAmt) =
                findPaths(env, carol, dan, usd(-1), std::nullopt, mpt.mpt());
            BEAST_EXPECT(srcAmt == mpt(80));
            BEAST_EXPECT(dstAmt == usd(80));
            if (BEAST_EXPECT(pathSet.size() == 1 && same(pathSet, stpath(ipe(usd)))))
            {
                // validate a payment works with the path
                env(pay(carol, dan, usd(10)),
                    Path(pathSet[0]),
                    Sendmax(mpt(10)),
                    Txflags(tfNoRippleDirect | tfPartialPayment));
            }
        }

        // Cross-asset payment via mpt1/MPT offer (one step)
        {
            Env env = pathTestEnv(*this);

            MPTTester mptTester(env, gw, {.holders = {alice, dan}});
            MPTTester mptTester1(env, gw1, {.holders = {carol}});

            mptTester.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt = mptTester["MPT"];
            mptTester1.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt1 = mptTester1["MPT1"];

            mptTester.authorize({.account = alice});
            mptTester.authorize({.account = dan});
            mptTester.pay(gw, alice, 200);

            mptTester1.authorize({.account = carol});
            mptTester1.authorize({.account = alice});
            mptTester1.pay(gw1, carol, 200);

            env(offer(alice, mpt1(100), mpt(100)));
            env.close();

            // No sendMax
            STPathSet pathSet;
            STAmount srcAmt;
            STAmount dstAmt;
            std::tie(pathSet, srcAmt, dstAmt) = findPaths(env, carol, dan, mpt(-1));
            BEAST_EXPECT(srcAmt == mpt1(100));
            BEAST_EXPECT(dstAmt == mpt(100));
            if (BEAST_EXPECT(
                    pathSet.size() == 1 && same(pathSet, stpath(ipe(mptTester.issuanceID())))))
            {
                // validate a payment works with the path
                env(pay(carol, dan, mpt(10)),
                    Path(pathSet[0]),
                    Sendmax(mpt1(10)),
                    Txflags(tfNoRippleDirect | tfPartialPayment));
            }

            // Include sendMax
            std::tie(pathSet, srcAmt, dstAmt) = findPaths(env, carol, dan, mpt(-1), mpt1(-1));
            BEAST_EXPECT(srcAmt == mpt1(90));
            BEAST_EXPECT(dstAmt == mpt(90));
            if (BEAST_EXPECT(
                    pathSet.size() == 1 && same(pathSet, stpath(ipe(mptTester.issuanceID())))))
            {
                // validate a payment works with the path
                env(pay(carol, dan, mpt(10)),
                    Path(pathSet[0]),
                    Sendmax(mpt1(10)),
                    Txflags(tfNoRippleDirect | tfPartialPayment));
            }

            // Include source token
            std::tie(pathSet, srcAmt, dstAmt) =
                findPaths(env, carol, dan, mpt(-1), std::nullopt, mpt1.mpt());
            BEAST_EXPECT(srcAmt == mpt1(80));
            BEAST_EXPECT(dstAmt == mpt(80));
            if (BEAST_EXPECT(
                    pathSet.size() == 1 && same(pathSet, stpath(ipe(mptTester.issuanceID())))))
            {
                // validate a payment works with the path
                env(pay(carol, dan, mpt(10)),
                    Path(pathSet[0]),
                    Sendmax(mpt1(10)),
                    Txflags(tfNoRippleDirect | tfPartialPayment));
            }
        }

        // Cross-asset payment via offers (two steps)
        {
            Env env = pathTestEnv(*this);

            env.fund(XRP(1'000), carol);
            env.fund(XRP(1'000), dan);

            MPTTester mptTester(env, gw, {.holders = {alice, bob}});

            mptTester.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt = mptTester["MPT"];

            mptTester.authorize({.account = alice});
            mptTester.authorize({.account = bob});
            mptTester.pay(gw, alice, 200);
            mptTester.pay(gw, bob, 200);

            env(trust(bob, usd(200)));
            env(pay(gw, bob, usd(100)));
            env(trust(dan, usd(200)));
            env(trust(alice, usd(200)));

            env(offer(alice, XRP(100), mpt(100)));
            env(offer(bob, mpt(100), usd(100)));
            env.close();

            // No sendMax
            STPathSet pathSet;
            STAmount srcAmt;
            STAmount dstAmt;
            std::tie(pathSet, srcAmt, dstAmt) = findPaths(env, carol, dan, usd(-1));
            BEAST_EXPECT(srcAmt == XRP(100));
            BEAST_EXPECT(dstAmt == usd(100));
            if (BEAST_EXPECT(
                    pathSet.size() == 1 &&
                    same(pathSet, stpath(ipe(mptTester.issuanceID()), ipe(usd)))))
            {
                // validate a payment works with the path
                env(pay(carol, dan, usd(10)),
                    Path(pathSet[0]),
                    Sendmax(XRP(10)),
                    Txflags(tfNoRippleDirect | tfPartialPayment));
            }

            // Include sendMax
            std::tie(pathSet, srcAmt, dstAmt) = findPaths(env, carol, dan, usd(-1), XRP(100));
            BEAST_EXPECT(srcAmt == XRP(90));
            BEAST_EXPECT(dstAmt == usd(90));
            if (BEAST_EXPECT(
                    pathSet.size() == 1 &&
                    same(pathSet, stpath(ipe(mptTester.issuanceID()), ipe(usd)))))
            {
                // validate a payment works with the path
                env(pay(carol, dan, usd(10)),
                    Path(pathSet[0]),
                    Sendmax(XRP(10)),
                    Txflags(tfNoRippleDirect | tfPartialPayment));
            }
        }

        // Cross-asset payment via offers (two steps)
        // Start/End with mpt/mp1 and book steps in the middle
        {
            Env env = pathTestEnv(*this);
            Account const gw2{"gw2"};
            env.fund(XRP(1'000), gw2);
            auto const usd2 = gw2["USD"];

            MPTTester mptTester(env, gw, {.holders = {alice, carol}});
            mptTester.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt = mptTester["MPT"];
            mptTester.authorize({.account = alice});
            mptTester.authorize({.account = carol});
            mptTester.pay(gw, carol, 200);

            MPTTester mptTester1(env, gw1, {.holders = {bob, dan}});
            mptTester1.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt1 = mptTester1["MPT1"];
            mptTester1.authorize({.account = bob});
            mptTester1.pay(gw1, bob, 200);
            mptTester1.authorize({.account = dan});

            env(trust(alice, usd2(400)));
            env(pay(gw2, alice, usd2(200)));
            env(trust(bob, usd2(400)));

            env(offer(alice, mpt(100), usd2(100)));
            env(offer(bob, usd2(100), mpt1(100)));
            env.close();

            // No sendMax
            STPathSet pathSet;
            STAmount srcAmt;
            STAmount dstAmt;
            std::tie(pathSet, srcAmt, dstAmt) = findPaths(env, carol, dan, mpt1(-1));
            BEAST_EXPECT(srcAmt == mpt(100));
            BEAST_EXPECT(dstAmt == mpt1(100));
            if (BEAST_EXPECT(
                    pathSet.size() == 1 &&
                    same(pathSet, stpath(ipe(usd2), ipe(mptTester1.issuanceID())))))
            {
                // validate a payment works with the path
                env(pay(carol, dan, mpt1(10)),
                    Path(pathSet[0]),
                    Sendmax(mpt(10)),
                    Txflags(tfNoRippleDirect | tfPartialPayment));
            }

            // Include sendMax
            std::tie(pathSet, srcAmt, dstAmt) = findPaths(env, carol, dan, mpt1(-1), mpt(-1));
            BEAST_EXPECT(srcAmt == mpt(90));
            BEAST_EXPECT(dstAmt == mpt1(90));
            if (BEAST_EXPECT(
                    pathSet.size() == 1 &&
                    same(pathSet, stpath(ipe(usd2), ipe(mptTester1.issuanceID())))))
            {
                // validate a payment works with the path
                env(pay(carol, dan, mpt1(10)),
                    Path(pathSet[0]),
                    Sendmax(mpt(10)),
                    Txflags(tfNoRippleDirect | tfPartialPayment));
            }

            // Include source token
            std::tie(pathSet, srcAmt, dstAmt) =
                findPaths(env, carol, dan, mpt1(-1), std::nullopt, mpt.mpt());
            BEAST_EXPECT(srcAmt == mpt(80));
            BEAST_EXPECT(dstAmt == mpt1(80));
            if (BEAST_EXPECT(
                    pathSet.size() == 1 &&
                    same(pathSet, stpath(ipe(usd2), ipe(mptTester1.issuanceID())))))
            {
                // validate a payment works with the path
                env(pay(carol, dan, mpt1(10)),
                    Path(pathSet[0]),
                    Sendmax(mpt(10)),
                    Txflags(tfNoRippleDirect | tfPartialPayment));
            }
        }

        // Cross-asset payment via offers (two steps)
        // Start/End with mpt/mp2 and book steps in the middle
        // offers are MPT/MPT
        {
            Env env = pathTestEnv(*this);
            Account const gw2{"gw2"};
            env.fund(XRP(1'000), gw, gw1, gw2, alice, bob, carol, dan);

            MPTTester mptTester(env, gw, {.holders = {alice, carol}, .fund = false});
            mptTester.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt = mptTester["MPT"];
            mptTester.authorize({.account = alice});
            mptTester.authorize({.account = carol});
            mptTester.pay(gw, carol, 200);

            MPTTester mptTester1(env, gw1, {.holders = {bob, alice}, .fund = false});
            mptTester1.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt1 = mptTester1["MPT1"];
            mptTester1.authorize({.account = alice});
            mptTester1.pay(gw1, alice, 200);
            mptTester1.authorize({.account = bob});

            MPTTester mptTester2(env, gw2, {.holders = {bob, dan}, .fund = false});
            mptTester2.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt2 = mptTester2["MPT2"];
            mptTester2.authorize({.account = bob});
            mptTester2.pay(gw2, bob, 200);
            mptTester2.authorize({.account = dan});

            env(offer(alice, mpt(100), mpt1(100)));
            env(offer(bob, mpt1(100), mpt2(100)));
            env.close();

            // No sendMax
            STPathSet pathSet;
            STAmount srcAmt;
            STAmount dstAmt;
            std::tie(pathSet, srcAmt, dstAmt) = findPaths(env, carol, dan, mpt2(-1));
            BEAST_EXPECT(srcAmt == mpt(100));
            BEAST_EXPECT(dstAmt == mpt2(100));
            if (BEAST_EXPECT(
                    pathSet.size() == 1 &&
                    same(
                        pathSet,
                        stpath(ipe(mptTester1.issuanceID()), ipe(mptTester2.issuanceID())))))
            {
                // validate a payment works with the path
                env(pay(carol, dan, mpt2(10)),
                    Path(pathSet[0]),
                    Sendmax(mpt(10)),
                    Txflags(tfNoRippleDirect | tfPartialPayment));
            }

            // Include sendMax
            std::tie(pathSet, srcAmt, dstAmt) = findPaths(env, carol, dan, mpt2(-1), mpt(-1));
            BEAST_EXPECT(srcAmt == mpt(90));
            BEAST_EXPECT(dstAmt == mpt2(90));
            if (BEAST_EXPECT(
                    pathSet.size() == 1 &&
                    same(
                        pathSet,
                        stpath(ipe(mptTester1.issuanceID()), ipe(mptTester2.issuanceID())))))
            {
                // validate a payment works with the path
                env(pay(carol, dan, mpt2(10)),
                    Path(pathSet[0]),
                    Sendmax(mpt(10)),
                    Txflags(tfNoRippleDirect | tfPartialPayment));
            }

            // Include source token
            std::tie(pathSet, srcAmt, dstAmt) =
                findPaths(env, carol, dan, mpt2(-1), std::nullopt, mpt.mpt());
            BEAST_EXPECT(srcAmt == mpt(80));
            BEAST_EXPECT(dstAmt == mpt2(80));
            if (BEAST_EXPECT(
                    pathSet.size() == 1 &&
                    same(
                        pathSet,
                        stpath(ipe(mptTester1.issuanceID()), ipe(mptTester2.issuanceID())))))
            {
                // validate a payment works with the path
                env(pay(carol, dan, mpt2(10)),
                    Path(pathSet[0]),
                    Sendmax(mpt(10)),
                    Txflags(tfNoRippleDirect | tfPartialPayment));
            }
        }

        // verify no MPT rippling
        {
            Env env = pathTestEnv(*this);
            Account const gw{"gw"};
            Account const gw1{"gw1"};
            Account const carol{"carol"};
            Account const bob{"bob"};
            Account const dan{"dan"};
            Account const john{"john"};
            Account const sean{"sean"};

            env.fund(XRP(1'000'000), gw);
            env.fund(XRP(1'000'000), gw1);
            env.fund(XRP(1'000'000), carol);
            env.fund(XRP(1'000'000), dan);
            env.fund(XRP(1'000'000), bob);
            env.fund(XRP(1'000'000), john);
            env.fund(XRP(1'000'000), sean);
            env.close();

            MPTTester usdTester(env, gw, {.holders = {carol, dan}, .fund = false});
            usdTester.create(
                {.authorize = MPTCreate::allHolders,
                 .pay = {{MPTCreate::allHolders, 100}},
                 .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const usd = usdTester["USD"];
            env(offer(carol, XRP(100), usd(100)));

            MPTTester gbpTester(env, gw, {.holders = {bob, sean}, .fund = false});
            gbpTester.create(
                {.authorize = MPTCreate::allHolders,
                 .pay = {{{bob}, 100}},
                 .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const gbp = gbpTester["GBP"];

            MPTTester usd1(env, gw1, {.holders = {bob, dan}, .fund = false});
            usd1.create(
                {.authorize = MPTCreate::allHolders,
                 .pay = {{{dan}, 100}},
                 .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const usD1 = usd1["USD1"];
            env(offer(bob, usd1(100), gbp(100)));

            // dan has USD/gw and USD1/gw. Had USD been IOU, it would have
            // been able to ripple through dan's account.
            auto const [pathSet, srcAmt, dstAmt] = findPaths(env, john, sean, gbp(-1), XRP(-1));
            BEAST_EXPECT(pathSet.empty());

            env(pay(john, sean, gbp(10)),
                Sendmax(XRP(20)),
                Path(~usd, dan, gw1, ~gbp),
                Txflags(tfNoRippleDirect | tfPartialPayment),
                Ter(temBAD_PATH));
        }
    }

    void
    testCheck(FeatureBitset features)
    {
        testcase("Check Create/Cash");

        using namespace test::jtx;
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const carol{"carol"};

        // MPTokensV2 is disabled
        {
            Env env{*this, features - featureMPTokensV2};

            MPTTester mptTester(env, gw, {.holders = {alice}});
            mptTester.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt = mptTester["MPT"];
            mptTester.authorize({.account = alice});

            uint256 const checkId{keylet::check(gw, env.seq(gw)).key};

            env(check::create(gw, alice, mpt(100)), Ter(temDISABLED));
            env.close();

            env(check::cash(alice, checkId, mpt(100)), Ter(temDISABLED));
            env.close();
        }

        // Insufficient funds
        {
            Env env{*this, features};
            Account const carol{"carol"};

            MPTTester mptTester(env, gw, {.holders = {alice, carol}});
            mptTester.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt = mptTester["MPT"];
            mptTester.authorize({.account = alice});
            mptTester.pay(gw, alice, 50);

            uint256 const checkId{keylet::check(alice, env.seq(alice)).key};

            // can create
            env(check::create(alice, carol, mpt(100)));
            env.close();

            // can't cash since alice only has 50 of MPT
            env(check::cash(carol, checkId, mpt(100)), Ter(tecPATH_PARTIAL));
            env.close();

            // can cash if DeliverMin is set
            // carol is not authorized, MPToken is authorized by CheckCash
            env(check::cash(carol, checkId, check::DeliverMin(mpt(50))));
            env.close();
            BEAST_EXPECT(mptTester.checkMPTokenAmount(carol, 50));
            BEAST_EXPECT(mptTester.checkMPTokenOutstandingAmount(50));
        }

        // Exceed max amount
        {
            Env env{*this, features};

            MPTTester mptTester(env, gw, {.holders = {alice}});
            mptTester.create(
                {.maxAmt = 100,
                 .ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt = mptTester["MPT"];

            uint256 const checkId{keylet::check(gw, env.seq(gw)).key};

            // can create
            env(check::create(gw, alice, mpt(200)));
            env.close();

            // can't cash since the outstanding amount exceeds max amount
            env(check::cash(alice, checkId, mpt(200)), Ter(tecPATH_PARTIAL));
            env.close();

            // can cash if DeliverMin is set
            env(check::cash(alice, checkId, check::DeliverMin(mpt(100))));
            env.close();
            BEAST_EXPECT(mptTester.checkMPTokenAmount(alice, 100));
            BEAST_EXPECT(mptTester.checkMPTokenOutstandingAmount(100));
        }

        // MPTokenIssuance object doesn't exist
        {
            Env env{*this, features};
            env.fund(XRP(1'000), gw, alice, carol);
            env(check::create(alice, carol, MPT(gw)(50)), Ter(tecOBJECT_NOT_FOUND));
            env.close();
            auto btc = MPTTester({.env = env, .issuer = gw});
            uint256 const chkId{getCheckIndex(gw, env.seq(gw))};
            env(check::cash(carol, chkId, MPT(gw)(1)), Ter(tecNO_ENTRY));
            env.close();
        }

        // MPToken doesn't exist - can create check since MPToken will be
        // automatically created on cash check
        {
            Env env{*this, features};
            env.fund(XRP(1'000), gw, alice, carol);
            auto btc = MPTTester({.env = env, .issuer = gw});
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, carol, btc(50)));
            env.close();

            // But cashing fails if alice doesn't have MPToken
            env(check::cash(carol, chkId, btc(1)), Ter(tecPATH_PARTIAL));
            env.close();
        }

        // MPTLock is set
        {
            Env env{*this, features};
            env.fund(XRP(1'000), gw, alice, carol);
            env.close();
            auto mpt = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .pay = 100,
                 .flags = kMptDexFlags | tfMPTCanLock});

            mpt.set({.flags = tfMPTLock});

            // Create Check fails, holder or issuer as destination
            env(check::create(alice, carol, mpt(10)), Ter(tecLOCKED));
            env.close();
            env(check::create(gw, carol, mpt(10)), Ter(tecLOCKED));
            env.close();

            mpt.set({.flags = tfMPTUnlock});

            // Create Check succeeds, holder or issuer as destination
            uint256 const chkIdAlice{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, carol, mpt(10)));
            env.close();
            uint256 const chkIdGw{getCheckIndex(gw, env.seq(gw))};
            env(check::create(gw, carol, mpt(10)));
            env.close();

            mpt.set({.flags = tfMPTLock});

            // Cash Check fails, holder and issuer env(check::cash(carol,
            // chkIdAlice, mpt(1)), ter(tecPATH_PARTIAL)); // tec is different
            // if the source is the issuer (this is consistent with IOU)
            env(check::cash(carol, chkIdGw, mpt(2)), Ter(tecLOCKED));
            env.close();

            mpt.set({.flags = tfMPTUnlock});

            // Cash Check succeeds, holder and issuer.
            env(check::cash(carol, chkIdAlice, mpt(1)));
            env(check::cash(carol, chkIdGw, mpt(2)));

            // Individual lock
            mpt.set({.holder = alice, .flags = tfMPTLock});
            env(check::create(alice, carol, mpt(10)), Ter(tecLOCKED));
            env.close();
            env(check::create(carol, alice, mpt(10)), Ter(tecLOCKED));
            env.close();

            mpt.set({.holder = alice, .flags = tfMPTUnlock});
            uint256 const chkId1{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, carol, mpt(10)));
            env.close();
            uint256 const chkId2{getCheckIndex(gw, env.seq(gw))};
            env(check::create(gw, alice, mpt(10)));
            env.close();
            uint256 const chkId3{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, gw, mpt(10)));
            env.close();
            uint256 const chkId4{getCheckIndex(gw, env.seq(gw))};
            env(check::create(gw, alice, mpt(10)));
            env.close();
            mpt.set({.holder = alice, .flags = tfMPTLock});
            env(check::cash(carol, chkId1, mpt(1)), Ter(tecPATH_PARTIAL));
            env(check::cash(alice, chkId2, mpt(1)), Ter(tecLOCKED));
            env(check::cash(gw, chkId3, mpt(1)), Ter(tecPATH_PARTIAL));
            env(check::cash(alice, chkId4, mpt(1)), Ter(tecLOCKED));
        }

        // MPTRequireAuth flag is set and the account is not authorized.
        // Can create check, which is consistent with the trustlines.
        // It should fail on cash check.
        {
            Env env{*this, features};
            env.fund(XRP(1'000), gw, alice, carol);
            auto btc = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .flags = tfMPTRequireAuth | kMptDexFlags});
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, carol, btc(50)));
            env.close();

            // Authorize alice
            btc.authorize({.account = gw, .holder = alice});
            env(pay(gw, alice, btc(100)));

            // carol is still not authorized
            env(check::cash(carol, chkId, btc(10)), Ter(tecNO_AUTH));
            env.close();

            // authorize carol, can cash now
            btc.authorize({.account = gw, .holder = carol});
            env(check::cash(carol, chkId, btc(10)));
            env.close();
        }

        // MPTCanTransfer is disabled
        {
            Env env{*this, features};
            env.fund(XRP(1'000), gw, alice, carol);
            env.close();

            MPTTester mpt(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .flags = tfMPTCanTrade,
                 .mutableFlags = tmfMPTCanEnableCanTransfer});

            // src is issuer
            uint256 checkId{keylet::check(gw, env.seq(gw)).key};

            // can create
            env(check::create(gw, alice, mpt(100)));
            env.close();

            // can cash since source is issuer
            env(check::cash(alice, checkId, mpt(100)));
            env.close();

            BEAST_EXPECT(env.balance(alice, mpt) == mpt(100));
            BEAST_EXPECT(env.balance(gw, mpt) == mpt(-100));

            // dst is issuer
            checkId = keylet::check(alice, env.seq(alice)).key;

            // can create
            env(check::create(alice, gw, mpt(100)));
            env.close();

            // can cash since source is issuer
            env(check::cash(gw, checkId, mpt(100)));
            env.close();

            BEAST_EXPECT(env.balance(alice, mpt) == mpt(0));
            BEAST_EXPECT(env.balance(gw, mpt) == mpt(0));

            // neither src nor dst is issuer, can't create
            checkId = keylet::check(alice, env.seq(alice)).key;
            env(check::create(alice, carol, mpt(100)), Ter(tecNO_AUTH));
            env.close();

            // can create now
            mpt.set({.account = gw, .mutableFlags = tmfMPTSetCanTransfer});
            checkId = keylet::check(alice, env.seq(alice)).key;
            env(check::create(alice, carol, mpt(100)));
            env.close();
            env(pay(gw, alice, mpt(10)));
            env.close();

            // can cash since MPTCanTransfer is enabled
            env(check::cash(carol, checkId, mpt(10)));
            env.close();
        }

        // MPTCanTrade is disabled
        {
            Env env{*this, features};
            env.fund(XRP(1'000), gw, alice, carol);
            env.close();

            MPT const mpt = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .pay = 10,
                 .flags = tfMPTCanTransfer});

            uint256 const checkId{keylet::check(alice, env.seq(alice)).key};

            // can create
            env(check::create(alice, carol, mpt(100)));
            env.close();

            // can cash
            env(check::cash(carol, checkId, mpt(10)));
            env.close();
        }

        // MPTokenIssuance object doesn't exist
        {
            Env env{*this, features};
            env.fund(XRP(1'000), gw, alice, carol);
            auto usd = MPTTester({.env = env, .issuer = gw, .holders = {alice}});
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, carol, usd(1)));
            env.close();

            // temMALFORMED because MPT is not USD. It doesn't matter if it
            // exists or not
            env(check::cash(carol, chkId, MPT(alice)(1)), Ter(temMALFORMED));
            env.close();
        }

        // MPToken object doesn't exist and the account is not the issuer of MPT
        {
            Env env{*this, features};
            env.fund(XRP(1'000), gw, alice, carol);

            auto btc = MPTTester({.env = env, .issuer = gw, .holders = {alice}, .pay = 1'000});

            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};

            env(check::create(alice, carol, btc(1)));
            env.close();

            // MPToken is automatically created
            env(check::cash(carol, chkId, btc(1)));
            env.close();
        }

        // MPTRequireAuth flag is set and the account is not authorized.
        {
            Env env{*this, features};
            env.fund(XRP(1'000), gw, alice, carol);

            auto btc = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .flags = tfMPTRequireAuth | kMptDexFlags,
                 .authHolder = true});
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, carol, btc(1)));
            env.close();

            env(check::cash(carol, chkId, btc(1)), Ter(tecPATH_PARTIAL));
            env.close();
        }

        // Can create check if src/dst don't own MPT
        {
            Env env{*this, features};

            MPTTester mptTester(env, gw);
            mptTester.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt = mptTester["MPT"];

            env.fund(XRP(1'000), alice, carol);

            // src is issuer
            uint256 const checkId{keylet::check(alice, env.seq(alice)).key};

            // can create
            env(check::create(alice, carol, mpt(100)));
            env.close();

            // authorize/fund alice
            mptTester.authorize({.account = alice});
            mptTester.pay(gw, alice, 100);

            // carol can cash the check. MPToken is created automatically
            env(check::cash(carol, checkId, mpt(100)));
            env.close();

            BEAST_EXPECT(mptTester.checkMPTokenAmount(carol, 100));
            BEAST_EXPECT(mptTester.checkMPTokenOutstandingAmount(100));
        }

        // Normal create/cash
        {
            Env env{*this, features};

            MPTTester mptTester(env, gw, {.holders = {alice}});
            mptTester.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt = mptTester["MPT"];
            mptTester.authorize({.account = alice});

            uint256 const checkId{keylet::check(gw, env.seq(gw)).key};

            env(check::create(gw, alice, mpt(100)));
            env.close();

            env(check::cash(alice, checkId, mpt(100)));
            env.close();

            BEAST_EXPECT(mptTester.checkMPTokenAmount(alice, 100));
            BEAST_EXPECT(mptTester.checkMPTokenOutstandingAmount(100));
        }
    }

    void
    testAMMClawback(FeatureBitset features)
    {
        using namespace jtx;
        testcase("AMMClawback");
        Account const gw{"gw"};
        Account const alice{"alice"};
        auto const usd = gw["USD"];

        // MPTokenIssuance object doesn't exist
        {
            Env env(*this, features);
            env.fund(XRP(1'000), gw, alice);
            MPTTester const btc({.env = env, .issuer = gw});
            AMM const amm(env, gw, btc(100), usd(100));
            env(amm::ammClawback(gw, alice, usd, MPT(alice), std::nullopt), Ter(terNO_AMM));
            env(amm::ammClawback(gw, alice, usd, btc, MPT(alice)(100)), Ter(temBAD_AMOUNT));
        }

        // MPTLock flag is set and the account is not the issuer of MPT -
        // can still clawback since the issuer clawbacks
        {
            Env env(*this, features);
            env.fund(XRP(100'000), gw, alice);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            auto btc = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 40'000,
                 .flags = tfMPTCanLock | tfMPTCanClawback | kMptDexFlags});

            env.trust(usd(10'000), alice);
            env(pay(gw, alice, usd(10'000)));
            env.close();

            AMM amm(env, gw, btc(100), usd(100));
            env.close();
            amm.deposit(alice, 1'000);
            env.close();

            btc.set({.flags = tfMPTLock});

            env(amm::ammClawback(gw, alice, btc, usd, std::nullopt));
        }

        // MPTRequireAuth flag is set and the account is not authorized -
        // can still clawback since the issuer clawbacks
        {
            Env env(*this, features);
            env.fund(XRP(100'000), gw, alice);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            auto btc = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 40'000,
                 .flags = tfMPTRequireAuth | tfMPTCanClawback | kMptDexFlags,
                 .authHolder = true});

            env.trust(usd(10'000), alice);
            env(pay(gw, alice, usd(10'000)));
            env.close();

            AMM amm(env, gw, btc(100), usd(100));
            env.close();
            amm.deposit(alice, 1'000);
            env.close();

            btc.authorize({.account = gw, .holder = alice, .flags = tfMPTUnauthorize});

            env(amm::ammClawback(gw, alice, btc, usd, std::nullopt));
        }

        // MPTCanTransfer is not set and the account is not the issuer of MPT -
        // can't clawback since a holder can't deposit
        {
            Env env(*this, features);
            env.fund(XRP(100'000), gw, alice);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            auto btc = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 40'000,
                 .flags = tfMPTCanClawback | tfMPTCanTrade,
                 .authHolder = true});

            env.trust(usd(10'000), alice);
            env(pay(gw, alice, usd(10'000)));
            env.close();

            AMM amm(env, gw, btc(100), usd(100));
            env.close();
            // alice can't deposit since MPTCanTransfer is not set
            amm.deposit(DepositArg{.account = alice, .tokens = 1'000, .err = Ter(tecNO_AUTH)});
            env.close();

            // can't clawback since alice is not an LP
            env(amm::ammClawback(gw, alice, btc, usd, std::nullopt), Ter(tecAMM_BALANCE));
        }

        {
            Env env(*this, features);
            fund(env, gw, {alice}, XRP(1'000), {usd(1'000)});
            MPTTester mptTester(env, gw, {.fund = false});
            mptTester.create({.flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt = mptTester["MPT"];
            AMM amm(env, gw, mpt(100), XRP(100));
            amm.deposit(DepositArg{.account = alice, .asset1In = XRP(10)});
            amm::ammClawback(gw, alice, MPTIssue(mptTester.issuanceID()), xrpIssue(), mpt(10));
        }

        {
            Env env(*this, features);
            fund(env, gw, {alice}, XRP(1'000), {usd(1'000)});
            MPTTester mptTester(env, gw, {.fund = false});
            mptTester.create({.flags = tfMPTCanTransfer | tfMPTCanTrade});
            mptTester.authorize({.account = alice});
            mptTester.pay(gw, alice, 1'000);
            auto const mpt = mptTester["MPT"];
            AMM amm(env, gw, mpt(100), XRP(100));
            amm.deposit(DepositArg{.account = alice, .tokens = 10'000});
            amm::ammClawback(gw, alice, MPTIssue(mptTester.issuanceID()), xrpIssue(), mpt(10));
        }

        // clawback one asset from MPT/MPT AMM. MPToken for another asset
        // is created for the Liquidity Provider
        {
            Env env(*this, features);
            env.fund(XRP(1'000), gw, alice);
            auto usd = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 10'000,
                 .flags = tfMPTCanClawback | kMptDexFlags});
            auto eur =
                MPTTester({.env = env, .issuer = gw, .flags = tfMPTCanClawback | kMptDexFlags});
            AMM amm(env, gw, usd(1'000), eur(1'000));
            amm.deposit({.account = alice, .asset1In = usd(1'000)});
            // MPToken doesn't exist
            BEAST_EXPECT(env.le(keylet::mptoken(eur.issuanceID(), alice)) == nullptr);
            env(amm::ammClawback(gw, alice, usd, eur, usd(100)));
            // MPToken is created
            BEAST_EXPECT(env.le(keylet::mptoken(eur.issuanceID(), alice)));
        }
    }

    void
    testBasicAMM(FeatureBitset features)
    {
        testcase("Basic AMM");
        using namespace jtx;
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const bob{"bob"};
        auto const usd = gw["USD"];

        // Create/deposit/withdraw
        {
            Env env{*this};

            fund(env, gw, {alice, carol, bob}, XRP(1'000), {usd(1'000)});

            MPTTester mptTester(env, gw, {.fund = false});
            mptTester.create({.flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt = mptTester["MPT"];
            mptTester.authorize({.account = alice});
            mptTester.authorize({.account = carol});
            mptTester.pay(gw, alice, 1'000);
            mptTester.pay(gw, carol, 1'000);

            MPTTester mptTester1(env, gw, {.fund = false});
            mptTester1.create({.flags = tfMPTCanTransfer | tfMPTCanTrade});
            auto const mpt1 = mptTester1["MPT1"];
            mptTester1.authorize({.account = alice});
            mptTester1.authorize({.account = carol});
            mptTester1.pay(gw, alice, 1'000);
            mptTester1.pay(gw, carol, 1'000);

            std::vector<std::tuple<PrettyAmount, PrettyAmount, IOUAmount>> pools = {
                {XRP(100), mpt(100), IOUAmount{100'000}},
                {usd(100), mpt(100), IOUAmount{100}},
                {mpt(100), mpt1(100), IOUAmount{100}}};
            for (auto& pool : pools)
            {
                AMM amm(env, gw, std::get<0>(pool), std::get<1>(pool));
                amm.deposit(alice, std::get<2>(pool));
                amm.deposit(carol, std::get<2>(pool));
                // bob doesn't own MPT
                amm.deposit(
                    DepositArg{
                        .account = bob, .tokens = std::get<2>(pool), .err = Ter(tecNO_AUTH)});
                amm.withdrawAll(alice);
                amm.withdrawAll(carol);
                amm.withdrawAll(gw);
                BEAST_EXPECT(!amm.ammExists());
            }
        }

        // Payment, one step
        {
            Env env(*this);

            env.fund(XRP(1'000), gw, alice, carol);
            env.close();

            MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice, carol}});
            MPT const eur = MPTTester({.env = env, .issuer = gw, .holders = {alice, carol}});

            env(pay(gw, alice, eur(100)));

            AMM const amm(env, gw, usd(1'100), eur(1'000));

            env(pay(alice, carol, usd(100)), Sendmax(eur(100)));

            BEAST_EXPECT(amm.expectBalances(usd(1'000), eur(1'100), amm.tokens()));
            BEAST_EXPECT(env.balance(carol, usd) == usd(100));
            BEAST_EXPECT(env.balance(alice, eur) == eur(0));
        }

        // Payment, two steps
        {
            Env env(*this);

            env.fund(XRP(1'000), gw, alice, carol);
            env.close();

            MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice, carol}});
            MPT const eur = MPTTester({.env = env, .issuer = gw, .holders = {alice, carol}});
            MPT const btc = MPTTester({.env = env, .issuer = gw, .holders = {alice, carol}});
            env(pay(gw, alice, eur(100)));

            AMM const ammEurUsd(env, gw, eur(1'000), usd(1'100));
            AMM const ammUsdBtc(env, gw, usd(1'000), btc(1'100));

            env(pay(alice, carol, btc(100)),
                Sendmax(eur(100)),
                Path(~usd, ~btc),
                Txflags(tfNoRippleDirect));

            BEAST_EXPECT(ammEurUsd.expectBalances(usd(1'000), eur(1'100), ammEurUsd.tokens()));
            BEAST_EXPECT(ammUsdBtc.expectBalances(usd(1'100), btc(1'000), ammUsdBtc.tokens()));
            BEAST_EXPECT(env.balance(carol, btc) == btc(100));
            BEAST_EXPECT(env.balance(alice, eur) == eur(0));
        }

        // Offer crossing
        {
            Env env(*this);

            env.fund(XRP(1'000), gw, alice);
            env.close();

            MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice}});
            MPT const eur = MPTTester({.env = env, .issuer = gw, .holders = {alice}});

            env(pay(gw, alice, eur(1'000)));

            AMM const amm(env, gw, eur(1'000'000), usd(1'001'000));

            env(offer(alice, usd(1'000), eur(1'000)));

            BEAST_EXPECT(amm.expectBalances(usd(1'000'000), eur(1'001'000), amm.tokens()));
            BEAST_EXPECT(env.balance(alice, usd) == usd(1'000));
            BEAST_EXPECT(env.balance(alice, eur) == eur(0));
        }

        {
            Env env(*this);
            env.fund(XRP(1'000'000), gw, alice, carol);

            auto const increment = env.current()->fees().increment;
            auto const txfee = Fee(drops(increment));
            auto const badMPT = MPT(gw, 1'000);

            auto const makeMPT = [&](std::uint32_t const flags,
                                     Holders holders = {},
                                     std::uint64_t const pay = 0,
                                     std::optional<std::uint32_t> const mutableFlags =
                                         std::nullopt) {
                return MPTTester(
                    {.env = env,
                     .issuer = gw,
                     .holders = holders,
                     .pay = pay ? std::optional<std::uint64_t>{pay} : std::nullopt,
                     .flags = flags,
                     .mutableFlags = mutableFlags});
            };

            auto const makeDexMPT = [&](Holders holders = {}, std::uint64_t const pay = 0) {
                return makeMPT(
                    tfMPTCanLock | kMptDexFlags,
                    holders,
                    pay,
                    tmfMPTCanEnableRequireAuth | tmfMPTCanEnableCanTransfer |
                        tmfMPTCanEnableCanTrade);
            };

            auto const makeNoTransferMPT = [&](Holders holders = {}, std::uint64_t const pay = 0) {
                return makeMPT(
                    tfMPTCanLock | tfMPTCanTrade, holders, pay, tmfMPTCanEnableCanTransfer);
            };

            auto const makeNoTradeMPT = [&](Holders holders = {}, std::uint64_t const pay = 0) {
                return makeMPT(
                    tfMPTCanLock | tfMPTCanTransfer, holders, pay, tmfMPTCanEnableCanTrade);
            };

            // AMMCreate
            {
                auto usd = makeDexMPT();
                auto eur = makeDexMPT({alice}, 1'000'000);

                auto createDeleteAMM = [&](auto const& asset, Account const& lp) {
                    AMM amm(
                        env,
                        lp,
                        asset(1'000),
                        eur(1'000),
                        CreateArg{.fee = static_cast<std::uint32_t>(increment.value())});
                    amm.withdrawAll(lp);
                    BEAST_EXPECT(!amm.ammExists());
                };

                auto createFail = [&](auto const& asset, Account const& account, auto const& err) {
                    auto const createJv = AMM::createJv(account, asset(1'000), eur(1'000), 0);
                    env(createJv, txfee, Ter(err));
                    env.close();
                };

                // MPTokenIssuance doesn't exist
                createFail(badMPT, alice, tecOBJECT_NOT_FOUND);

                // MPToken doesn't exist
                createFail(usd, alice, tecNO_AUTH);

                // alice authorizes MPToken, can create
                usd.authorize({.account = alice});
                env(pay(gw, alice, usd(1'000'000)), txfee);
                env.close();
                createDeleteAMM(usd, alice);

                // MPTLock is set
                // alice and issuer can't create
                usd.set({.flags = tfMPTLock});
                createFail(usd, alice, tecLOCKED);
                createFail(usd, gw, tecLOCKED);

                // MPTRequireAuth is set
                // alice is not authorized
                usd.set({.flags = tfMPTUnlock});
                usd.set({.mutableFlags = tmfMPTSetRequireAuth});
                createFail(usd, alice, tecNO_AUTH);
                // issuer can create
                createDeleteAMM(usd, gw);

                // alice is authorized, can create
                usd.authorize({.account = gw, .holder = alice});
                createDeleteAMM(usd, alice);

                // MPTCanTransfer is not set
                {
                    auto usd2 = makeNoTransferMPT({alice}, 1'000'000);

                    // alice can't create
                    createFail(usd2, alice, tecNO_AUTH);
                    // issuer can create
                    createDeleteAMM(usd2, gw);
                    usd2.set({.mutableFlags = tmfMPTSetCanTransfer});
                    // alice can create
                    createDeleteAMM(usd2, alice);
                }

                // MPTCanTrade is not set
                {
                    auto usd3 = makeNoTradeMPT({alice}, 1'000'000);

                    // alice and issuer can't create
                    createFail(usd3, alice, tecNO_PERMISSION);
                    createFail(usd3, gw, tecNO_PERMISSION);
                    usd3.set({.mutableFlags = tmfMPTSetCanTrade});
                    // alice can create
                    createDeleteAMM(usd3, alice);
                }
            }

            // AMMDeposit
            {
                auto usd = makeDexMPT();
                auto eur = makeDexMPT({alice}, 1'000'000);
                AMM amm(env, gw, usd(1'000), eur(1'000));

                // MPTokenIssuance doesn't exist
                amm.deposit(
                    {.account = alice,
                     .asset1In = badMPT(1),
                     .asset2In = eur(1),
                     .assets = std::make_pair(badMPT, eur),
                     .err = Ter(terNO_AMM)});

                // MPToken doesn't exist
                amm.deposit(
                    {.account = carol,
                     .asset1In = usd(1),
                     .asset2In = eur(1),
                     .err = Ter(tecNO_AUTH)});

                // Fund carol for the AMMDeposit checks.
                usd.authorize({.account = carol});
                env(pay(gw, carol, usd(1'000'000)));
                eur.authorize({.account = carol});
                env(pay(gw, carol, eur(1'000'000)));
                env.close();

                // MPTLock is set
                usd.set({.flags = tfMPTLock});

                // alice and issuer can't deposit
                for (auto const& account : {carol, gw})
                {
                    amm.deposit(
                        {.account = account,
                         .asset1In = usd(1),
                         .asset2In = eur(1),
                         .err = Ter(tecLOCKED)});
                    amm.deposit(
                        {.account = account,
                         .asset1In = eur(1),
                         .assets = std::make_pair(eur, usd),
                         .err = Ter(tecLOCKED)});
                }
                usd.set({.flags = tfMPTUnlock});

                // MPTRequireAuth is set
                // carol is not authorized by the issuer
                usd.set({.mutableFlags = tmfMPTSetRequireAuth});
                env.close();
                amm.deposit(
                    {.account = carol,
                     .asset1In = usd(1),
                     .asset2In = eur(1),
                     .err = Ter(tecNO_AUTH)});
                amm.deposit(
                    {.account = carol,
                     .asset1In = eur(1),
                     .assets = std::make_pair(eur, usd),
                     .err = Ter(tecNO_AUTH)});
                // issuer can deposit
                amm.deposit({.account = gw, .tokens = 1'000});
                // carol is authorized, can deposit
                usd.authorize({.account = gw, .holder = carol});
                amm.deposit({.account = carol, .tokens = 1'000});
                // Can't authorize or unauthorize AMM pseudo-account
                usd.authorize(
                    {.account = gw,
                     .holder = Account{"amm", amm.ammAccount()},
                     .err = tecNO_PERMISSION});
                usd.authorize(
                    {.account = gw,
                     .holder = Account{"amm", amm.ammAccount()},
                     .flags = tfMPTUnauthorize,
                     .err = tecNO_PERMISSION});

                // MPTCanTransfer is not set
                {
                    auto usd2 = makeNoTransferMPT({carol}, 1'000'000);
                    AMM amm2(env, gw, usd2(1'000), eur(1'000));

                    // carol can't deposit
                    amm2.deposit(
                        {.account = carol,
                         .asset1In = usd2(1),
                         .asset2In = eur(1),
                         .err = Ter(tecNO_AUTH)});
                    amm2.deposit(
                        {.account = carol,
                         .asset1In = eur(1),
                         .assets = std::make_pair(eur, usd2),
                         .err = Ter(tecNO_AUTH)});
                    // issuer can deposit
                    amm2.deposit({.account = gw, .tokens = 1'000});
                    usd2.set({.mutableFlags = tmfMPTSetCanTransfer});
                    // carol can deposit
                    amm2.deposit({.account = carol, .tokens = 1'000});
                }
            }

            // AMMWithdraw
            {
                auto usd = makeDexMPT();
                auto eur = makeDexMPT({carol}, 1'000'000);
                AMM amm(env, gw, usd(1'000), eur(1'000));

                usd.authorize({.account = carol});
                env(pay(gw, carol, usd(1'000'000)));
                env.close();
                amm.deposit({.account = carol, .tokens = 1'000});

                // MPTokenIssuance doesn't exist
                amm.withdraw(
                    WithdrawArg{
                        .account = carol,
                        .asset1Out = badMPT(1),
                        .asset2Out = eur(1),
                        .assets = std::make_pair(badMPT, eur),
                        .err = Ter(terNO_AMM)});

                // MPToken doesn't exist - doesn't apply since MPToken is created
                // on withdraw in this case

                // MPTLock is set
                usd.set({.flags = tfMPTLock});
                // carol and issuer can't withdraw
                for (auto const& account : {carol, gw})
                {
                    amm.withdraw(
                        {.account = account,
                         .asset1Out = usd(1),
                         .asset2Out = eur(1),
                         .err = Ter(tecLOCKED)});
                    amm.withdraw({.account = account, .tokens = 1'000, .err = Ter(tecLOCKED)});
                    // can single withdraw another asset
                    amm.withdraw(
                        {.account = account,
                         .asset1Out = eur(1),
                         .assets = std::make_pair(eur, usd)});
                }
                usd.set({.flags = tfMPTUnlock});

                // MPTRequireAuth is set
                usd.set({.mutableFlags = tmfMPTSetRequireAuth});
                usd.authorize({.account = gw, .holder = carol, .flags = tfMPTUnauthorize});
                // carol can't withdraw
                amm.withdraw(
                    {.account = carol,
                     .asset1Out = usd(1),
                     .asset2Out = eur(1),
                     .err = Ter(tecNO_AUTH)});
                // can withdraw another asset
                amm.withdraw(
                    {.account = carol, .asset1Out = eur(1), .assets = std::make_pair(eur, usd)});
                // issuer can withdraw
                amm.withdraw({.account = gw, .asset1Out = usd(1), .asset2Out = eur(1)});
                // carol is authorized, can withdraw
                usd.authorize({.account = gw, .holder = carol});
                amm.withdraw({.account = carol, .asset1Out = usd(1), .asset2Out = eur(1)});

                // MPTCanTransfer is not set, allow to withdraw
                {
                    auto usd2 = makeNoTransferMPT({carol}, 1'000'000);
                    AMM amm2(env, gw, usd2(1'000), eur(1'000));

                    // carol cannot deposit usd2 without MPTCanTransfer, so give her
                    // LP tokens directly to test the withdraw path.
                    env.trust(STAmount{amm2.lptIssue(), 1'000}, carol);
                    env(pay(gw, carol, STAmount{amm2.lptIssue(), 100}));
                    env.close();

                    // carol can withdraw
                    amm2.withdraw({.account = carol, .asset1Out = usd2(1), .asset2Out = eur(1)});
                    // can withdraw another asset
                    amm2.withdraw(
                        {.account = carol,
                         .asset1Out = eur(1),
                         .assets = std::make_pair(eur, usd2)});
                    // issuer can withdraw
                    amm2.withdraw({.account = gw, .asset1Out = usd2(1), .asset2Out = eur(1)});
                    // Holder can't transfer to another holder
                    env.fund(XRP(1'000), bob);
                    usd2.authorize({.account = bob});
                    env(pay(carol, bob, usd2(1)), Ter(tecNO_AUTH));
                    usd2.authorize({.account = bob, .flags = tfMPTUnauthorize});
                    // Can redeem
                    env(pay(carol, gw, usd2(1)));
                    usd2.set({.mutableFlags = tmfMPTSetCanTransfer});
                    // carol can withdraw
                    amm2.withdraw({.account = carol, .asset1Out = usd2(1), .asset2Out = eur(1)});
                }

                // MPToken created on withdraw
                {
                    auto usd3 = makeDexMPT();
                    auto eur3 = makeDexMPT({carol}, 1'000'000);
                    AMM amm3(env, gw, usd3(1'000), eur3(1'000));

                    BEAST_EXPECT(env.le(keylet::mptoken(usd3.issuanceID(), carol)) == nullptr);
                    // single-deposit EUR
                    amm3.deposit(
                        {.account = carol,
                         .asset1In = eur3(1'000),
                         .assets = std::make_pair(eur3, usd3)});
                    BEAST_EXPECT(env.le(keylet::mptoken(usd3.issuanceID(), carol)) == nullptr);
                    // withdraw in USD to create MPToken
                    amm3.withdraw({.account = carol, .asset1Out = usd3(100)});
                    BEAST_EXPECT(env.le(keylet::mptoken(usd3.issuanceID(), carol)));
                }
            }
        }
    }

    void
    testFixDoubleOwnerCount(FeatureBitset all)
    {
        testcase("Fix Double adjustOwnerCount in AMMWithdraw");

        using namespace jtx;

        // Carol deposits XRP into an XRP/MPT pool, then withdraws MPT.
        // Carol has no MPToken before the withdrawal. If the bug exists,
        // her ownerCount will be inflated by +1 extra.
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Env env(*this, all);
        env.fund(XRP(30'000), gw, alice, carol);
        env.close();

        // Create MPT with DEX flags. Only alice is a holder initially.
        MPT const btc = MPTTester(
            {.env = env, .issuer = gw, .holders = {alice}, .pay = 20'000, .flags = kMptDexFlags});

        // Alice creates XRP/MPT AMM pool
        AMM amm(env, alice, XRP(10'000), btc(10'000));

        // Carol deposits XRP (single asset) into the pool.
        // Carol gets LP tokens but does NOT have an MPToken yet.
        auto const carolOwnersBefore = ownerCount(env, carol);
        amm.deposit(carol, XRP(1'000), std::nullopt, std::nullopt, tfSingleAsset);
        auto const carolOwnersAfterDeposit = ownerCount(env, carol);
        // Carol should have +1 for LP token trustline
        BEAST_EXPECT(carolOwnersAfterDeposit == carolOwnersBefore + 1);

        auto const carolOwnersBeforeWithdraw = ownerCount(env, carol);
        // Carol withdraws single MPT asset. She doesn't have an MPToken,
        // so one must be created. Bug: ownerCount incremented twice.
        amm.withdraw({.account = carol, .asset1Out = btc(100), .flags = tfSingleAsset});
        auto const carolOwnersAfterWithdraw = ownerCount(env, carol);

        // Expected: +1 for the new MPToken (so total increase = 1)
        BEAST_EXPECT(carolOwnersAfterWithdraw == carolOwnersBeforeWithdraw + 1);
    }

    void
    testTradeAndTransfer()
    {
        using namespace jtx;
        testcase("Trade and Transfer");

        // Verify canMPTTradeAndTransfer validates the flags when from == to and from != to

        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Env env(*this);
        env.fund(XRP(1'000), gw, alice, carol);

        auto const checkCanTradeCanTransfer = [&](std::uint32_t const flags,
                                                  TER const gwToGw,
                                                  TER const gwToAlice,
                                                  TER const aliceToAlice,
                                                  TER const aliceToCarol) {
            MPTTester const mpt(
                {.env = env, .issuer = gw, .holders = {alice, carol}, .pay = 100, .flags = flags});

            BEAST_EXPECT(canMPTTradeAndTransfer(*env.current(), mpt, gw, gw) == gwToGw);
            BEAST_EXPECT(canMPTTradeAndTransfer(*env.current(), mpt, gw, alice) == gwToAlice);
            BEAST_EXPECT(canMPTTradeAndTransfer(*env.current(), mpt, alice, alice) == aliceToAlice);
            BEAST_EXPECT(canMPTTradeAndTransfer(*env.current(), mpt, alice, carol) == aliceToCarol);
        };

        // Both flags are enabled
        checkCanTradeCanTransfer(kMptDexFlags, tesSUCCESS, tesSUCCESS, tesSUCCESS, tesSUCCESS);

        // MPTCanTrade is disabled
        checkCanTradeCanTransfer(
            tfMPTCanTransfer,
            tecNO_PERMISSION,
            tecNO_PERMISSION,
            tecNO_PERMISSION,
            tecNO_PERMISSION);

        // MPTCanTransfer is disabled
        checkCanTradeCanTransfer(tfMPTCanTrade, tesSUCCESS, tesSUCCESS, tecNO_AUTH, tecNO_AUTH);

        // Both flags are disabled
        checkCanTradeCanTransfer(
            0, tecNO_PERMISSION, tecNO_PERMISSION, tecNO_PERMISSION, tecNO_PERMISSION);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{testableAmendments()};

        testMultiSendMaximumAmount(all);
        // MPTokenIssuanceCreate
        testCreateValidation(all - featureSingleAssetVault);
        testCreateValidation(all - featurePermissionedDomains);
        testCreateValidation(all);
        testCreateEnabled(all - featureSingleAssetVault);
        testCreateEnabled(all);

        // MPTokenIssuanceDestroy
        testDestroyValidation(all - featureSingleAssetVault);
        testDestroyValidation(all - featureSingleAssetVault - featureMPTokensV2);
        testDestroyValidation((all | featureSingleAssetVault) - featureMPTokensV2);
        testDestroyValidation(all - featureMPTokensV2);
        testDestroyValidation(all | featureSingleAssetVault);
        testDestroyEnabled(all - featureSingleAssetVault);
        testDestroyEnabled(all - featureSingleAssetVault - featureMPTokensV2);
        testDestroyEnabled((all | featureSingleAssetVault) - featureMPTokensV2);
        testDestroyEnabled(all - featureMPTokensV2);
        testDestroyEnabled(all | featureSingleAssetVault);

        // MPTokenAuthorize
        testAuthorizeValidation(all - featureSingleAssetVault);
        testAuthorizeValidation(all - featureSingleAssetVault - featureMPTokensV2);
        testAuthorizeValidation((all | featureSingleAssetVault) - featureMPTokensV2);
        testAuthorizeValidation(all - featureMPTokensV2);
        testAuthorizeValidation(all | featureSingleAssetVault);
        testAuthorizeEnabled(all - featureSingleAssetVault);
        testAuthorizeEnabled(all - featureSingleAssetVault - featureMPTokensV2);
        testAuthorizeEnabled((all | featureSingleAssetVault) - featureMPTokensV2);
        testAuthorizeEnabled(all - featureMPTokensV2);
        testAuthorizeEnabled(all | featureSingleAssetVault);

        // MPTokenIssuanceSet
        testSetValidation(all - featureSingleAssetVault - featureDynamicMPT);
        testSetValidation(all - featureSingleAssetVault);
        testSetValidation(all - featureDynamicMPT);
        testSetValidation(all - featurePermissionedDomains);
        testSetValidation(all);

        testSetEnabled(all - featureSingleAssetVault);
        testSetEnabled(all);

        // MPT clawback
        testClawbackValidation(all);
        testClawbackValidation(all - featureMPTokensV2);
        testClawback(all);
        testClawback(all - featureMPTokensV2);

        // Test Direct Payment
        testPayment(all);
        testPayment(all | featureSingleAssetVault);
        testPayment((all | featureSingleAssetVault) - featureMPTokensV2);
        testPayment(all - featureMPTokensV2);

        testDepositPreauth(all);
        testDepositPreauth(all - featureCredentials);
        testDepositPreauth(all - featureMPTokensV2);
        testDepositPreauth(all - featureCredentials - featureMPTokensV2);

        // Test MPT Amount is invalid in Tx, which don't support MPT
        testMPTInvalidInTx(all);
        testNonCanonicalMPTAmountCleanup(all);
        // Test parsed MPTokenIssuanceID in API response metadata
        testTxJsonMetaFields(all);

        // Test tokens equality
        testTokensEquality();

        // Test helpers
        testHelperFunctions();

        // Dynamic MPT
        testInvalidCreateDynamic(all);
        testInvalidSetDynamic(all);
        testMutateMPT(all);
        testMutateCanLock(all);
        testMutateRequireAuth(all);
        testMutateCanEscrow(all);
        testMutateCanTransfer(all);
        testMutateCanTransfer(all - featureMPTokensV2);
        testMutateCanClawback(all);

        // Test offer crossing
        testOfferCrossing(all);

        // Test cross asset payment
        testCrossAssetPayment(all);

        // Test path finding
        testPath(all);

        // Test checks
        testCheck(all);

        // Add AMMClawback
        testAMMClawback(all);

        // Test AMM
        testBasicAMM(all);

        // Test Trade/Transfer
        testTradeAndTransfer();

        // Fixes
        testFixDoubleOwnerCount(all);
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(MPToken, app, xrpl, 2);

}  // namespace xrpl::test
