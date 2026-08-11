#include <test/jtx/Account.h>
#include <test/jtx/ConfidentialTransfer.h>
#include <test/jtx/Env.h>
#include <test/jtx/mpt.h>

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace xrpl {

class ConfidentialMPTKeyRotation_test : public ConfidentialTransferTestBase
{
    void
    testMPTokenIssuanceSetRotateIssuerKey(FeatureBitset features)
    {
        testcase("MPTokenIssuanceSet rotate issuer key");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        MPTTester mptAlice(env, alice, {.holders = {bob}});

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance,
        });

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);

        // First-time registration.
        mptAlice.set({
            .account = alice,
            .issuerPubKey = mptAlice.getPubKey(alice),
        });

        // Verify that no epochs are set when registering for the first time.
        {
            auto const sleIssuance = env.le(keylet::mptokenIssuance(mptAlice.issuanceID()));
            BEAST_EXPECT(sleIssuance);
            BEAST_EXPECT(sleIssuance && !sleIssuance->isFieldPresent(sfIssuerKeyEpoch));
            BEAST_EXPECT(sleIssuance && !sleIssuance->isFieldPresent(sfAuditorKeyEpoch));
        }

        // Rotating the issuer key requires the key rotation amendment
        bool const rotationEnabled = features[featureConfidentialMPTKeyRotation];
        mptAlice.set({
            .account = alice,
            .issuerPubKey = mptAlice.getPubKey(bob),
            .err = rotationEnabled ? TER(tesSUCCESS) : TER(tecNO_PERMISSION),
        });

        {
            auto const sleIssuance = env.le(keylet::mptokenIssuance(mptAlice.issuanceID()));
            if (!BEAST_EXPECT(sleIssuance))
                return;

            auto const expectedKey =
                rotationEnabled ? mptAlice.getPubKey(bob) : mptAlice.getPubKey(alice);
            BEAST_EXPECT(
                expectedKey &&
                strHex((*sleIssuance)[sfIssuerEncryptionKey]) == strHex(*expectedKey));

            // Rotating the issuer key bumps the epoch.
            if (rotationEnabled)
                BEAST_EXPECT((*sleIssuance)[~sfIssuerKeyEpoch] == 1u);
            else
                BEAST_EXPECT(!sleIssuance->isFieldPresent(sfIssuerKeyEpoch));
            BEAST_EXPECT(!sleIssuance->isFieldPresent(sfAuditorKeyEpoch));
        }

        if (rotationEnabled)
        {
            // A second rotation increments the epoch again
            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(alice),
            });

            auto const sleIssuance = env.le(keylet::mptokenIssuance(mptAlice.issuanceID()));
            BEAST_EXPECT(sleIssuance && (*sleIssuance)[~sfIssuerKeyEpoch] == 2u);
        }
    }

    void
    testMPTokenIssuanceSetRotateBothKeys(FeatureBitset features)
    {
        testcase("MPTokenIssuanceSet rotate both issuer and auditor keys");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const auditor("auditor");
        MPTTester mptAlice(env, alice, {.holders = {bob}});

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance,
        });

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(auditor);

        // Register both keys together.
        mptAlice.set({
            .account = alice,
            .issuerPubKey = mptAlice.getPubKey(alice),
            .auditorPubKey = mptAlice.getPubKey(auditor),
        });

        //  Verify that no epochs are set when registering for the first time.
        {
            auto const sleIssuance = env.le(keylet::mptokenIssuance(mptAlice.issuanceID()));
            BEAST_EXPECT(sleIssuance && !sleIssuance->isFieldPresent(sfIssuerKeyEpoch));
            BEAST_EXPECT(sleIssuance && !sleIssuance->isFieldPresent(sfAuditorKeyEpoch));
        }

        // Rotating both keys, it requires the amendment
        bool const rotationEnabled = features[featureConfidentialMPTKeyRotation];
        mptAlice.set({
            .account = alice,
            .issuerPubKey = mptAlice.getPubKey(bob),
            .auditorPubKey = mptAlice.getPubKey(alice),
            .err = rotationEnabled ? TER(tesSUCCESS) : TER(tecNO_PERMISSION),
        });

        auto const sleIssuance = env.le(keylet::mptokenIssuance(mptAlice.issuanceID()));
        if (!BEAST_EXPECT(sleIssuance))
            return;

        auto const expectedIssuerKey =
            rotationEnabled ? mptAlice.getPubKey(bob) : mptAlice.getPubKey(alice);
        auto const expectedAuditorKey =
            rotationEnabled ? mptAlice.getPubKey(alice) : mptAlice.getPubKey(auditor);
        BEAST_EXPECT(
            expectedIssuerKey &&
            strHex((*sleIssuance)[sfIssuerEncryptionKey]) == strHex(*expectedIssuerKey));
        BEAST_EXPECT(
            expectedAuditorKey &&
            strHex((*sleIssuance)[sfAuditorEncryptionKey]) == strHex(*expectedAuditorKey));
        if (rotationEnabled)
        {
            BEAST_EXPECT((*sleIssuance)[~sfIssuerKeyEpoch] == 1u);
            BEAST_EXPECT((*sleIssuance)[~sfAuditorKeyEpoch] == 1u);
        }
        else
        {
            BEAST_EXPECT(!sleIssuance->isFieldPresent(sfIssuerKeyEpoch));
            BEAST_EXPECT(!sleIssuance->isFieldPresent(sfAuditorKeyEpoch));
        }

        if (rotationEnabled)
        {
            // A second rotation increments both epochs again
            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(alice),
                .auditorPubKey = mptAlice.getPubKey(auditor),
            });

            auto const sleIssuance = env.le(keylet::mptokenIssuance(mptAlice.issuanceID()));
            BEAST_EXPECT(sleIssuance && (*sleIssuance)[~sfIssuerKeyEpoch] == 2u);
            BEAST_EXPECT(sleIssuance && (*sleIssuance)[~sfAuditorKeyEpoch] == 2u);
        }
    }

    void
    testMPTokenIssuanceSetRotateAuditorKeyOnly(FeatureBitset features)
    {
        testcase("MPTokenIssuanceSet rotate auditor key only");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const auditor("auditor");
        MPTTester mptAlice(env, alice, {.holders = {bob}});

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance,
        });

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(auditor);

        // Register both keys together.
        mptAlice.set({
            .account = alice,
            .issuerPubKey = mptAlice.getPubKey(alice),
            .auditorPubKey = mptAlice.getPubKey(auditor),
        });

        // A transaction carrying only the auditor key fails preflight
        // pre-ConfidentialMPTKeyRotation; post-ConfidentialMPTKeyRotation it rotates the auditor
        // key
        bool const rotationEnabled = features[featureConfidentialMPTKeyRotation];
        mptAlice.set({
            .account = alice,
            .auditorPubKey = mptAlice.getPubKey(bob),
            .err = rotationEnabled ? TER(tesSUCCESS) : TER(temMALFORMED),
        });

        auto const sleIssuance = env.le(keylet::mptokenIssuance(mptAlice.issuanceID()));
        if (!BEAST_EXPECT(sleIssuance))
            return;

        // The issuer key keeps unchanged.
        auto const issuerKey = mptAlice.getPubKey(alice);
        BEAST_EXPECT(
            issuerKey && strHex((*sleIssuance)[sfIssuerEncryptionKey]) == strHex(*issuerKey));
        BEAST_EXPECT(!sleIssuance->isFieldPresent(sfIssuerKeyEpoch));

        auto const expectedAuditorKey =
            rotationEnabled ? mptAlice.getPubKey(bob) : mptAlice.getPubKey(auditor);
        BEAST_EXPECT(
            expectedAuditorKey &&
            strHex((*sleIssuance)[sfAuditorEncryptionKey]) == strHex(*expectedAuditorKey));

        // Rotating the auditor key bumps its epoch.
        if (rotationEnabled)
            BEAST_EXPECT((*sleIssuance)[~sfAuditorKeyEpoch] == 1u);
        else
            BEAST_EXPECT(!sleIssuance->isFieldPresent(sfAuditorKeyEpoch));

        if (rotationEnabled)
        {
            // A second rotation increments the epoch again
            mptAlice.set({
                .account = alice,
                .auditorPubKey = mptAlice.getPubKey(auditor),
            });

            auto const sleIssuance = env.le(keylet::mptokenIssuance(mptAlice.issuanceID()));
            BEAST_EXPECT(sleIssuance && (*sleIssuance)[~sfAuditorKeyEpoch] == 2u);

            // The issuer key epoch is still untouched.
            BEAST_EXPECT(sleIssuance && !sleIssuance->isFieldPresent(sfIssuerKeyEpoch));
        }
    }

    void
    testMPTokenIssuanceSetRegisterAuditorKeyLater(FeatureBitset features)
    {
        testcase("MPTokenIssuanceSet register auditor key after issuer key");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const auditor("auditor");
        MPTTester mptAlice(env, alice);

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance,
        });

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(auditor);

        // Register the issuer key first. We'll register the auditor key in a separate transaction.
        mptAlice.set({
            .account = alice,
            .issuerPubKey = mptAlice.getPubKey(alice),
        });

        // Register the auditor key separately.
        // pre-ConfidentialMPTKeyRotation it fails preflight; post-ConfidentialMPTKeyRotation it
        // succeeds without touching any epoch because it's a first-time registration.
        bool const rotationEnabled = features[featureConfidentialMPTKeyRotation];
        mptAlice.set({
            .account = alice,
            .auditorPubKey = mptAlice.getPubKey(auditor),
            .err = rotationEnabled ? TER(tesSUCCESS) : TER(temMALFORMED),
        });

        auto const sleIssuance = env.le(keylet::mptokenIssuance(mptAlice.issuanceID()));
        if (!BEAST_EXPECT(sleIssuance))
            return;
        BEAST_EXPECT(sleIssuance->isFieldPresent(sfAuditorEncryptionKey) == rotationEnabled);
        BEAST_EXPECT(!sleIssuance->isFieldPresent(sfIssuerKeyEpoch));
        BEAST_EXPECT(!sleIssuance->isFieldPresent(sfAuditorKeyEpoch));
    }

    void
    testMPTokenIssuanceSetRegisterAuditorKeyLaterWithCOA(FeatureBitset features)
    {
        testcase("MPTokenIssuanceSet register auditor key later with circulating supply");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const auditor("auditor");
        MPTTester mptAlice(env, alice, {.holders = {bob}});

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance,
        });

        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 100);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(auditor);

        mptAlice.set({
            .account = alice,
            .issuerPubKey = mptAlice.getPubKey(alice),
        });

        // Convert some of bob's balance so that COA > 0
        mptAlice.convert({
            .account = bob,
            .amt = 50,
            .holderPubKey = mptAlice.getPubKey(bob),
        });

        auto const sleIssuanceBefore = env.le(keylet::mptokenIssuance(mptAlice.issuanceID()));
        if (!BEAST_EXPECT(sleIssuanceBefore))
            return;
        auto const coaBefore = (*sleIssuanceBefore)[~sfConfidentialOutstandingAmount].value_or(0);
        BEAST_EXPECT(coaBefore > 0);

        // Registering the auditor key for the first time while confidential
        // supply is circulating: pre-ConfidentialMPTKeyRotation an auditor-only
        // transaction fails preflight; post-ConfidentialMPTKeyRotation it
        // succeeds as a first-time late-registration even COA > 0.
        bool const rotationEnabled = features[featureConfidentialMPTKeyRotation];
        mptAlice.set({
            .account = alice,
            .auditorPubKey = mptAlice.getPubKey(auditor),
            .err = rotationEnabled ? TER(tesSUCCESS) : TER(temMALFORMED),
        });

        auto const sleIssuance = env.le(keylet::mptokenIssuance(mptAlice.issuanceID()));
        if (!BEAST_EXPECT(sleIssuance))
            return;
        BEAST_EXPECT(sleIssuance->isFieldPresent(sfAuditorEncryptionKey) == rotationEnabled);
        BEAST_EXPECT(!sleIssuance->isFieldPresent(sfIssuerKeyEpoch));
        BEAST_EXPECT(!sleIssuance->isFieldPresent(sfAuditorKeyEpoch));

        // The circulating supply itself is not affected.
        BEAST_EXPECT((*sleIssuance)[~sfConfidentialOutstandingAmount].value_or(0) == coaBefore);
    }

    void
    testMPTokenIssuanceSetAuditorKeyWithoutIssuerKey(FeatureBitset features)
    {
        testcase("MPTokenIssuanceSet auditor key requires issuer key");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const auditor("auditor");
        MPTTester mptAlice(env, alice);

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance,
        });

        mptAlice.generateKeyPair(auditor);
        // The issuer key was never registered. pre-ConfidentialMPTKeyRotation an auditor-only
        // transaction fails preflight; post-ConfidentialMPTKeyRotation it passes preflight
        // but preclaim rejects registering an auditor key on an issuance
        // without an issuer key.
        bool const rotationEnabled = features[featureConfidentialMPTKeyRotation];
        mptAlice.set({
            .account = alice,
            .auditorPubKey = mptAlice.getPubKey(auditor),
            .err = rotationEnabled ? TER(tecNO_PERMISSION) : TER(temMALFORMED),
        });

        auto const sleIssuance = env.le(keylet::mptokenIssuance(mptAlice.issuanceID()));
        BEAST_EXPECT(sleIssuance && !sleIssuance->isFieldPresent(sfAuditorEncryptionKey));
    }

    void
    testMPTokenIssuanceSetRotateWithCOA(FeatureBitset features)
    {
        testcase("MPTokenIssuanceSet rotate with circulating confidential supply");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        MPTTester mptAlice(env, alice, {.holders = {bob}});

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance,
        });

        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 100);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);

        mptAlice.set({
            .account = alice,
            .issuerPubKey = mptAlice.getPubKey(alice),
        });

        // Convert some of bob's balance to confidential spending, so that the
        // issuance has confidential supply. COA > 0.
        mptAlice.convert({
            .account = bob,
            .amt = 50,
            .holderPubKey = mptAlice.getPubKey(bob),
        });

        auto const sleIssuanceBeforeRotation =
            env.le(keylet::mptokenIssuance(mptAlice.issuanceID()));
        if (!BEAST_EXPECT(sleIssuanceBeforeRotation))
            return;
        auto const coaBeforeRotation =
            (*sleIssuanceBeforeRotation)[~sfConfidentialOutstandingAmount].value_or(0);
        BEAST_EXPECT(coaBeforeRotation > 0);

        // Rotating key requires the
        // amendment.
        bool const rotationEnabled = features[featureConfidentialMPTKeyRotation];
        mptAlice.set({
            .account = alice,
            .issuerPubKey = mptAlice.getPubKey(carol),
            .err = rotationEnabled ? TER(tesSUCCESS) : TER(tecNO_PERMISSION),
        });

        auto const sleIssuance = env.le(keylet::mptokenIssuance(mptAlice.issuanceID()));
        if (!BEAST_EXPECT(sleIssuance))
            return;
        auto const expectedKey =
            rotationEnabled ? mptAlice.getPubKey(carol) : mptAlice.getPubKey(alice);
        BEAST_EXPECT(
            expectedKey && strHex((*sleIssuance)[sfIssuerEncryptionKey]) == strHex(*expectedKey));
        if (rotationEnabled)
            BEAST_EXPECT((*sleIssuance)[~sfIssuerKeyEpoch] == 1u);
        else
            BEAST_EXPECT(!sleIssuance->isFieldPresent(sfIssuerKeyEpoch));

        // The confidential outstanding amount is not affected by the rotation
        BEAST_EXPECT(
            (*sleIssuance)[~sfConfidentialOutstandingAmount].value_or(0) == coaBeforeRotation);

        // Re-enabling confidential balances while supply is circulating is
        // rejected regardless of the ConfidentialMPTKeyRotation amendment.
        mptAlice.set({
            .account = alice,
            .mutableFlags = tmfMPTSetCanHoldConfidentialBalance,
            .err = tecNO_PERMISSION,
        });
    }

    void
    testMPTokenIssuanceSetWithFeats(FeatureBitset features)
    {
        testMPTokenIssuanceSetRotateIssuerKey(features);
        testMPTokenIssuanceSetRotateBothKeys(features);
        testMPTokenIssuanceSetRotateAuditorKeyOnly(features);
        testMPTokenIssuanceSetRegisterAuditorKeyLater(features);
        testMPTokenIssuanceSetRegisterAuditorKeyLaterWithCOA(features);
        testMPTokenIssuanceSetAuditorKeyWithoutIssuerKey(features);
        testMPTokenIssuanceSetRotateWithCOA(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{testableAmendments()};

        testMPTokenIssuanceSetWithFeats(all);
        testMPTokenIssuanceSetWithFeats(all - featureConfidentialMPTKeyRotation);
    }
};

BEAST_DEFINE_TESTSUITE(ConfidentialMPTKeyRotation, app, xrpl);

}  // namespace xrpl
