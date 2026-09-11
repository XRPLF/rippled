#include <test/jtx/Account.h>
#include <test/jtx/ConfidentialTransfer.h>
#include <test/jtx/Env.h>
#include <test/jtx/mpt.h>

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
#include <vector>

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

        // A rotation replaces the issuer key and bumps its epoch. The auditor
        // key was never registered, so it and its epoch stay absent.
        if (rotationEnabled)
        {
            BEAST_EXPECT(mptAlice.checkEncryptionKeys(bob, std::nullopt));
            BEAST_EXPECT(mptAlice.checkKeyEpochs(1u, std::nullopt));
        }
        else
        {
            BEAST_EXPECT(mptAlice.checkEncryptionKeys(alice, std::nullopt));
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

        // Verify that no epochs are set when registering for the first time.
        BEAST_EXPECT(mptAlice.checkKeyEpochs(std::nullopt, std::nullopt));

        // Rotating both keys requires the amendment
        bool const rotationEnabled = features[featureConfidentialMPTKeyRotation];
        mptAlice.set({
            .account = alice,
            .issuerPubKey = mptAlice.getPubKey(bob),
            .auditorPubKey = mptAlice.getPubKey(alice),
            .err = rotationEnabled ? TER(tesSUCCESS) : TER(tecNO_PERMISSION),
        });

        if (rotationEnabled)
        {
            BEAST_EXPECT(mptAlice.checkEncryptionKeys(bob, alice));
            BEAST_EXPECT(mptAlice.checkKeyEpochs(1u, 1u));
        }
        else
        {
            BEAST_EXPECT(mptAlice.checkEncryptionKeys(alice, auditor));
            BEAST_EXPECT(mptAlice.checkKeyEpochs(std::nullopt, std::nullopt));
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

        // The issuer key keeps unchanged, and rotating only the auditor key
        // bumps only its epoch.
        if (rotationEnabled)
        {
            BEAST_EXPECT(mptAlice.checkEncryptionKeys(alice, bob));
            BEAST_EXPECT(mptAlice.checkKeyEpochs(std::nullopt, 1u));
        }
        else
        {
            BEAST_EXPECT(mptAlice.checkEncryptionKeys(alice, auditor));
            BEAST_EXPECT(mptAlice.checkKeyEpochs(std::nullopt, std::nullopt));
        }

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

        BEAST_EXPECT(mptAlice.checkEncryptionKeys(
            alice, rotationEnabled ? std::optional<Account>(auditor) : std::nullopt));
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
        BEAST_EXPECT(mptAlice.checkEncryptionKeys(
            alice, rotationEnabled ? std::optional<Account>(auditor) : std::nullopt));
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

        // The rejected transaction leaves the issuance without either key.
        BEAST_EXPECT(mptAlice.checkEncryptionKeys(std::nullopt, std::nullopt));
        BEAST_EXPECT(mptAlice.checkKeyEpochs(std::nullopt, std::nullopt));
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
        if (rotationEnabled)
        {
            BEAST_EXPECT(mptAlice.checkEncryptionKeys(carol, std::nullopt));
            BEAST_EXPECT(mptAlice.checkKeyEpochs(1u, std::nullopt));
        }
        else
        {
            BEAST_EXPECT(mptAlice.checkEncryptionKeys(alice, std::nullopt));
            BEAST_EXPECT(mptAlice.checkKeyEpochs(std::nullopt, std::nullopt));
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
    testMPTokenIssuanceSetKeyEpochAtMax(FeatureBitset features)
    {
        using namespace test::jtx;
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

        BEAST_EXPECT(mptAlice.checkEncryptionKeys(alice, auditor));
        BEAST_EXPECT(mptAlice.checkKeyEpochs(std::nullopt, std::nullopt));

        // Increment the auditor epoch to kMaxKeyEpoch - 1, leaving the issuer epoch absent.
        setEpochs(std::nullopt, kMaxKeyEpoch - 1);
        BEAST_EXPECT(mptAlice.checkEncryptionKeys(alice, auditor));
        BEAST_EXPECT(mptAlice.checkKeyEpochs(std::nullopt, kMaxKeyEpoch - 1));

        // Rotating the auditor key to kMaxKeyEpoch succeeds.
        mptAlice.set({
            .account = alice,
            .auditorPubKey = mptAlice.getPubKey(carol),
        });

        BEAST_EXPECT(mptAlice.checkEncryptionKeys(alice, carol));
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

        // Both rejections leave every key and epoch as it was.
        BEAST_EXPECT(mptAlice.checkEncryptionKeys(alice, carol));
        BEAST_EXPECT(mptAlice.checkKeyEpochs(std::nullopt, kMaxKeyEpoch));

        // The issuer key is unaffected by the exhausted auditor epoch.
        mptAlice.set({
            .account = alice,
            .issuerPubKey = mptAlice.getPubKey(bob),
        });

        BEAST_EXPECT(mptAlice.checkEncryptionKeys(bob, carol));
        BEAST_EXPECT(mptAlice.checkKeyEpochs(1u, kMaxKeyEpoch));

        // Increment the issuer epoch to kMaxKeyEpoch - 1.
        setEpochs(kMaxKeyEpoch - 1, std::nullopt);
        BEAST_EXPECT(mptAlice.checkEncryptionKeys(bob, carol));
        BEAST_EXPECT(mptAlice.checkKeyEpochs(kMaxKeyEpoch - 1, kMaxKeyEpoch));

        // Rotating the issuer key to kMaxKeyEpoch succeeds.
        mptAlice.set({
            .account = alice,
            .issuerPubKey = mptAlice.getPubKey(auditor),
        });

        BEAST_EXPECT(mptAlice.checkEncryptionKeys(auditor, carol));
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

        BEAST_EXPECT(mptAlice.checkEncryptionKeys(auditor, carol));
        BEAST_EXPECT(mptAlice.checkKeyEpochs(kMaxKeyEpoch, kMaxKeyEpoch));
    }

    void
    testConfidentialMPTConvertEpoch(FeatureBitset features)
    {
        testcase("ConfidentialMPTConvert mirror epoch");
        using namespace test::jtx;

        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const auditor("auditor");

        // A first-time convert with no rotation leaves both mirror
        // epochs absent.
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}, .auditor = auditor});
            setupConfidentialIssuance(mptAlice, alice, {bob}, {auditor});

            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(alice),
                .auditorPubKey = mptAlice.getPubKey(auditor),
            });

            BEAST_EXPECT(mptAlice.checkKeyEpochs(std::nullopt, std::nullopt));

            mptAlice.convert({
                .account = bob,
                .amt = 50,
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            BEAST_EXPECT(mptAlice.checkMirrorEpochs(bob, std::nullopt, std::nullopt));

            // Both mirrors are current, so converting again is allowed and
            // leaves the epochs untouched.
            mptAlice.convert({
                .account = bob,
                .amt = 20,
            });

            BEAST_EXPECT(mptAlice.checkMirrorEpochs(bob, std::nullopt, std::nullopt));
        }

        // Every remaining case needs key rotation to be enabled.
        if (!features[featureConfidentialMPTKeyRotation])
            return;

        // A first-time convert stamps the mirrors with whatever epochs the
        // issuance currently sits at. Only issuer key rotated in this case.
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}, .auditor = auditor});
            setupConfidentialIssuance(mptAlice, alice, {bob, carol}, {auditor});

            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(alice),
                .auditorPubKey = mptAlice.getPubKey(auditor),
            });

            // Ten rotations, issuance's issuer epoch is 10.
            for (int i = 0; i < 10; ++i)
            {
                mptAlice.generateKeyPair(alice);
                mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});
            }

            BEAST_EXPECT(mptAlice.checkKeyEpochs(10u, std::nullopt));
            BEAST_EXPECT(mptAlice.checkEncryptionKeys(alice, auditor));

            // carol converts for the first time, and her mirrors are stamped with the current
            // issuer epoch of 10.
            mptAlice.convert({
                .account = carol,
                .amt = 50,
                .holderPubKey = mptAlice.getPubKey(carol),
            });

            BEAST_EXPECT(mptAlice.checkMirrorEpochs(carol, 10u, std::nullopt));
        }

        // A first-time convert stamps the mirrors with whatever epochs the
        // issuance currently sits at. Both keys rotated in this case.
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}, .auditor = auditor});
            setupConfidentialIssuance(mptAlice, alice, {bob, carol}, {auditor});

            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(alice),
                .auditorPubKey = mptAlice.getPubKey(auditor),
            });

            // 100 rotations of both keys, so both epochs are 100.
            for (int i = 0; i < 100; ++i)
            {
                mptAlice.generateKeyPair(alice);
                mptAlice.generateKeyPair(auditor);
                mptAlice.set({
                    .account = alice,
                    .issuerPubKey = mptAlice.getPubKey(alice),
                    .auditorPubKey = mptAlice.getPubKey(auditor),
                });
            }

            // 5 more rotations of the auditor key alone, so the auditor epoch is 105 now.
            for (int i = 0; i < 5; ++i)
            {
                mptAlice.generateKeyPair(auditor);
                mptAlice.set({.account = alice, .auditorPubKey = mptAlice.getPubKey(auditor)});
            }

            BEAST_EXPECT(mptAlice.checkKeyEpochs(100u, 105u));
            BEAST_EXPECT(mptAlice.checkEncryptionKeys(alice, auditor));

            // carol converts for the first time, and each of her mirrors is stamped with the epoch
            // of the key it was encrypted under.
            mptAlice.convert({
                .account = carol,
                .amt = 50,
                .holderPubKey = mptAlice.getPubKey(carol),
            });

            BEAST_EXPECT(mptAlice.checkMirrorEpochs(carol, 100u, 105u));
        }

        // An issuer key rotation leaves an existing holder's issuer mirror
        // behind, converting will be blocked until the holder's mirror is updated to the new epoch.
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}, .auditor = auditor});
            setupConfidentialIssuance(mptAlice, alice, {bob, carol}, {auditor});

            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(alice),
                .auditorPubKey = mptAlice.getPubKey(auditor),
            });

            // carol initializes before any rotation, so her mirrors carry no epoch
            // at all, the state every holder is in before the amendment.
            mptAlice.convert({
                .account = carol,
                .amt = 50,
                .holderPubKey = mptAlice.getPubKey(carol),
            });

            BEAST_EXPECT(mptAlice.checkMirrorEpochs(carol, std::nullopt, std::nullopt));

            // Rotate the issuer key to epoch 1.
            mptAlice.generateKeyPair(alice);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            BEAST_EXPECT(mptAlice.checkKeyEpochs(1u, std::nullopt));

            // bob converts for the first time which is allowed when registering the key.
            mptAlice.convert({
                .account = bob,
                .amt = 50,
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            BEAST_EXPECT(mptAlice.checkMirrorEpochs(bob, 1u, std::nullopt));

            // carol's absent epoch reads as 0 which is stale.
            mptAlice.convert({
                .account = carol,
                .amt = 20,
                .err = tecNO_PERMISSION,
            });

            BEAST_EXPECT(mptAlice.checkMirrorEpochs(carol, std::nullopt, std::nullopt));

            // Rotate the issuer key to epoch 2, leaving bob's issuer mirror stale.
            mptAlice.generateKeyPair(alice);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            BEAST_EXPECT(mptAlice.checkKeyEpochs(2u, std::nullopt));

            // This is not the first time convert, and bob's issuer mirror is behind the current
            // epoch, so the convert is rejected.
            mptAlice.convert({
                .account = bob,
                .amt = 20,
                .err = tecNO_PERMISSION,
            });

            // The rejected convert leaves bob's mirrors exactly as they were.
            BEAST_EXPECT(mptAlice.checkMirrorEpochs(bob, 1u, std::nullopt));

            // carol still cannot convert.
            mptAlice.convert({
                .account = carol,
                .amt = 20,
                .err = tecNO_PERMISSION,
            });

            BEAST_EXPECT(mptAlice.checkMirrorEpochs(carol, std::nullopt, std::nullopt));
        }

        // The auditor mirror is checked the same way, so rotating only the
        // auditor key blocks the convert on its own, with the issuer epoch
        // untouched.
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}, .auditor = auditor});
            setupConfidentialIssuance(mptAlice, alice, {bob, carol}, {auditor});

            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(alice),
                .auditorPubKey = mptAlice.getPubKey(auditor),
            });

            // bob initializes his confidential balance at epoch 0, so both of his
            // mirrors are current.
            mptAlice.convert({
                .account = bob,
                .amt = 50,
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            BEAST_EXPECT(mptAlice.checkMirrorEpochs(bob, std::nullopt, std::nullopt));

            // Rotate the auditor key only, leaving bob's auditor mirror behind
            // while his issuer mirror stays current.
            mptAlice.generateKeyPair(auditor);
            mptAlice.set({.account = alice, .auditorPubKey = mptAlice.getPubKey(auditor)});

            BEAST_EXPECT(mptAlice.checkKeyEpochs(std::nullopt, 1u));
            BEAST_EXPECT(mptAlice.checkEncryptionKeys(alice, auditor));

            mptAlice.convert({
                .account = bob,
                .amt = 20,
                .err = tecNO_PERMISSION,
            });

            BEAST_EXPECT(mptAlice.checkMirrorEpochs(bob, std::nullopt, std::nullopt));

            // Carol converts for the first time, and her auditor mirror is stamped with the current
            // auditor epoch of 1.
            mptAlice.convert({
                .account = carol,
                .amt = 50,
                .holderPubKey = mptAlice.getPubKey(carol),
            });

            BEAST_EXPECT(mptAlice.checkMirrorEpochs(carol, std::nullopt, 1u));
        }

        // A late auditor key registration bumps no epoch.
        // Although both epochs are still zero, the convert is blocked
        // because auditor mirror is missing.
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}, .auditor = auditor});
            setupConfidentialIssuance(mptAlice, alice, {bob}, {auditor});

            // Register the issuer key only.
            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(alice),
            });

            // The issuance has no auditor yet, so no auditor mirror is created.
            mptAlice.convert({
                .account = bob,
                .amt = 50,
                .fillAuditorEncryptedAmt = false,
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            BEAST_EXPECT(mptAlice.checkMirrorEpochs(bob, std::nullopt, std::nullopt));

            // Register the auditor key later, which bumps no epoch.
            mptAlice.set({
                .account = alice,
                .auditorPubKey = mptAlice.getPubKey(auditor),
            });

            BEAST_EXPECT(mptAlice.checkKeyEpochs(std::nullopt, std::nullopt));

            // bob's auditor mirror is still missing, so the convert is rejected.
            mptAlice.convert({
                .account = bob,
                .amt = 20,
                .err = tecNO_PERMISSION,
            });

            BEAST_EXPECT(mptAlice.checkMirrorEpochs(bob, std::nullopt, std::nullopt));
        }
    }

    void
    testConfidentialMPTSendEpoch(FeatureBitset features)
    {
        testcase("ConfidentialMPTSend mirror epoch");
        using namespace test::jtx;

        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const auditor("auditor");

        // Two holders that both initialized after a rotation are current, so a
        // send between them succeeds and leaves both mirrors untouched.
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});
            setupConfidentialIssuance(mptAlice, alice, {bob, carol});
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            // Rotate the issuer key to epoch 1 before anyone holds a confidential
            // balance.
            mptAlice.generateKeyPair(alice);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            BEAST_EXPECT(mptAlice.checkKeyEpochs(1u, std::nullopt));

            for (auto const& holder : {bob, carol})
            {
                mptAlice.convert({
                    .account = holder,
                    .amt = 50,
                    .holderPubKey = mptAlice.getPubKey(holder),
                });
                mptAlice.mergeInbox({.account = holder});
            }

            BEAST_EXPECT(mptAlice.checkMirrorEpochs(bob, 1u, std::nullopt));
            BEAST_EXPECT(mptAlice.checkMirrorEpochs(carol, 1u, std::nullopt));

            mptAlice.send({.account = bob, .dest = carol, .amt = 10});

            // The epochs are unchanged after send.
            BEAST_EXPECT(mptAlice.checkMirrorEpochs(bob, 1u, std::nullopt));
            BEAST_EXPECT(mptAlice.checkMirrorEpochs(carol, 1u, std::nullopt));
        }

        // Either the sender or the destination being stale will be rejected.
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});
            setupConfidentialIssuance(mptAlice, alice, {bob, carol});
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            // carol initializes at epoch 0.
            mptAlice.convert({
                .account = carol,
                .amt = 50,
                .holderPubKey = mptAlice.getPubKey(carol),
            });
            mptAlice.mergeInbox({.account = carol});

            // Rotate the issuer key to epoch 1, leaving carol behind.
            mptAlice.generateKeyPair(alice);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            // bob initializes after the rotation, so his mirrors are current.
            mptAlice.convert({
                .account = bob,
                .amt = 50,
                .holderPubKey = mptAlice.getPubKey(bob),
            });
            mptAlice.mergeInbox({.account = bob});

            BEAST_EXPECT(mptAlice.checkMirrorEpochs(carol, std::nullopt, std::nullopt));
            BEAST_EXPECT(mptAlice.checkMirrorEpochs(bob, 1u, std::nullopt));

            // This is rejected because the sender is the stale even though the destination is
            // current.
            mptAlice.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .err = tecNO_PERMISSION,
            });

            // This is rejected because the destination is the stale even though the sender is
            // current.
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .err = tecNO_PERMISSION,
            });

            // The rejected sends leave both mirrors as they were.
            BEAST_EXPECT(mptAlice.checkMirrorEpochs(carol, std::nullopt, std::nullopt));
            BEAST_EXPECT(mptAlice.checkMirrorEpochs(bob, 1u, std::nullopt));
        }

        // Auditor mirror is stale, the send will be rejected.
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}, .auditor = auditor});
            setupConfidentialIssuance(mptAlice, alice, {bob, carol}, {auditor});
            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(alice),
                .auditorPubKey = mptAlice.getPubKey(auditor),
            });

            for (auto const& holder : {bob, carol})
            {
                mptAlice.convert({
                    .account = holder,
                    .amt = 50,
                    .holderPubKey = mptAlice.getPubKey(holder),
                });
                mptAlice.mergeInbox({.account = holder});
            }

            // Rotate the auditor key only
            mptAlice.generateKeyPair(auditor);
            mptAlice.set({.account = alice, .auditorPubKey = mptAlice.getPubKey(auditor)});

            BEAST_EXPECT(mptAlice.checkKeyEpochs(std::nullopt, 1u));

            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .err = tecNO_PERMISSION,
            });

            mptAlice.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .err = tecNO_PERMISSION,
            });

            BEAST_EXPECT(mptAlice.checkMirrorEpochs(bob, std::nullopt, std::nullopt));
            BEAST_EXPECT(mptAlice.checkMirrorEpochs(carol, std::nullopt, std::nullopt));
        }
    }

    void
    testConfidentialMPTConvertBackEpoch(FeatureBitset features)
    {
        testcase("ConfidentialMPTConvertBack mirror epoch");
        using namespace test::jtx;

        Account const alice("alice");
        Account const bob("bob");
        Account const auditor("auditor");

        // A holder who initialized after a rotation is current, so converting
        // back is allowed and leaves the epoch it was stamped with alone.
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}});
            setupConfidentialIssuance(mptAlice, alice, {bob});
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            // Rotate the issuer key to epoch 1 before bob holds a confidential
            // balance.
            mptAlice.generateKeyPair(alice);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            BEAST_EXPECT(mptAlice.checkKeyEpochs(1u, std::nullopt));

            mptAlice.convert({
                .account = bob,
                .amt = 50,
                .holderPubKey = mptAlice.getPubKey(bob),
            });
            mptAlice.mergeInbox({.account = bob});

            BEAST_EXPECT(mptAlice.checkMirrorEpochs(bob, 1u, std::nullopt));

            mptAlice.convertBack({.account = bob, .amt = 20});

            BEAST_EXPECT(mptAlice.checkMirrorEpochs(bob, 1u, std::nullopt));
        }

        // Converting back with stale mirrors is rejected.
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}});
            setupConfidentialIssuance(mptAlice, alice, {bob});
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            // bob initializes at epoch 0.
            mptAlice.convert({
                .account = bob,
                .amt = 50,
                .holderPubKey = mptAlice.getPubKey(bob),
            });
            mptAlice.mergeInbox({.account = bob});

            // Converting back is allowed while his mirrors are still current.
            mptAlice.convertBack({.account = bob, .amt = 20});

            // Rotate the issuer key to epoch 1, leaving bob behind.
            mptAlice.generateKeyPair(alice);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.convertBack({
                .account = bob,
                .amt = 10,
                .err = tecNO_PERMISSION,
            });

            // The rejected convert back leaves bob's mirrors as they were.
            BEAST_EXPECT(mptAlice.checkMirrorEpochs(bob, std::nullopt, std::nullopt));
        }

        // Converting back with a stale auditor mirror is rejected, even if the issuer mirror is
        // current.
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}, .auditor = auditor});
            setupConfidentialIssuance(mptAlice, alice, {bob}, {auditor});
            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(alice),
                .auditorPubKey = mptAlice.getPubKey(auditor),
            });

            mptAlice.convert({
                .account = bob,
                .amt = 50,
                .holderPubKey = mptAlice.getPubKey(bob),
            });
            mptAlice.mergeInbox({.account = bob});

            // Rotate the auditor key only, leaving bob behind on that mirror alone.
            mptAlice.generateKeyPair(auditor);
            mptAlice.set({.account = alice, .auditorPubKey = mptAlice.getPubKey(auditor)});

            BEAST_EXPECT(mptAlice.checkKeyEpochs(std::nullopt, 1u));

            mptAlice.convertBack({
                .account = bob,
                .amt = 10,
                .err = tecNO_PERMISSION,
            });

            BEAST_EXPECT(mptAlice.checkMirrorEpochs(bob, std::nullopt, std::nullopt));
        }
    }

    void
    testConfidentialMPTClawbackEpoch(FeatureBitset features)
    {
        testcase("ConfidentialMPTClawback mirror epoch");
        using namespace test::jtx;

        Account const alice("alice");
        Account const bob("bob");
        Account const auditor("auditor");

        std::uint32_t const clawbackFlags =
            tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance | tfMPTCanClawback;

        // Clawback is not blocked on
        // a stale auditor mirror.
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}, .auditor = auditor});
            setupConfidentialIssuance(mptAlice, alice, {bob}, {auditor}, clawbackFlags);
            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(alice),
                .auditorPubKey = mptAlice.getPubKey(auditor),
            });

            // bob initializes both mirrors at epoch 0.
            mptAlice.convert({
                .account = bob,
                .amt = 50,
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            // Rotate the auditor key twice, leaving bob's auditor mirror behind.
            for (int i = 0; i < 2; ++i)
            {
                mptAlice.generateKeyPair(auditor);
                mptAlice.set({.account = alice, .auditorPubKey = mptAlice.getPubKey(auditor)});
            }

            BEAST_EXPECT(mptAlice.checkKeyEpochs(std::nullopt, 2u));
            BEAST_EXPECT(mptAlice.checkMirrorEpochs(bob, std::nullopt, std::nullopt));

            mptAlice.confidentialClaw({.account = alice, .holder = bob, .amt = 50});
            BEAST_EXPECT(mptAlice.checkMirrorEpochs(bob, std::nullopt, 2u));
        }

        // A holder who initialized after a rotation is clawed back successfully, and
        // the issuer mirror is updated to the current epoch.
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}});
            setupConfidentialIssuance(mptAlice, alice, {bob}, {}, clawbackFlags);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            // Rotate the issuer key five times, issuance's issuer epoch is 5.
            for (int i = 0; i < 5; ++i)
            {
                mptAlice.generateKeyPair(alice);
                mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});
            }

            mptAlice.convert({
                .account = bob,
                .amt = 50,
                .holderPubKey = mptAlice.getPubKey(bob),
            });
            BEAST_EXPECT(mptAlice.checkMirrorEpochs(bob, 5u, std::nullopt));

            mptAlice.confidentialClaw({.account = alice, .holder = bob, .amt = 50});
            BEAST_EXPECT(mptAlice.checkMirrorEpochs(bob, 5u, std::nullopt));
        }

        // Clawback is not blocked on
        // a stale issuer mirror. For now the proof cannot verify: it is checked
        // against the key registered on the issuance, while the mirror is still
        // encrypted under the key it was written with, and that older key is
        // nowhere on the ledger yet. This will be added in a separate PR.
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {bob}});
            setupConfidentialIssuance(mptAlice, alice, {bob}, {}, clawbackFlags);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            // bob initializes at epoch 0.
            mptAlice.convert({
                .account = bob,
                .amt = 50,
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            // Rotate the issuer key to epoch 1, leaving bob behind.
            mptAlice.generateKeyPair(alice);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            BEAST_EXPECT(mptAlice.checkKeyEpochs(1u, std::nullopt));

            mptAlice.confidentialClaw({
                .account = alice,
                .holder = bob,
                .amt = 50,
                .err = tecBAD_PROOF,
            });

            BEAST_EXPECT(mptAlice.checkMirrorEpochs(bob, std::nullopt, std::nullopt));
        }
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

        testConfidentialMPTConvertEpoch(all);
        testConfidentialMPTConvertEpoch(all - featureConfidentialMPTKeyRotation);
        testConfidentialMPTSendEpoch(all);
        testConfidentialMPTConvertBackEpoch(all);
        testConfidentialMPTClawbackEpoch(all);
    }
};

BEAST_DEFINE_TESTSUITE(ConfidentialMPTKeyRotation, app, xrpl);

}  // namespace xrpl
