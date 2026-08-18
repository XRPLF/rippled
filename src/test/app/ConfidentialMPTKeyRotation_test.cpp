#include <test/jtx/Account.h>
#include <test/jtx/ConfidentialTransfer.h>
#include <test/jtx/Env.h>
#include <test/jtx/mpt.h>

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <cstdint>

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
            {
                BEAST_EXPECT((*sleIssuance)[~sfIssuerKeyEpoch] == 1u);
            }
            else
            {
                BEAST_EXPECT(!sleIssuance->isFieldPresent(sfIssuerKeyEpoch));
            }
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
            {
                auto const sleIssuance = env.le(keylet::mptokenIssuance(mptAlice.issuanceID()));
                BEAST_EXPECT(sleIssuance && (*sleIssuance)[~sfIssuerKeyEpoch] == 1u);
                BEAST_EXPECT(sleIssuance && (*sleIssuance)[~sfAuditorKeyEpoch] == 1u);
            }

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
        {
            BEAST_EXPECT((*sleIssuance)[~sfAuditorKeyEpoch] == 1u);
        }
        else
        {
            BEAST_EXPECT(!sleIssuance->isFieldPresent(sfAuditorKeyEpoch));
        }

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
        {
            BEAST_EXPECT((*sleIssuance)[~sfIssuerKeyEpoch] == 1u);
        }
        else
        {
            BEAST_EXPECT(!sleIssuance->isFieldPresent(sfIssuerKeyEpoch));
        }

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
    testConfidentialMPTMirrorUpdatePreflight(FeatureBitset features)
    {
        testcase("ConfidentialMPTMirrorUpdate preflight");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

        // A well-formed 66-byte ElGamal ciphertext
        Buffer const& validCipher = getTrivialCiphertext();

        // Both amendments are required: ConfidentialMPTKeyRotation and ConfidentialTransfer.
        if (!features[featureConfidentialMPTKeyRotation] || !features[featureConfidentialTransfer])
        {
            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer});
            mptAlice.mirrorUpdate({
                .account = bob,
                .issuerEncryptedAmount = validCipher,
                .err = temDISABLED,
            });
            return;
        }

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance,
        });
        mptAlice.generateKeyPair(alice);

        // A valid EC point
        auto const validKey = mptAlice.getPubKey(alice);

        // Issuer mode but account is not the issuer.
        mptAlice.mirrorUpdate({
            .account = bob,
            .holder = carol,
            .issuerEncryptedAmount = validCipher,
            .previousIssuerKey = validKey,
            .err = temMALFORMED,
        });

        // Issuer mode but the holder is the same as the issuer.
        mptAlice.mirrorUpdate({
            .account = alice,
            .holder = alice,
            .issuerEncryptedAmount = validCipher,
            .previousIssuerKey = validKey,
            .err = temMALFORMED,
        });

        // Issuer mode but holder is not provided.
        mptAlice.mirrorUpdate({
            .account = alice,
            .issuerEncryptedAmount = validCipher,
            .err = temMALFORMED,
        });

        // At least one of issuer or auditor amount must be present.
        mptAlice.mirrorUpdate({
            .account = alice,
            .holder = bob,
            .err = temMALFORMED,
        });

        // Holder mode, previousIssuerKey should not be present.
        mptAlice.mirrorUpdate({
            .account = bob,
            .issuerEncryptedAmount = validCipher,
            .previousIssuerKey = validKey,
            .err = temMALFORMED,
        });

        // Issuer mode, previousIssuerKey is provided but issuerEncryptedAmount is missing.
        mptAlice.mirrorUpdate({
            .account = alice,
            .holder = bob,
            .auditorEncryptedAmount = validCipher,
            .previousIssuerKey = validKey,
            .err = temMALFORMED,
        });

        // Issuer mode, issuerEncryptedAmount is present but previousIssuerKey is missing.
        mptAlice.mirrorUpdate({
            .account = alice,
            .holder = bob,
            .issuerEncryptedAmount = validCipher,
            .err = temMALFORMED,
        });

        // Issuer amount has the wrong length.
        mptAlice.mirrorUpdate({
            .account = alice,
            .holder = bob,
            .issuerEncryptedAmount = gMakeZeroBuffer(10),
            .previousIssuerKey = validKey,
            .err = temBAD_CIPHERTEXT,
        });

        // Auditor amount has the wrong length.
        mptAlice.mirrorUpdate({
            .account = alice,
            .holder = bob,
            .auditorEncryptedAmount = gMakeZeroBuffer(10),
            .err = temBAD_CIPHERTEXT,
        });

        // Issuer amount is the right length but not a valid ciphertext.
        mptAlice.mirrorUpdate({
            .account = alice,
            .holder = bob,
            .issuerEncryptedAmount = getBadCiphertext(),
            .previousIssuerKey = validKey,
            .err = temBAD_CIPHERTEXT,
        });

        // Auditor amount is the right length but not a valid ciphertext.
        mptAlice.mirrorUpdate({
            .account = alice,
            .holder = bob,
            .issuerEncryptedAmount = validCipher,
            .auditorEncryptedAmount = getBadCiphertext(),
            .previousIssuerKey = validKey,
            .err = temBAD_CIPHERTEXT,
        });

        // previousIssuerKey is present but not a valid EC point.
        mptAlice.mirrorUpdate({
            .account = alice,
            .holder = bob,
            .issuerEncryptedAmount = validCipher,
            .previousIssuerKey = gMakeZeroBuffer(kEcPubKeyLength),
            .err = temMALFORMED,
        });
    }

    void
    testConfidentialMPTMirrorUpdatePreclaim(FeatureBitset features)
    {
        testcase("ConfidentialMPTMirrorUpdate preclaim");
        using namespace test::jtx;

        Buffer const& validCipher = getTrivialCiphertext();
        Buffer const& validKey = getTrivialCommitment();

        // The issuance does not exist.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance,
            });
            // Destroy the issuance to test issuance not found.
            mptAlice.destroy();

            mptAlice.mirrorUpdate({
                .account = bob,
                .issuerEncryptedAmount = validCipher,
                .err = tecOBJECT_NOT_FOUND,
            });
        }

        // The issuance have not enabled confidential balances.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});
            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer});

            mptAlice.mirrorUpdate({
                .account = alice,
                .holder = bob,
                .issuerEncryptedAmount = validCipher,
                .previousIssuerKey = validKey,
                .err = tecNO_PERMISSION,
            });
        }

        // The issuer encryption key was not already registered.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});
            mptAlice.create(
                {.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance});
            mptAlice.authorize({.account = bob});

            mptAlice.mirrorUpdate({
                .account = alice,
                .holder = bob,
                .issuerEncryptedAmount = validCipher,
                .previousIssuerKey = validKey,
                .err = tecNO_PERMISSION,
            });
        }

        // In issuer mode, the specified holder account does not exist.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const carol("carol");
            MPTTester mptAlice(env, alice);
            mptAlice.create(
                {.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance});
            mptAlice.generateKeyPair(alice);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            // Carol never got funded so it does not exist.
            mptAlice.mirrorUpdate({
                .account = alice,
                .holder = carol,
                .issuerEncryptedAmount = validCipher,
                .previousIssuerKey = validKey,
                .err = tecNO_TARGET,
            });
        }

        // The holder's MPToken does not exist (holder never authorized).
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});
            mptAlice.create(
                {.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance});
            mptAlice.generateKeyPair(alice);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.mirrorUpdate({
                .account = alice,
                .holder = bob,
                .issuerEncryptedAmount = validCipher,
                .previousIssuerKey = validKey,
                .err = tecOBJECT_NOT_FOUND,
            });
        }

        // The holder has an MPToken but no confidential issuer balance (sfIssuerEncryptedBalance).
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});
            mptAlice.create(
                {.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance});
            mptAlice.authorize({.account = bob});
            mptAlice.generateKeyPair(alice);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.mirrorUpdate({
                .account = alice,
                .holder = bob,
                .issuerEncryptedAmount = validCipher,
                .previousIssuerKey = validKey,
                .err = tecNO_PERMISSION,
            });
        }

        // Auditor mirror migration on an issuance with no auditor key.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");

            // This setup has issuer key but no auditor key.
            ConfidentialEnv ct{env, alice, {{.account = bob}}};

            ct.mpt.mirrorUpdate({
                .account = alice,
                .holder = bob,
                .auditorEncryptedAmount = validCipher,
                .err = tecNO_PERMISSION,
            });
        }

        // Issuer mirror is already most up-to-date so
        // there is nothing to migrate.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            ConfidentialEnv ct{env, alice, {{.account = bob}}};

            ct.mpt.mirrorUpdate({
                .account = alice,
                .holder = bob,
                .issuerEncryptedAmount = validCipher,
                .previousIssuerKey = validKey,
                .err = tecNO_PERMISSION,
            });
        }

        // Issuer-mode auditor-only migration while the issuer mirror is stale:
        // the issuer mirror must be brought up to date before the auditor
        // mirror can be migrated.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const auditor("auditor");
            Account const newIssuerKey("newIssuerKey");

            // Issuance has both an issuer key and an auditor key, and bob holds
            // both mirrors at epoch 0.
            ConfidentialEnv ct{
                env,
                alice,
                {{.account = bob}},
                tfMPTCanHoldConfidentialBalance | tfMPTCanTransfer,
                auditor};

            // Rotate the issuer key: issuer key epoch 0 -> 1, while bob's
            // issuer-mirror epoch stays 0 (stale).
            ct.mpt.generateKeyPair(newIssuerKey);
            ct.mpt.set({.account = alice, .issuerPubKey = ct.mpt.getPubKey(newIssuerKey)});

            ct.mpt.mirrorUpdate({
                .account = alice,
                .holder = bob,
                .auditorEncryptedAmount = validCipher,
                .err = tecNO_PERMISSION,
            });
        }

        // Auditor mirror is already current (the auditor key has not rotated),
        // so there is nothing to migrate.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const auditor("auditor");

            // Issuance has both keys and bob holds both mirrors at epoch 0.
            ConfidentialEnv ct{
                env,
                alice,
                {{.account = bob}},
                tfMPTCanHoldConfidentialBalance | tfMPTCanTransfer,
                auditor};

            // No key has rotated, so the auditor mirror is up to date
            // so there is nothing to migrate.
            ct.mpt.mirrorUpdate({
                .account = alice,
                .holder = bob,
                .auditorEncryptedAmount = validCipher,
                .err = tecNO_PERMISSION,
            });
        }

        // In an issuer-mode simultaneous migration, both mirrors must be stale. Here
        // only the issuer key has rotated so its mirror is stale but the auditor mirror is not.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const auditor("auditor");
            Account const newIssuerKey("newIssuerKey");

            ConfidentialEnv ct{
                env,
                alice,
                {{.account = bob}},
                tfMPTCanHoldConfidentialBalance | tfMPTCanTransfer,
                auditor};

            // Rotate only the issuer key: issuer key epoch 0 -> 1, auditor key
            // epoch stays 0. The issuer mirror is now stale but the auditor
            // mirror is still current.
            ct.mpt.generateKeyPair(newIssuerKey);
            ct.mpt.set({.account = alice, .issuerPubKey = ct.mpt.getPubKey(newIssuerKey)});

            ct.mpt.mirrorUpdate({
                .account = alice,
                .holder = bob,
                .issuerEncryptedAmount = validCipher,
                .auditorEncryptedAmount = validCipher,
                .previousIssuerKey = validKey,
                .err = tecNO_PERMISSION,
            });
        }

        // In an issuer-mode simultaneous migration, both mirrors must be stale.
        // Here only the auditor key has rotated so its mirror is stale but the
        // issuer mirror is not.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const auditor("auditor");
            Account const newAuditorKey("newAuditorKey");

            ConfidentialEnv ct{
                env,
                alice,
                {{.account = bob}},
                tfMPTCanHoldConfidentialBalance | tfMPTCanTransfer,
                auditor};

            // Rotate only the auditor key: auditor key epoch 0 -> 1, issuer key
            // epoch stays 0. The auditor mirror is now stale but the issuer
            // mirror is still current.
            ct.mpt.generateKeyPair(newAuditorKey);
            ct.mpt.set({.account = alice, .auditorPubKey = ct.mpt.getPubKey(newAuditorKey)});

            ct.mpt.mirrorUpdate({
                .account = alice,
                .holder = bob,
                .issuerEncryptedAmount = validCipher,
                .auditorEncryptedAmount = validCipher,
                .previousIssuerKey = validKey,
                .err = tecNO_PERMISSION,
            });
        }

        // Holder self-migration mode runs the same staleness checks.
        // No key has rotated, so the holder's own issuer mirror is current and
        // there is nothing to migrate.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            ConfidentialEnv ct{env, alice, {{.account = bob}}};

            ct.mpt.mirrorUpdate({
                .account = bob,
                .issuerEncryptedAmount = validCipher,
                .err = tecNO_PERMISSION,
            });
        }

        // Holder self-migration mode, simultaneously migrating both keys: only the issuer key
        // has rotated, so the holder's issuer mirror is stale but the auditor
        // mirror is still current.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const auditor("auditor");
            Account const newIssuerKey("newIssuerKey");

            ConfidentialEnv ct{
                env,
                alice,
                {{.account = bob}},
                tfMPTCanHoldConfidentialBalance | tfMPTCanTransfer,
                auditor};

            // Rotate only the issuer key: issuer key epoch 0 -> 1, auditor key
            // epoch stays 0.
            ct.mpt.generateKeyPair(newIssuerKey);
            ct.mpt.set({.account = alice, .issuerPubKey = ct.mpt.getPubKey(newIssuerKey)});

            // Holder mode (no Holder field) needs no previous issuer key.
            ct.mpt.mirrorUpdate({
                .account = bob,
                .issuerEncryptedAmount = validCipher,
                .auditorEncryptedAmount = validCipher,
                .err = tecNO_PERMISSION,
            });
        }

        // Holder self-migration mode, simultaneously migrating both keys:
        // only the auditor key has rotated, so the holder's auditor mirror is stale but the issuer
        // mirror is still current.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const auditor("auditor");
            Account const newAuditorKey("newAuditorKey");

            ConfidentialEnv ct{
                env,
                alice,
                {{.account = bob}},
                tfMPTCanHoldConfidentialBalance | tfMPTCanTransfer,
                auditor};

            // Rotate only the auditor key: auditor key epoch 0 -> 1, issuer key
            // epoch stays 0.
            ct.mpt.generateKeyPair(newAuditorKey);
            ct.mpt.set({.account = alice, .auditorPubKey = ct.mpt.getPubKey(newAuditorKey)});

            // Auditor mirror is stale but issuer mirror is current so this is rejected.
            ct.mpt.mirrorUpdate({
                .account = bob,
                .issuerEncryptedAmount = validCipher,
                .auditorEncryptedAmount = validCipher,
                .err = tecNO_PERMISSION,
            });
        }

        // Holder self-migration requires the holder's inbox to be canonical
        // zero, because the cross-key equality proof anchors on the spending
        // balance, which only reflects the full balance after the inbox is
        // merged. A holder with a non-zero inbox is rejected.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const newIssuerKey("newIssuerKey");

            ConfidentialEnv ct{env, alice, {{.account = bob}, {.account = carol}}};

            // Carol sends Bob a confidential amount; Bob does NOT merge it, so
            // his inbox is no longer canonical zero.
            ct.mpt.send({.account = carol, .dest = bob, .amt = 10});

            // Rotate the issuer key so the issuer mirror is stale and the
            // migration gets past the epoch check to reach the inbox check.
            ct.mpt.generateKeyPair(newIssuerKey);
            ct.mpt.set({.account = alice, .issuerPubKey = ct.mpt.getPubKey(newIssuerKey)});

            ct.mpt.mirrorUpdate({
                .account = bob,
                .issuerEncryptedAmount = validCipher,
                .err = tecNO_PERMISSION,
            });

            // Merging the inbox makes the migration succeed.
            ct.mpt.mergeInbox({.account = bob});
            ct.mpt.mirrorUpdate({
                .account = bob,
                .issuerEncryptedAmount = validCipher,
                .err = tesSUCCESS,
            });
        }
    }

    void
    testConfidentialMPTMirrorUpdateDoApply(FeatureBitset features)
    {
        testcase("ConfidentialMPTMirrorUpdate doApply");
        using namespace test::jtx;

        // The holder's confidential balance, matching the ConfidentialEnv default
        // convertAmount. The migration re-encrypts this amount under the new key.
        std::uint64_t const amount = 100;

        // Issuer mode issuer-mirror migration. The new issuer mirror is written
        // and the auissuerditor mirror epoch advances to the issuer key epoch.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const newIssuerKey("newIssuerKey");
            ConfidentialEnv ct{env, alice, {{.account = bob}}};

            // Rotate the issuer key: issuer key epoch 0 -> 1.
            ct.mpt.generateKeyPair(newIssuerKey);
            ct.mpt.set({.account = alice, .issuerPubKey = ct.mpt.getPubKey(newIssuerKey)});

            // Re-encrypt Bob's balance under the new issuer key.
            Buffer const newIssuerCipher =
                ct.mpt.encryptAmount(newIssuerKey, amount, generateBlindingFactor());

            // The previous issuer key is the pre-rotation issuer key (alice's),
            // no longer on-ledger after the rotation, provide it in the transaction.
            ct.mpt.mirrorUpdate({
                .account = alice,
                .holder = bob,
                .issuerEncryptedAmount = newIssuerCipher,
                .previousIssuerKey = ct.mpt.getPubKey(alice),
            });

            auto const sle = env.le(keylet::mptoken(ct.mpt.issuanceID(), bob.id()));
            if (!BEAST_EXPECT(sle))
                return;

            BEAST_EXPECT(strHex((*sle)[sfIssuerEncryptedBalance]) == strHex(newIssuerCipher));
            BEAST_EXPECT((*sle)[~sfIssuerKeyMirrorEpoch] == 1u);

            // The issuer mirror is now current, so re-migrating it is rejected.
            ct.mpt.mirrorUpdate({
                .account = alice,
                .holder = bob,
                .issuerEncryptedAmount = newIssuerCipher,
                .previousIssuerKey = ct.mpt.getPubKey(alice),
                .err = tecNO_PERMISSION,
            });
        }

        // Issuer mode auditor-mirror migration. The new auditor mirror is written
        // and the auditor mirror epoch advances to the auditor key epoch.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const auditor("auditor");
            Account const newAuditorKey("newAuditorKey");
            ConfidentialEnv ct{
                env,
                alice,
                {{.account = bob}},
                tfMPTCanHoldConfidentialBalance | tfMPTCanTransfer,
                auditor};

            // Rotate only the auditor key: auditor key epoch 0 -> 1.
            ct.mpt.generateKeyPair(newAuditorKey);
            ct.mpt.set({.account = alice, .auditorPubKey = ct.mpt.getPubKey(newAuditorKey)});

            // Re-encrypt Bob's balance under the new auditor key.
            Buffer const newAuditorCipher =
                ct.mpt.encryptAmount(newAuditorKey, amount, generateBlindingFactor());

            ct.mpt.mirrorUpdate({
                .account = alice,
                .holder = bob,
                .auditorEncryptedAmount = newAuditorCipher,
            });

            auto const sle = env.le(keylet::mptoken(ct.mpt.issuanceID(), bob.id()));
            if (!BEAST_EXPECT(sle))
                return;

            BEAST_EXPECT(strHex((*sle)[sfAuditorEncryptedBalance]) == strHex(newAuditorCipher));
            BEAST_EXPECT((*sle)[~sfAuditorKeyMirrorEpoch] == 1u);
        }

        // Issuer mode simultaneous migration: both mirrors are written in one transaction and
        // both epochs advance.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const auditor("auditor");
            Account const newIssuerKey("newIssuerKey");
            Account const newAuditorKey("newAuditorKey");
            ConfidentialEnv ct{
                env,
                alice,
                {{.account = bob}},
                tfMPTCanHoldConfidentialBalance | tfMPTCanTransfer,
                auditor};

            // Rotate both keys: both key epochs 0 -> 1.
            ct.mpt.generateKeyPair(newIssuerKey);
            ct.mpt.generateKeyPair(newAuditorKey);
            ct.mpt.set({
                .account = alice,
                .issuerPubKey = ct.mpt.getPubKey(newIssuerKey),
                .auditorPubKey = ct.mpt.getPubKey(newAuditorKey),
            });

            // Re-encrypt Bob's balance under each new key.
            Buffer const bf = generateBlindingFactor();
            Buffer const newIssuerCipher = ct.mpt.encryptAmount(newIssuerKey, amount, bf);
            Buffer const newAuditorCipher = ct.mpt.encryptAmount(newAuditorKey, amount, bf);

            ct.mpt.mirrorUpdate({
                .account = alice,
                .holder = bob,
                .issuerEncryptedAmount = newIssuerCipher,
                .auditorEncryptedAmount = newAuditorCipher,
                .previousIssuerKey = ct.mpt.getPubKey(alice),
            });

            auto const sle = env.le(keylet::mptoken(ct.mpt.issuanceID(), bob.id()));
            if (!BEAST_EXPECT(sle))
                return;

            BEAST_EXPECT(strHex((*sle)[sfIssuerEncryptedBalance]) == strHex(newIssuerCipher));
            BEAST_EXPECT(strHex((*sle)[sfAuditorEncryptedBalance]) == strHex(newAuditorCipher));
            BEAST_EXPECT((*sle)[~sfIssuerKeyMirrorEpoch] == 1u);
            BEAST_EXPECT((*sle)[~sfAuditorKeyMirrorEpoch] == 1u);
        }

        // Issuer mode auditor late-registration: the auditor key is registered for the first
        // time (key epoch absent), so setting the initial auditor mirror leaves
        // the auditor mirror epoch absent as well.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const auditor("auditor");
            // No auditor in the confidential setup, so bob has no auditor mirror.
            ConfidentialEnv ct{env, alice, {{.account = bob}}};

            // Register an auditor key for the first time (auditor key epoch stays
            // absent).
            ct.mpt.generateKeyPair(auditor);
            ct.mpt.set({.account = alice, .auditorPubKey = ct.mpt.getPubKey(auditor)});

            // Encrypt Bob's balance under the newly registered auditor key.
            Buffer const auditorCipher =
                ct.mpt.encryptAmount(auditor, amount, generateBlindingFactor());

            ct.mpt.mirrorUpdate({
                .account = alice,
                .holder = bob,
                .auditorEncryptedAmount = auditorCipher,
            });

            auto const sle = env.le(keylet::mptoken(ct.mpt.issuanceID(), bob.id()));
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT(strHex((*sle)[sfAuditorEncryptedBalance]) == strHex(auditorCipher));
            // First-time registration leaves the mirror epoch absent (== 0).
            BEAST_EXPECT(!sle->isFieldPresent(sfAuditorKeyMirrorEpoch));
        }

        // Holder self-migration migrates from the holder's own spending balance
        // (Holder being Account field, no Holder field, and no previous issuer key in any flow
        // because the anchor is the spending balance, not the old issuer mirror). ConfidentialEnv
        // already merged the inbox so the holder's inbox is canonical zero.

        // Holder issuer-mirror migration.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const newIssuerKey("newIssuerKey");
            ConfidentialEnv ct{env, alice, {{.account = bob}}};

            ct.mpt.generateKeyPair(newIssuerKey);
            ct.mpt.set({.account = alice, .issuerPubKey = ct.mpt.getPubKey(newIssuerKey)});

            // The holder re-encrypts their own balance under the new issuer key.
            Buffer const newIssuerCipher =
                ct.mpt.encryptAmount(newIssuerKey, amount, generateBlindingFactor());

            ct.mpt.mirrorUpdate({
                .account = bob,
                .issuerEncryptedAmount = newIssuerCipher,
            });

            auto const sle = env.le(keylet::mptoken(ct.mpt.issuanceID(), bob.id()));
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT(strHex((*sle)[sfIssuerEncryptedBalance]) == strHex(newIssuerCipher));
            BEAST_EXPECT((*sle)[~sfIssuerKeyMirrorEpoch] == 1u);
        }

        // Holder auditor-mirror migration.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const auditor("auditor");
            Account const newAuditorKey("newAuditorKey");
            ConfidentialEnv ct{
                env,
                alice,
                {{.account = bob}},
                tfMPTCanHoldConfidentialBalance | tfMPTCanTransfer,
                auditor};

            ct.mpt.generateKeyPair(newAuditorKey);
            ct.mpt.set({.account = alice, .auditorPubKey = ct.mpt.getPubKey(newAuditorKey)});

            // The holder re-encrypts their own balance under the new auditor key.
            Buffer const newAuditorCipher =
                ct.mpt.encryptAmount(newAuditorKey, amount, generateBlindingFactor());

            ct.mpt.mirrorUpdate({
                .account = bob,
                .auditorEncryptedAmount = newAuditorCipher,
            });

            auto const sle = env.le(keylet::mptoken(ct.mpt.issuanceID(), bob.id()));
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT(strHex((*sle)[sfAuditorEncryptedBalance]) == strHex(newAuditorCipher));
            BEAST_EXPECT((*sle)[~sfAuditorKeyMirrorEpoch] == 1u);
        }

        // Holder simultaneous migration of both mirrors.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const auditor("auditor");
            Account const newIssuerKey("newIssuerKey");
            Account const newAuditorKey("newAuditorKey");
            ConfidentialEnv ct{
                env,
                alice,
                {{.account = bob}},
                tfMPTCanHoldConfidentialBalance | tfMPTCanTransfer,
                auditor};

            ct.mpt.generateKeyPair(newIssuerKey);
            ct.mpt.generateKeyPair(newAuditorKey);
            ct.mpt.set({
                .account = alice,
                .issuerPubKey = ct.mpt.getPubKey(newIssuerKey),
                .auditorPubKey = ct.mpt.getPubKey(newAuditorKey),
            });

            Buffer const bf = generateBlindingFactor();
            Buffer const newIssuerCipher = ct.mpt.encryptAmount(newIssuerKey, amount, bf);
            Buffer const newAuditorCipher = ct.mpt.encryptAmount(newAuditorKey, amount, bf);

            // Holder mode needs no previous issuer key even for the issuer mirror.
            ct.mpt.mirrorUpdate({
                .account = bob,
                .issuerEncryptedAmount = newIssuerCipher,
                .auditorEncryptedAmount = newAuditorCipher,
            });

            auto const sle = env.le(keylet::mptoken(ct.mpt.issuanceID(), bob.id()));
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT(strHex((*sle)[sfIssuerEncryptedBalance]) == strHex(newIssuerCipher));
            BEAST_EXPECT(strHex((*sle)[sfAuditorEncryptedBalance]) == strHex(newAuditorCipher));
            BEAST_EXPECT((*sle)[~sfIssuerKeyMirrorEpoch] == 1u);
            BEAST_EXPECT((*sle)[~sfAuditorKeyMirrorEpoch] == 1u);
        }
    }

    void
    testConfidentialMPTMirrorUpdateMultipleRotationsIssuerMode(FeatureBitset features)
    {
        testcase("ConfidentialMPTMirrorUpdate issuer migrates after several rotations");
        using namespace test::jtx;

        std::uint64_t const amount = 100;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const auditor("auditor");
        Account const issuerKey1("issuerKey1");
        Account const issuerKey2("issuerKey2");
        Account const issuerKey3("issuerKey3");
        Account const issuerKey4("issuerKey4");
        Account const issuerKey5("issuerKey5");
        Account const auditorKey1("auditorKey1");
        Account const auditorKey2("auditorKey2");
        Account const auditorKey3("auditorKey3");
        Account const auditorKey4("auditorKey4");
        ConfidentialEnv ct{
            env,
            alice,
            {{.account = bob}},
            tfMPTCanHoldConfidentialBalance | tfMPTCanTransfer,
            auditor};

        // Rotate the issuer key three times: issuer key epoch 0 -> 3. Bob never
        // migrates in between, so his issuer mirror stays at mirror epoch 0 and
        // is still encrypted under the original issuer key (alice's).
        ct.mpt.generateKeyPair(issuerKey1);
        ct.mpt.generateKeyPair(issuerKey2);
        ct.mpt.generateKeyPair(issuerKey3);
        ct.mpt.set({.account = alice, .issuerPubKey = ct.mpt.getPubKey(issuerKey1)});
        ct.mpt.set({.account = alice, .issuerPubKey = ct.mpt.getPubKey(issuerKey2)});
        ct.mpt.set({.account = alice, .issuerPubKey = ct.mpt.getPubKey(issuerKey3)});

        {
            auto const sleIssuance = env.le(keylet::mptokenIssuance(ct.mpt.issuanceID()));
            BEAST_EXPECT(sleIssuance && (*sleIssuance)[~sfIssuerKeyEpoch] == 3u);
        }

        // A single migration re-encrypts the mirror under the newest key and
        // jumps the mirror epoch straight to the current key epoch (3), rather
        // than advancing one rotation at a time. The previous issuer key is the
        // original key (alice's) that the stale mirror is still encrypted under,
        // not any intermediate rotation.
        Buffer const newIssuerCipher =
            ct.mpt.encryptAmount(issuerKey3, amount, generateBlindingFactor());

        ct.mpt.mirrorUpdate({
            .account = alice,
            .holder = bob,
            .issuerEncryptedAmount = newIssuerCipher,
            .previousIssuerKey = ct.mpt.getPubKey(alice),
        });

        {
            auto const sle = env.le(keylet::mptoken(ct.mpt.issuanceID(), bob.id()));
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT(strHex((*sle)[sfIssuerEncryptedBalance]) == strHex(newIssuerCipher));
            BEAST_EXPECT((*sle)[~sfIssuerKeyMirrorEpoch] == 3u);
        }

        // The issuer mirror is now current (epoch 3 == key epoch 3), so a second
        // issuer migration is rejected.
        ct.mpt.mirrorUpdate({
            .account = alice,
            .holder = bob,
            .issuerEncryptedAmount = newIssuerCipher,
            .previousIssuerKey = ct.mpt.getPubKey(alice),
            .err = tecNO_PERMISSION,
        });

        // Now rotate the auditor key twice: auditor key epoch 0 -> 2. Bob's
        // auditor mirror is still at mirror epoch 0, under the original auditor
        // key. The issuer key and its epoch are untouched.
        ct.mpt.generateKeyPair(auditorKey1);
        ct.mpt.generateKeyPair(auditorKey2);
        ct.mpt.set({.account = alice, .auditorPubKey = ct.mpt.getPubKey(auditorKey1)});
        ct.mpt.set({.account = alice, .auditorPubKey = ct.mpt.getPubKey(auditorKey2)});

        {
            auto const sleIssuance = env.le(keylet::mptokenIssuance(ct.mpt.issuanceID()));
            BEAST_EXPECT(sleIssuance && (*sleIssuance)[~sfAuditorKeyEpoch] == 2u);
            BEAST_EXPECT(sleIssuance && (*sleIssuance)[~sfIssuerKeyEpoch] == 3u);
        }

        // A single auditor-only migration jumps the auditor mirror epoch straight
        // to the current auditor key epoch (2). This is an issuer-mode
        // auditor-only migration, which is allowed because the issuer mirror is
        // already current; no previous issuer key is needed for an auditor
        // migration.
        Buffer const newAuditorCipher =
            ct.mpt.encryptAmount(auditorKey2, amount, generateBlindingFactor());

        ct.mpt.mirrorUpdate({
            .account = alice,
            .holder = bob,
            .auditorEncryptedAmount = newAuditorCipher,
        });

        {
            auto const sle = env.le(keylet::mptoken(ct.mpt.issuanceID(), bob.id()));
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT(strHex((*sle)[sfAuditorEncryptedBalance]) == strHex(newAuditorCipher));
            BEAST_EXPECT((*sle)[~sfAuditorKeyMirrorEpoch] == 2u);
            // The issuer mirror and its epoch are unaffected by the auditor
            // migration.
            BEAST_EXPECT(strHex((*sle)[sfIssuerEncryptedBalance]) == strHex(newIssuerCipher));
            BEAST_EXPECT((*sle)[~sfIssuerKeyMirrorEpoch] == 3u);
        }

        // The auditor mirror is now current (epoch 2 == key epoch 2), so a second
        // auditor migration is rejected.
        ct.mpt.mirrorUpdate({
            .account = alice,
            .holder = bob,
            .auditorEncryptedAmount = newAuditorCipher,
            .err = tecNO_PERMISSION,
        });

        // Now rotate BOTH keys together twice: issuer key epoch 3 -> 5, auditor
        // key epoch 2 -> 4. Bob's mirrors stay at epoch 3 / 2 (stale again).
        ct.mpt.generateKeyPair(issuerKey4);
        ct.mpt.generateKeyPair(issuerKey5);
        ct.mpt.generateKeyPair(auditorKey3);
        ct.mpt.generateKeyPair(auditorKey4);
        ct.mpt.set({
            .account = alice,
            .issuerPubKey = ct.mpt.getPubKey(issuerKey4),
            .auditorPubKey = ct.mpt.getPubKey(auditorKey3),
        });
        ct.mpt.set({
            .account = alice,
            .issuerPubKey = ct.mpt.getPubKey(issuerKey5),
            .auditorPubKey = ct.mpt.getPubKey(auditorKey4),
        });

        {
            auto const sleIssuance = env.le(keylet::mptokenIssuance(ct.mpt.issuanceID()));
            BEAST_EXPECT(sleIssuance && (*sleIssuance)[~sfIssuerKeyEpoch] == 5u);
            BEAST_EXPECT(sleIssuance && (*sleIssuance)[~sfAuditorKeyEpoch] == 4u);
        }

        // A single simultaneous migration brings both mirrors current in one
        // transaction: issuer mirror epoch 3 -> 5, auditor mirror epoch 2 -> 4.
        // The previous issuer key is issuerKey3, which is the key Bob's current
        // (stale) issuer mirror is encrypted under after the earlier issuer
        // migration, not alice's original key nor any intermediate rotation.
        Buffer const bothIssuerCipher =
            ct.mpt.encryptAmount(issuerKey5, amount, generateBlindingFactor());
        Buffer const bothAuditorCipher =
            ct.mpt.encryptAmount(auditorKey4, amount, generateBlindingFactor());

        ct.mpt.mirrorUpdate({
            .account = alice,
            .holder = bob,
            .issuerEncryptedAmount = bothIssuerCipher,
            .auditorEncryptedAmount = bothAuditorCipher,
            .previousIssuerKey = ct.mpt.getPubKey(issuerKey3),
        });

        {
            auto const sle = env.le(keylet::mptoken(ct.mpt.issuanceID(), bob.id()));
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT(strHex((*sle)[sfIssuerEncryptedBalance]) == strHex(bothIssuerCipher));
            BEAST_EXPECT(strHex((*sle)[sfAuditorEncryptedBalance]) == strHex(bothAuditorCipher));
            BEAST_EXPECT((*sle)[~sfIssuerKeyMirrorEpoch] == 5u);
            BEAST_EXPECT((*sle)[~sfAuditorKeyMirrorEpoch] == 4u);
        }

        // Both mirrors are current now, so a second simultaneous migration is
        // rejected.
        ct.mpt.mirrorUpdate({
            .account = alice,
            .holder = bob,
            .issuerEncryptedAmount = bothIssuerCipher,
            .auditorEncryptedAmount = bothAuditorCipher,
            .previousIssuerKey = ct.mpt.getPubKey(issuerKey5),
            .err = tecNO_PERMISSION,
        });
    }

    void
    testConfidentialMPTMirrorUpdateMultipleRotationsHolderMode(FeatureBitset features)
    {
        testcase("ConfidentialMPTMirrorUpdate holder migrates after several rotations");
        using namespace test::jtx;

        std::uint64_t const amount = 100;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const auditor("auditor");
        Account const issuerKey1("issuerKey1");
        Account const issuerKey2("issuerKey2");
        Account const issuerKey3("issuerKey3");
        Account const issuerKey4("issuerKey4");
        Account const issuerKey5("issuerKey5");
        Account const auditorKey1("auditorKey1");
        Account const auditorKey2("auditorKey2");
        Account const auditorKey3("auditorKey3");
        Account const auditorKey4("auditorKey4");
        ConfidentialEnv ct{
            env,
            alice,
            {{.account = bob}},
            tfMPTCanHoldConfidentialBalance | tfMPTCanTransfer,
            auditor};

        // In holder self-migration mode the holder submits (account = bob, no
        // Holder field) and never provides a previous issuer key.
        // Bob's inbox is canonical zero after the ConfidentialEnv merge.

        // Rotate the issuer key three times: issuer key epoch 0 -> 3.
        ct.mpt.generateKeyPair(issuerKey1);
        ct.mpt.generateKeyPair(issuerKey2);
        ct.mpt.generateKeyPair(issuerKey3);
        ct.mpt.set({.account = alice, .issuerPubKey = ct.mpt.getPubKey(issuerKey1)});
        ct.mpt.set({.account = alice, .issuerPubKey = ct.mpt.getPubKey(issuerKey2)});
        ct.mpt.set({.account = alice, .issuerPubKey = ct.mpt.getPubKey(issuerKey3)});

        // A single holder migration jumps the issuer mirror epoch straight to 3.
        Buffer const newIssuerCipher =
            ct.mpt.encryptAmount(issuerKey3, amount, generateBlindingFactor());

        ct.mpt.mirrorUpdate({
            .account = bob,
            .issuerEncryptedAmount = newIssuerCipher,
        });

        {
            auto const sle = env.le(keylet::mptoken(ct.mpt.issuanceID(), bob.id()));
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT(strHex((*sle)[sfIssuerEncryptedBalance]) == strHex(newIssuerCipher));
            BEAST_EXPECT((*sle)[~sfIssuerKeyMirrorEpoch] == 3u);
        }

        // The issuer mirror is current, so a second holder issuer migration is
        // rejected.
        ct.mpt.mirrorUpdate({
            .account = bob,
            .issuerEncryptedAmount = newIssuerCipher,
            .err = tecNO_PERMISSION,
        });

        // Rotate the auditor key twice: auditor key epoch 0 -> 2.
        ct.mpt.generateKeyPair(auditorKey1);
        ct.mpt.generateKeyPair(auditorKey2);
        ct.mpt.set({.account = alice, .auditorPubKey = ct.mpt.getPubKey(auditorKey1)});
        ct.mpt.set({.account = alice, .auditorPubKey = ct.mpt.getPubKey(auditorKey2)});

        // A single holder auditor migration jumps the auditor mirror epoch to 2.
        Buffer const newAuditorCipher =
            ct.mpt.encryptAmount(auditorKey2, amount, generateBlindingFactor());

        ct.mpt.mirrorUpdate({
            .account = bob,
            .auditorEncryptedAmount = newAuditorCipher,
        });

        {
            auto const sle = env.le(keylet::mptoken(ct.mpt.issuanceID(), bob.id()));
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT(strHex((*sle)[sfAuditorEncryptedBalance]) == strHex(newAuditorCipher));
            BEAST_EXPECT((*sle)[~sfAuditorKeyMirrorEpoch] == 2u);
            // The issuer mirror is unaffected.
            BEAST_EXPECT(strHex((*sle)[sfIssuerEncryptedBalance]) == strHex(newIssuerCipher));
            BEAST_EXPECT((*sle)[~sfIssuerKeyMirrorEpoch] == 3u);
        }

        // The auditor mirror is current, so a second holder auditor migration is
        // rejected.
        ct.mpt.mirrorUpdate({
            .account = bob,
            .auditorEncryptedAmount = newAuditorCipher,
            .err = tecNO_PERMISSION,
        });

        // Rotate both keys together twice: issuer key epoch 3 -> 5, auditor key
        // epoch 2 -> 4.
        ct.mpt.generateKeyPair(issuerKey4);
        ct.mpt.generateKeyPair(issuerKey5);
        ct.mpt.generateKeyPair(auditorKey3);
        ct.mpt.generateKeyPair(auditorKey4);
        ct.mpt.set({
            .account = alice,
            .issuerPubKey = ct.mpt.getPubKey(issuerKey4),
            .auditorPubKey = ct.mpt.getPubKey(auditorKey3),
        });
        ct.mpt.set({
            .account = alice,
            .issuerPubKey = ct.mpt.getPubKey(issuerKey5),
            .auditorPubKey = ct.mpt.getPubKey(auditorKey4),
        });

        // A single holder migration brings both mirrors current: issuer mirror
        // epoch 3 -> 5, auditor mirror epoch 2 -> 4. Still no previous issuer key.
        Buffer const bothIssuerCipher =
            ct.mpt.encryptAmount(issuerKey5, amount, generateBlindingFactor());
        Buffer const bothAuditorCipher =
            ct.mpt.encryptAmount(auditorKey4, amount, generateBlindingFactor());

        ct.mpt.mirrorUpdate({
            .account = bob,
            .issuerEncryptedAmount = bothIssuerCipher,
            .auditorEncryptedAmount = bothAuditorCipher,
        });

        {
            auto const sle = env.le(keylet::mptoken(ct.mpt.issuanceID(), bob.id()));
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT(strHex((*sle)[sfIssuerEncryptedBalance]) == strHex(bothIssuerCipher));
            BEAST_EXPECT(strHex((*sle)[sfAuditorEncryptedBalance]) == strHex(bothAuditorCipher));
            BEAST_EXPECT((*sle)[~sfIssuerKeyMirrorEpoch] == 5u);
            BEAST_EXPECT((*sle)[~sfAuditorKeyMirrorEpoch] == 4u);
        }

        // Both mirrors are current, so a second holder migration is rejected.
        ct.mpt.mirrorUpdate({
            .account = bob,
            .issuerEncryptedAmount = bothIssuerCipher,
            .auditorEncryptedAmount = bothAuditorCipher,
            .err = tecNO_PERMISSION,
        });
    }

public:
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

    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{testableAmendments()};

        testMPTokenIssuanceSetWithFeats(all);
        testMPTokenIssuanceSetWithFeats(all - featureConfidentialMPTKeyRotation);

        testConfidentialMPTMirrorUpdatePreflight(all);
        testConfidentialMPTMirrorUpdatePreflight(all - featureConfidentialMPTKeyRotation);
        testConfidentialMPTMirrorUpdatePreflight(all - featureConfidentialTransfer);
        testConfidentialMPTMirrorUpdatePreclaim(all);
        testConfidentialMPTMirrorUpdateDoApply(all);
        testConfidentialMPTMirrorUpdateMultipleRotationsIssuerMode(all);
        testConfidentialMPTMirrorUpdateMultipleRotationsHolderMode(all);
    }
};

BEAST_DEFINE_TESTSUITE(ConfidentialMPTKeyRotation, app, xrpl);

}  // namespace xrpl
