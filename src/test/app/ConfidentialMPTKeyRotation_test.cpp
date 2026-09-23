#include <test/jtx/Account.h>
#include <test/jtx/ConfidentialTransfer.h>
#include <test/jtx/Env.h>
#include <test/jtx/mpt.h>

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
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
        // Issuer mode but account is not the issuer.
        mptAlice.mirrorUpdate({
            .account = bob,
            .holder = carol,
            .issuerEncryptedAmount = validCipher,
            .err = temMALFORMED,
        });

        // Issuer mode but the holder is the same as the issuer.
        mptAlice.mirrorUpdate({
            .account = alice,
            .holder = alice,
            .issuerEncryptedAmount = validCipher,
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

        // Issuer amount has the wrong length.
        mptAlice.mirrorUpdate({
            .account = alice,
            .holder = bob,
            .issuerEncryptedAmount = gMakeZeroBuffer(10),
            .err = temBAD_CIPHERTEXT,
        });

        // Auditor amount has the wrong length.
        mptAlice.mirrorUpdate({
            .account = alice,
            .holder = bob,
            .auditorEncryptedAmount = gMakeZeroBuffer(10),
            .err = temBAD_CIPHERTEXT,
        });

        // The proof has the wrong length.
        mptAlice.mirrorUpdate({
            .account = alice,
            .holder = bob,
            .issuerEncryptedAmount = validCipher,
            .zkProof = gMakeZeroBuffer(kEcEqualityProofLength - 1),
            .err = temMALFORMED,
        });

        // Issuer amount is the right length but not a valid ciphertext.
        mptAlice.mirrorUpdate({
            .account = alice,
            .holder = bob,
            .issuerEncryptedAmount = getBadCiphertext(),
            .err = temBAD_CIPHERTEXT,
        });

        // Auditor amount is the right length but not a valid ciphertext.
        mptAlice.mirrorUpdate({
            .account = alice,
            .holder = bob,
            .issuerEncryptedAmount = validCipher,
            .auditorEncryptedAmount = getBadCiphertext(),
            .err = temBAD_CIPHERTEXT,
        });
    }

    void
    testConfidentialMPTMirrorUpdatePreclaim(FeatureBitset features)
    {
        testcase("ConfidentialMPTMirrorUpdate preclaim");
        using namespace test::jtx;

        Buffer const& validCipher = getTrivialCiphertext();

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

        // The issuance has not enabled confidential balances.
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

        // A lock does not block a migration.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const newIssuerKey("newIssuerKey");
            Account const newerIssuerKey("newerIssuerKey");

            ConfidentialEnv ct{env, alice, {{.account = bob}, {.account = carol}}};
            ct.mpt.set({.account = alice, .holder = bob, .flags = tfMPTLock});
            ct.mpt.set({.account = alice, .holder = carol, .flags = tfMPTLock});

            // Rotate the issuer key so both holders' mirrors are stale.
            ct.mpt.generateKeyPair(newIssuerKey);
            ct.mpt.set({.account = alice, .issuerPubKey = ct.mpt.getPubKey(newIssuerKey)});

            // The issuer migrates an individually locked holder.
            ct.mpt.mirrorUpdate({
                .account = alice,
                .holder = bob,
                .issuerEncryptedAmount = validCipher,
                .err = tesSUCCESS,
            });

            // An individually locked holder migrates itself.
            ct.mpt.mirrorUpdate({
                .account = carol,
                .issuerEncryptedAmount = validCipher,
                .err = tesSUCCESS,
            });

            // Release the individual locks and lock the whole issuance instead. Rotate again so
            // both mirrors are stale once more.
            ct.mpt.set({.account = alice, .holder = bob, .flags = tfMPTUnlock});
            ct.mpt.set({.account = alice, .holder = carol, .flags = tfMPTUnlock});
            ct.mpt.set({.account = alice, .flags = tfMPTLock});
            ct.mpt.generateKeyPair(newerIssuerKey);
            ct.mpt.set({.account = alice, .issuerPubKey = ct.mpt.getPubKey(newerIssuerKey)});

            ct.mpt.mirrorUpdate({
                .account = alice,
                .holder = bob,
                .issuerEncryptedAmount = validCipher,
                .err = tesSUCCESS,
            });

            ct.mpt.mirrorUpdate({
                .account = carol,
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
        // and the auditor mirror epoch advances to the issuer key epoch.
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

        // Holder auditor late-registration: the auditor key is registered for the first time (key
        // epoch absent), so the holder setting their initial auditor mirror leaves the auditor
        // mirror epoch absent as well.
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

            // The holder encrypts their own balance under the newly registered auditor key.
            Buffer const auditorCipher =
                ct.mpt.encryptAmount(auditor, amount, generateBlindingFactor());

            ct.mpt.mirrorUpdate({
                .account = bob,
                .auditorEncryptedAmount = auditorCipher,
            });

            auto const sle = env.le(keylet::mptoken(ct.mpt.issuanceID(), bob.id()));
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT(strHex((*sle)[sfAuditorEncryptedBalance]) == strHex(auditorCipher));
            // First-time registration leaves the mirror epoch absent.
            BEAST_EXPECT(!sle->isFieldPresent(sfAuditorKeyMirrorEpoch));
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

    void
    testConfidentialMPTHolderKeyUpdatePreflight(FeatureBitset features)
    {
        testcase("ConfidentialMPTHolderKeyUpdate preflight");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");

        // Both amendments are required: ConfidentialMPTKeyRotation and ConfidentialTransfer.
        if (!features[featureConfidentialMPTKeyRotation] || !features[featureConfidentialTransfer])
        {
            MPTTester mptAlice(env, alice, {.holders = {bob}});
            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer});
            mptAlice.holderKeyUpdate({
                .account = bob,
                .holderPubKey = gMakeZeroBuffer(kEcPubKeyLength),
                .flags = tfHolderKeyRecovery,
                .err = temDISABLED,
            });
            return;
        }

        ConfidentialEnv ct{env, alice, {{.account = bob, .payAmount = 100, .convertAmount = 40}}};

        Account const bobNewKey("bobNewKey");
        ct.mpt.generateKeyPair(bobNewKey);

        // Neither Rotation nor Recovery nor Cancel flag set.
        ct.mpt.holderKeyUpdate({
            .account = bob,
            .holderPubKey = ct.mpt.getPubKey(bobNewKey),
            .flags = 0,
            .err = temINVALID_FLAG,
        });

        // Both Rotation and Recovery flags set.
        ct.mpt.holderKeyUpdate({
            .account = bob,
            .holderPubKey = ct.mpt.getPubKey(bobNewKey),
            .flags = tfHolderKeyRotation | tfHolderKeyRecovery,
            .err = temINVALID_FLAG,
        });

        // Rotation and Cancel flags both set.
        ct.mpt.holderKeyUpdate({
            .account = bob,
            .flags = tfHolderKeyRotation | tfCancelRecovery,
            .err = temINVALID_FLAG,
        });

        // All three mode flags set.
        ct.mpt.holderKeyUpdate({
            .account = bob,
            .flags = tfHolderKeyRotation | tfHolderKeyRecovery | tfCancelRecovery,
            .err = temINVALID_FLAG,
        });

        // The issuer cannot rotate or recover a confidential balance it cannot hold.
        Account const aliceNewKey("aliceNewKey");
        ct.mpt.generateKeyPair(aliceNewKey);
        ct.mpt.holderKeyUpdate({
            .account = alice,
            .holderPubKey = ct.mpt.getPubKey(aliceNewKey),
            .flags = tfHolderKeyRecovery,
            .err = temMALFORMED,
        });

        // HolderEncryptionKey one byte short of the required length.
        ct.mpt.holderKeyUpdate({
            .account = bob,
            .holderPubKey = Buffer(kEcPubKeyLength - 1),
            .flags = tfHolderKeyRecovery,
            .err = temMALFORMED,
        });

        // HolderEncryptionKey the correct length, but not a well-formed
        // compressed secp256k1 point.
        ct.mpt.holderKeyUpdate({
            .account = bob,
            .holderPubKey = gMakeZeroBuffer(kEcPubKeyLength),
            .flags = tfHolderKeyRecovery,
            .err = temMALFORMED,
        });

        // HolderEncryptionKey is entirely absent (as opposed to present with
        // the wrong length or format).
        ct.mpt.holderKeyUpdate({
            .account = bob,
            .flags = tfHolderKeyRecovery,
            .err = temMALFORMED,
        });

        // Rotation mode requires both spending and inbox ciphertexts.
        ct.mpt.holderKeyUpdate({
            .account = bob,
            .holderPubKey = ct.mpt.getPubKey(bobNewKey),
            .omitCiphertexts = true,
            .flags = tfHolderKeyRotation,
            .err = temMALFORMED,
        });

        // Recovery mode must not include ciphertexts.
        ct.mpt.holderKeyUpdate({
            .account = bob,
            .holderPubKey = ct.mpt.getPubKey(bobNewKey),
            .spendingCiphertext = gMakeZeroBuffer(kEcGamalEncryptedTotalLength),
            .inboxCiphertext = gMakeZeroBuffer(kEcGamalEncryptedTotalLength),
            .flags = tfHolderKeyRecovery,
            .err = temMALFORMED,
        });

        // Spending ciphertext has the wrong length.
        ct.mpt.holderKeyUpdate({
            .account = bob,
            .holderPubKey = ct.mpt.getPubKey(bobNewKey),
            .spendingCiphertext = gMakeZeroBuffer(kEcGamalEncryptedTotalLength - 1),
            .flags = tfHolderKeyRotation,
            .err = temBAD_CIPHERTEXT,
        });

        // Inbox ciphertext has the wrong length.
        ct.mpt.holderKeyUpdate({
            .account = bob,
            .holderPubKey = ct.mpt.getPubKey(bobNewKey),
            .inboxCiphertext = gMakeZeroBuffer(kEcGamalEncryptedTotalLength - 1),
            .flags = tfHolderKeyRotation,
            .err = temBAD_CIPHERTEXT,
        });

        // Rotation/Recovery mode requires a ZKProof.
        ct.mpt.holderKeyUpdate({
            .account = bob,
            .holderPubKey = ct.mpt.getPubKey(bobNewKey),
            .omitProof = true,
            .flags = tfHolderKeyRecovery,
            .err = temMALFORMED,
        });

        // Cancel mode must not include HolderEncryptionKey.
        ct.mpt.holderKeyUpdate({
            .account = bob,
            .holderPubKey = ct.mpt.getPubKey(bobNewKey),
            .flags = tfCancelRecovery,
            .err = temMALFORMED,
        });

        // Cancel mode must not include ciphertexts.
        ct.mpt.holderKeyUpdate({
            .account = bob,
            .spendingCiphertext = gMakeZeroBuffer(kEcGamalEncryptedTotalLength),
            .inboxCiphertext = gMakeZeroBuffer(kEcGamalEncryptedTotalLength),
            .flags = tfCancelRecovery,
            .err = temMALFORMED,
        });

        // Cancel mode must not include a ZKProof.
        ct.mpt.holderKeyUpdate({
            .account = bob,
            .proof = gMakeZeroBuffer(1),
            .flags = tfCancelRecovery,
            .err = temMALFORMED,
        });
    }

    void
    testConfidentialMPTHolderKeyUpdatePreclaim(FeatureBitset features)
    {
        testcase("ConfidentialMPTHolderKeyUpdate preclaim");
        using namespace test::jtx;

        // The issuance does not exist.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance,
            });
            // Destroy the issuance to test issuance not found.
            mptAlice.destroy();

            mptAlice.holderKeyUpdate({
                .account = bob,
                .flags = tfCancelRecovery,
                .err = tecOBJECT_NOT_FOUND,
            });
        }

        // The issuance has not enabled confidential balances. The holder is
        // authorized so this reaches the confidential-balance-flag check
        // rather than failing earlier on a missing MPToken.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});
            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer});
            mptAlice.authorize({.account = bob});

            mptAlice.holderKeyUpdate({
                .account = bob,
                .flags = tfCancelRecovery,
                .err = tecNO_PERMISSION,
            });
        }

        // carol exists as an account but was never authorized to hold this
        // issuance, so she has no MPToken for it at all.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const carol("carol");
            MPTTester mptAlice(env, alice);
            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance,
            });
            env.fund(XRP(1000), carol);
            env.close();

            Account const carolNewKey("carolNewKey");
            mptAlice.generateKeyPair(carolNewKey);

            mptAlice.holderKeyUpdate({
                .account = carol,
                .holderPubKey = mptAlice.getPubKey(carolNewKey),
                .flags = tfHolderKeyRecovery,
                .err = tecOBJECT_NOT_FOUND,
            });
        }

        // The holder has an MPToken but no confidential state yet - never
        // registered a key or converted anything.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});
            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance,
            });
            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            Account const bobNewKey("bobNewKey");
            mptAlice.generateKeyPair(bobNewKey);

            mptAlice.holderKeyUpdate({
                .account = bob,
                .holderPubKey = mptAlice.getPubKey(bobNewKey),
                .flags = tfHolderKeyRecovery,
                .err = tecNO_PERMISSION,
            });
        }

        // Submitting the holder's own current key as the "new" key is a no-op.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            ConfidentialEnv ct{
                env, alice, {{.account = bob, .payAmount = 100, .convertAmount = 40}}};

            ct.mpt.holderKeyUpdate({
                .account = bob,
                .holderPubKey = ct.mpt.getPubKey(bob),
                .flags = tfHolderKeyRecovery,
                .err = tecNO_PERMISSION,
            });
        }

        // A second Recovery-mode transaction must not silently overwrite an
        // already-pending RecoveryKey.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            ConfidentialEnv ct{
                env, alice, {{.account = bob, .payAmount = 100, .convertAmount = 40}}};

            Account const bobRecoveryKey("bobRecoveryKey");
            ct.mpt.generateKeyPair(bobRecoveryKey);
            ct.mpt.holderKeyUpdate({
                .account = bob,
                .holderPubKey = ct.mpt.getPubKey(bobRecoveryKey),
                .flags = tfHolderKeyRecovery,
            });

            Account const bobRecoveryKey2("bobRecoveryKey2");
            ct.mpt.generateKeyPair(bobRecoveryKey2);
            ct.mpt.holderKeyUpdate({
                .account = bob,
                .holderPubKey = ct.mpt.getPubKey(bobRecoveryKey2),
                .flags = tfHolderKeyRecovery,
                .err = tecNO_PERMISSION,
            });
        }

        // bob never submitted a Recovery-mode transaction, so there is no
        // sfRecoveryKey to cancel.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            ConfidentialEnv ct{
                env, alice, {{.account = bob, .payAmount = 100, .convertAmount = 40}}};

            ct.mpt.holderKeyUpdate({
                .account = bob,
                .flags = tfCancelRecovery,
                .err = tecNO_PERMISSION,
            });
        }
    }

    void
    testConfidentialMPTHolderKeyUpdateDoApply(FeatureBitset features)
    {
        testcase("ConfidentialMPTHolderKeyUpdate doApply");
        using namespace test::jtx;

        // Rotation mode updates the key and re-encrypted balances, bumps the
        // version, and clears any pending recovery.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            ConfidentialEnv ct{
                env, alice, {{.account = bob, .payAmount = 100, .convertAmount = 40}}};

            auto const prevVersion = ct.mpt.getMPTokenVersion(bob);

            Account const bobNewKey("bobNewKey");
            ct.mpt.generateKeyPair(bobNewKey);

            ct.mpt.holderKeyUpdate({
                .account = bob,
                .holderPubKey = ct.mpt.getPubKey(bobNewKey),
                .flags = tfHolderKeyRotation,
            });

            auto const sleMptoken = env.le(keylet::mptoken(ct.mpt.issuanceID(), bob.id()));
            if (!BEAST_EXPECT(sleMptoken))
                return;

            auto const newPubKey = ct.mpt.getPubKey(bobNewKey);
            BEAST_EXPECT(
                newPubKey && strHex((*sleMptoken)[sfHolderEncryptionKey]) == strHex(*newPubKey));
            BEAST_EXPECT(!sleMptoken->isFieldPresent(sfRecoveryKey));
            BEAST_EXPECT(ct.mpt.getMPTokenVersion(bob) == prevVersion + 1);

            // The rotated balances must still decrypt to the same amounts,
            // but now only under the NEW private key.
            auto const spendingCt =
                ct.mpt.getEncryptedBalance(bob, MPTTester::holderEncryptedSpending);
            auto const inboxCt = ct.mpt.getEncryptedBalance(bob, MPTTester::holderEncryptedInbox);
            BEAST_EXPECT(spendingCt.has_value());
            BEAST_EXPECT(inboxCt.has_value());
            if (!spendingCt || !inboxCt)
                return;

            auto const spendingAmt = ct.mpt.decryptAmount(bobNewKey, *spendingCt);
            auto const inboxAmt = ct.mpt.decryptAmount(bobNewKey, *inboxCt);
            BEAST_EXPECT(spendingAmt && *spendingAmt == 40);
            BEAST_EXPECT(inboxAmt && *inboxAmt == 0);
        }

        // Recovery mode only records the pending recovery key; the current
        // key, balances, and version are untouched.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            ConfidentialEnv ct{
                env, alice, {{.account = bob, .payAmount = 100, .convertAmount = 40}}};

            auto const prevVersion = ct.mpt.getMPTokenVersion(bob);
            auto const prevSpending =
                ct.mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending);
            auto const prevInbox = ct.mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedInbox);
            auto const prevKey = ct.mpt.getPubKey(bob);

            Account const bobRecoveryKey("bobRecoveryKey");
            ct.mpt.generateKeyPair(bobRecoveryKey);

            ct.mpt.holderKeyUpdate({
                .account = bob,
                .holderPubKey = ct.mpt.getPubKey(bobRecoveryKey),
                .flags = tfHolderKeyRecovery,
            });

            auto const sleMptoken = env.le(keylet::mptoken(ct.mpt.issuanceID(), bob.id()));
            if (!BEAST_EXPECT(sleMptoken))
                return;

            auto const recoveryKey = ct.mpt.getPubKey(bobRecoveryKey);
            BEAST_EXPECT(
                sleMptoken->isFieldPresent(sfRecoveryKey) && recoveryKey &&
                strHex((*sleMptoken)[sfRecoveryKey]) == strHex(*recoveryKey));

            BEAST_EXPECT(
                prevKey && strHex((*sleMptoken)[sfHolderEncryptionKey]) == strHex(*prevKey));
            BEAST_EXPECT(ct.mpt.getMPTokenVersion(bob) == prevVersion);
            BEAST_EXPECT(
                ct.mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) ==
                prevSpending);
            BEAST_EXPECT(
                ct.mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedInbox) == prevInbox);
        }

        // Cancel mode clears the pending recovery key only; the current key,
        // balances, and version are untouched.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            ConfidentialEnv ct{
                env, alice, {{.account = bob, .payAmount = 100, .convertAmount = 40}}};

            Account const bobRecoveryKey("bobRecoveryKey");
            ct.mpt.generateKeyPair(bobRecoveryKey);

            ct.mpt.holderKeyUpdate({
                .account = bob,
                .holderPubKey = ct.mpt.getPubKey(bobRecoveryKey),
                .flags = tfHolderKeyRecovery,
            });

            auto const prevVersion = ct.mpt.getMPTokenVersion(bob);
            auto const prevSpending =
                ct.mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending);
            auto const prevInbox = ct.mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedInbox);
            auto const prevKey = ct.mpt.getPubKey(bob);

            ct.mpt.holderKeyUpdate({
                .account = bob,
                .flags = tfCancelRecovery,
            });

            auto const sleMptoken = env.le(keylet::mptoken(ct.mpt.issuanceID(), bob.id()));
            if (!BEAST_EXPECT(sleMptoken))
                return;

            BEAST_EXPECT(!sleMptoken->isFieldPresent(sfRecoveryKey));
            BEAST_EXPECT(
                prevKey && strHex((*sleMptoken)[sfHolderEncryptionKey]) == strHex(*prevKey));
            BEAST_EXPECT(ct.mpt.getMPTokenVersion(bob) == prevVersion);
            BEAST_EXPECT(
                ct.mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) ==
                prevSpending);
            BEAST_EXPECT(
                ct.mpt.getDecryptedBalance(bob, MPTTester::holderEncryptedInbox) == prevInbox);
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

        testConfidentialMPTMirrorUpdatePreflight(all);
        testConfidentialMPTMirrorUpdatePreflight(all - featureConfidentialMPTKeyRotation);
        testConfidentialMPTMirrorUpdatePreflight(all - featureConfidentialTransfer);
        testConfidentialMPTMirrorUpdatePreclaim(all);
        testConfidentialMPTMirrorUpdateDoApply(all);
        testConfidentialMPTMirrorUpdateMultipleRotationsIssuerMode(all);
        testConfidentialMPTMirrorUpdateMultipleRotationsHolderMode(all);

        testConfidentialMPTHolderKeyUpdatePreflight(all);
        testConfidentialMPTHolderKeyUpdatePreflight(all - featureConfidentialMPTKeyRotation);
        testConfidentialMPTHolderKeyUpdatePreflight(all - featureConfidentialTransfer);
        testConfidentialMPTHolderKeyUpdatePreclaim(all);
        testConfidentialMPTHolderKeyUpdateDoApply(all);
    }
};

BEAST_DEFINE_TESTSUITE(ConfidentialMPTKeyRotation, app, xrpl);

}  // namespace xrpl
