#include <test/jtx/Account.h>
#include <test/jtx/ConfidentialTransfer.h>
#include <test/jtx/Env.h>
#include <test/jtx/mpt.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <cstdint>
#include <memory>
#include <optional>

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
        BEAST_EXPECT(mptAlice.checkKeyEpochs(std::nullopt, std::nullopt));

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

            // Rotating the issuer key bumps its epoch. The auditor key was
            // never registered, so its epoch stays absent.
            if (rotationEnabled)
                BEAST_EXPECT(mptAlice.checkKeyEpochs(1u, std::nullopt));
            else
                BEAST_EXPECT(mptAlice.checkKeyEpochs(std::nullopt, std::nullopt));
        }

        if (rotationEnabled)
        {
            // A second rotation increments the epoch again
            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(alice),
            });

            BEAST_EXPECT(mptAlice.checkKeyEpochs(2u, std::nullopt));
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
        BEAST_EXPECT(mptAlice.checkKeyEpochs(std::nullopt, std::nullopt));

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
            BEAST_EXPECT(mptAlice.checkKeyEpochs(1u, 1u));
        else
            BEAST_EXPECT(mptAlice.checkKeyEpochs(std::nullopt, std::nullopt));

        if (rotationEnabled)
        {
            // Rotating the issuer key to its current value fails.
            // Current issuer key is bob, duplicate.
            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(bob),
                .err = tecDUPLICATE,
            });

            // Rotating the auditor key to its current value fails.
            // Current auditor key is alice, duplicate.
            mptAlice.set({
                .account = alice,
                .auditorPubKey = mptAlice.getPubKey(alice),
                .err = tecDUPLICATE,
            });

            // The whole transaction fails when one key is unchanged, even if
            // the other key is rotated to a new value.
            // Current issuer key is bob, duplicate.
            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(bob),
                .auditorPubKey = mptAlice.getPubKey(auditor),
                .err = tecDUPLICATE,
            });

            // Current auditor key is alice, duplicate.
            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(auditor),
                .auditorPubKey = mptAlice.getPubKey(alice),
                .err = tecDUPLICATE,
            });

            // Nothing changed: keys and epochs are untouched
            BEAST_EXPECT(mptAlice.checkKeyEpochs(1u, 1u));

            // A second rotation increments both epochs again
            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(alice),
                .auditorPubKey = mptAlice.getPubKey(auditor),
            });

            BEAST_EXPECT(mptAlice.checkKeyEpochs(2u, 2u));
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

        auto const expectedAuditorKey =
            rotationEnabled ? mptAlice.getPubKey(bob) : mptAlice.getPubKey(auditor);
        BEAST_EXPECT(
            expectedAuditorKey &&
            strHex((*sleIssuance)[sfAuditorEncryptionKey]) == strHex(*expectedAuditorKey));

        // Rotating the auditor key bumps its epoch, and never registering the
        // issuer key leaves the issuer epoch absent.
        if (rotationEnabled)
            BEAST_EXPECT(mptAlice.checkKeyEpochs(std::nullopt, 1u));
        else
            BEAST_EXPECT(mptAlice.checkKeyEpochs(std::nullopt, std::nullopt));

        if (rotationEnabled)
        {
            // A second rotation increments the epoch again
            mptAlice.set({
                .account = alice,
                .auditorPubKey = mptAlice.getPubKey(auditor),
            });

            // The issuer key epoch is still untouched.
            BEAST_EXPECT(mptAlice.checkKeyEpochs(std::nullopt, 2u));
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
        BEAST_EXPECT(mptAlice.checkKeyEpochs(std::nullopt, std::nullopt));
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
        BEAST_EXPECT(mptAlice.checkKeyEpochs(std::nullopt, std::nullopt));

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
            BEAST_EXPECT(mptAlice.checkKeyEpochs(1u, std::nullopt));
        else
            BEAST_EXPECT(mptAlice.checkKeyEpochs(std::nullopt, std::nullopt));

        // The confidential outstanding amount is not affected by the rotation
        BEAST_EXPECT(
            (*sleIssuance)[~sfConfidentialOutstandingAmount].value_or(0) == coaBeforeRotation);

        // Re-enabling confidential balances while supply is circulating is
        // rejected regardless of the ConfidentialMPTKeyRotation amendment.
        mptAlice.set({
            .account = alice,
            .flags = tfMPTSetCanHoldConfidentialBalance,
            .err = tecNO_PERMISSION,
        });
    }

    void
    testMPTokenIssuanceSetKeyEpochAtMax(FeatureBitset features)
    {
        using namespace test::jtx;

        // Without the amendment the keys cannot be rotated at all, so there is
        // no epoch to exhaust. Bail out before opening the testcase, otherwise
        // it records no conditions and trips the runner's "forgot to call pass
        // or fail" assertion.
        if (!features[featureConfidentialMPTKeyRotation])
            return;

        testcase("MPTokenIssuanceSet key epoch cannot wrap");

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const auditor("auditor");

        // Keep the ledger open so that we can write the key epochs directly into it.
        MPTTester mptAlice(env, alice, {.holders = {bob}, .close = false});

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance,
        });

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);
        mptAlice.generateKeyPair(auditor);

        mptAlice.set({
            .account = alice,
            .issuerPubKey = mptAlice.getPubKey(alice),
            .auditorPubKey = mptAlice.getPubKey(auditor),
        });

        auto const issuanceKeylet = keylet::mptokenIssuance(mptAlice.issuanceID());

        // Writes the supplied key epochs straight into the open ledger so that
        // the maximum epoch is reachable without submitting four billion
        // rotations.
        auto setEpochs = [&](std::optional<std::uint32_t> const& issuerKeyEpoch,
                             std::optional<std::uint32_t> const& auditorKeyEpoch) {
            env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) {
                auto const sle = view.read(issuanceKeylet);
                if (!sle)
                    return false;  // LCOV_EXCL_LINE

                auto replacement = std::make_shared<SLE>(*sle);
                if (issuerKeyEpoch)
                    (*replacement)[sfIssuerKeyEpoch] = *issuerKeyEpoch;
                if (auditorKeyEpoch)
                    (*replacement)[sfAuditorKeyEpoch] = *auditorKeyEpoch;
                view.rawReplace(replacement);
                return true;
            });
        };

        // Increment the auditor epoch to kMaxKeyEpoch - 1, leaving the issuer epoch absent.
        setEpochs(std::nullopt, kMaxKeyEpoch - 1);
        BEAST_EXPECT(mptAlice.checkKeyEpochs(std::nullopt, kMaxKeyEpoch - 1));

        // Rotating the auditor key to kMaxKeyEpoch succeeds.
        mptAlice.set({
            .account = alice,
            .auditorPubKey = mptAlice.getPubKey(carol),
        });

        BEAST_EXPECT(mptAlice.checkKeyEpochs(std::nullopt, kMaxKeyEpoch));

        // A further auditor rotation is rejected because the epoch is exhausted.
        mptAlice.set({
            .account = alice,
            .auditorPubKey = mptAlice.getPubKey(bob),
            .err = tecNO_PERMISSION,
        });

        // Rotating both keys at once is rejected as a whole because the auditor
        // epoch is exhausted.
        mptAlice.set({
            .account = alice,
            .issuerPubKey = mptAlice.getPubKey(auditor),
            .auditorPubKey = mptAlice.getPubKey(bob),
            .err = tecNO_PERMISSION,
        });

        // The issuer key is unaffected by the exhausted auditor epoch.
        mptAlice.set({
            .account = alice,
            .issuerPubKey = mptAlice.getPubKey(bob),
        });

        BEAST_EXPECT(mptAlice.checkKeyEpochs(1u, kMaxKeyEpoch));

        // Increment the issuer epoch to kMaxKeyEpoch - 1.
        setEpochs(kMaxKeyEpoch - 1, std::nullopt);
        BEAST_EXPECT(mptAlice.checkKeyEpochs(kMaxKeyEpoch - 1, kMaxKeyEpoch));

        // Rotating the issuer key to kMaxKeyEpoch succeeds.
        mptAlice.set({
            .account = alice,
            .issuerPubKey = mptAlice.getPubKey(carol),
        });

        BEAST_EXPECT(mptAlice.checkKeyEpochs(kMaxKeyEpoch, kMaxKeyEpoch));

        // With both epochs exhausted neither key can be rotated again.
        mptAlice.set({
            .account = alice,
            .issuerPubKey = mptAlice.getPubKey(alice),
            .err = tecNO_PERMISSION,
        });
        mptAlice.set({
            .account = alice,
            .auditorPubKey = mptAlice.getPubKey(bob),
            .err = tecNO_PERMISSION,
        });
        mptAlice.set({
            .account = alice,
            .issuerPubKey = mptAlice.getPubKey(alice),
            .auditorPubKey = mptAlice.getPubKey(bob),
            .err = tecNO_PERMISSION,
        });

        BEAST_EXPECT(mptAlice.checkKeyEpochs(kMaxKeyEpoch, kMaxKeyEpoch));
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
        testMPTokenIssuanceSetKeyEpochAtMax(features);
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
