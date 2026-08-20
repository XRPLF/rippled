#include <test/jtx/Account.h>
#include <test/jtx/ConfidentialTransfer.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/mpt.h>

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace xrpl {

class ConfidentialMPTHolderKeyUpdate_test : public ConfidentialTransferTestBase
{
    // Creates alice (issuer) and bob (holder), with bob already holding a
    // real ElGamal key and 40 confidential spending balance.
    static void
    setupBobWithConfidentialBalance(
        test::jtx::Env& env,
        test::jtx::MPTTester& mptAlice,
        test::jtx::Account const& alice,
        test::jtx::Account const& bob)
    {
        using namespace test::jtx;

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance,
        });

        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 100);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        mptAlice.convert({
            .account = bob,
            .amt = 40,
            .holderPubKey = mptAlice.getPubKey(bob),
        });
        mptAlice.mergeInbox({.account = bob});
    }

    void
    testPreflightFlags(FeatureBitset features)
    {
        testcase("HolderKeyUpdate preflight: flags");
        using namespace test::jtx;

        // Neither Rotation and Recovery flag set.
        {
            Env env{*this, features};
            Account const alice("alice"), bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});
            setupBobWithConfidentialBalance(env, mptAlice, alice, bob);

            Account const bobNewKey("bobNewKey");
            mptAlice.generateKeyPair(bobNewKey);

            mptAlice.holderKeyUpdate({
                .account = bob,
                .holderPubKey = mptAlice.getPubKey(bobNewKey),
                .flags = 0,
                .err = temINVALID_FLAG,
            });
        }

        // Both Rotation and Recovery flags set.
        {
            Env env{*this, features};
            Account const alice("alice"), bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});
            setupBobWithConfidentialBalance(env, mptAlice, alice, bob);

            Account const bobNewKey("bobNewKey");
            mptAlice.generateKeyPair(bobNewKey);

            mptAlice.holderKeyUpdate({
                .account = bob,
                .holderPubKey = mptAlice.getPubKey(bobNewKey),
                .flags = tfHolderKeyRotation | tfHolderKeyRecovery,
                .err = temINVALID_FLAG,
            });
        }
    }

    void
    testPreflightIssuerCannotHoldConfidential(FeatureBitset features)
    {
        testcase("HolderKeyUpdate preflight: issuer cannot rotate/recover");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice"), bob("bob");
        MPTTester mptAlice(env, alice, {.holders = {bob}});
        setupBobWithConfidentialBalance(env, mptAlice, alice, bob);

        Account const aliceNewKey("aliceNewKey");
        mptAlice.generateKeyPair(aliceNewKey);

        mptAlice.holderKeyUpdate({
            .account = alice,
            .holderPubKey = mptAlice.getPubKey(aliceNewKey),
            .flags = tfHolderKeyRecovery,
            .err = temMALFORMED,
        });
    }

    void
    testPreflightHolderKeyLength(FeatureBitset features)
    {
        testcase("HolderKeyUpdate preflight: HolderEncryptionKey length");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice"), bob("bob");
        MPTTester mptAlice(env, alice, {.holders = {bob}});
        setupBobWithConfidentialBalance(env, mptAlice, alice, bob);

        mptAlice.holderKeyUpdate({
            .account = bob,
            .holderPubKey = Buffer(kEcPubKeyLength - 1),  // one byte short of kEcPubKeyLength
            .flags = tfHolderKeyRecovery,
            .err = temMALFORMED,
        });
    }

    void
    testPreflightRotationRequiresCiphertexts(FeatureBitset features)
    {
        testcase("HolderKeyUpdate preflight: Rotation mode requires spending/inbox ciphertexts");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice"), bob("bob");
        MPTTester mptAlice(env, alice, {.holders = {bob}});
        setupBobWithConfidentialBalance(env, mptAlice, alice, bob);

        Account const bobNewKey("bobNewKey");
        mptAlice.generateKeyPair(bobNewKey);

        mptAlice.holderKeyUpdate({
            .account = bob,
            .holderPubKey = mptAlice.getPubKey(bobNewKey),
            .omitCiphertexts = true,
            .flags = tfHolderKeyRotation,
            .err = temMALFORMED,
        });
    }

    void
    testPreflightRecoveryRejectsCiphertexts(FeatureBitset features)
    {
        testcase("HolderKeyUpdate preflight: Recovery mode must not include ciphertexts");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice"), bob("bob");
        MPTTester mptAlice(env, alice, {.holders = {bob}});
        setupBobWithConfidentialBalance(env, mptAlice, alice, bob);

        Account const bobNewKey("bobNewKey");
        mptAlice.generateKeyPair(bobNewKey);

        mptAlice.holderKeyUpdate({
            .account = bob,
            .holderPubKey = mptAlice.getPubKey(bobNewKey),
            .spendingCiphertext = gMakeZeroBuffer(kEcGamalEncryptedTotalLength),
            .inboxCiphertext = gMakeZeroBuffer(kEcGamalEncryptedTotalLength),
            .flags = tfHolderKeyRecovery,
            .err = temMALFORMED,
        });
    }

    void
    testPreflightBadCiphertext(FeatureBitset features)
    {
        testcase("HolderKeyUpdate preflight: invalid ciphertext");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice"), bob("bob");
        MPTTester mptAlice(env, alice, {.holders = {bob}});
        setupBobWithConfidentialBalance(env, mptAlice, alice, bob);

        Account const bobNewKey("bobNewKey");
        mptAlice.generateKeyPair(bobNewKey);

        // Wrong-length spending ciphertext.
        mptAlice.holderKeyUpdate({
            .account = bob,
            .holderPubKey = mptAlice.getPubKey(bobNewKey),
            .spendingCiphertext = gMakeZeroBuffer(kEcGamalEncryptedTotalLength - 1),
            .flags = tfHolderKeyRotation,
            .err = temBAD_CIPHERTEXT,
        });
    }

    void
    testPreflightProof(FeatureBitset features)
    {
        testcase("HolderKeyUpdate preflight: ZKProof length");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice"), bob("bob");
        MPTTester mptAlice(env, alice, {.holders = {bob}});
        setupBobWithConfidentialBalance(env, mptAlice, alice, bob);

        Account const bobNewKey("bobNewKey");
        mptAlice.generateKeyPair(bobNewKey);

        mptAlice.holderKeyUpdate({
            .account = bob,
            .holderPubKey = mptAlice.getPubKey(bobNewKey),
            .proof = gMakeZeroBuffer(kEcHolderKeyRecoveryProofLength - 1),
            .flags = tfHolderKeyRecovery,
            .err = temMALFORMED,
        });
    }

    void
    testPreclaimObjectsExist(FeatureBitset features)
    {
        testcase("HolderKeyUpdate preclaim: issuance/MPToken must exist");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice"), bob("bob"), carol("carol");
        MPTTester mptAlice(env, alice, {.holders = {bob}});
        setupBobWithConfidentialBalance(env, mptAlice, alice, bob);

        Account const bobNewKey("bobNewKey");
        mptAlice.generateKeyPair(bobNewKey);

        // carol exists as an account but was never authorized to hold this
        // issuance, so she has no MPToken for it at all.
        env.fund(XRP(1000), carol);
        env.close();

        mptAlice.holderKeyUpdate({
            .account = carol,
            .holderPubKey = mptAlice.getPubKey(bobNewKey),
            .flags = tfHolderKeyRecovery,
            .err = tecOBJECT_NOT_FOUND,
        });
    }

    void
    testPreclaimMissingConfidentialState(FeatureBitset features)
    {
        testcase("HolderKeyUpdate preclaim: holder missing confidential state");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice"), bob("bob");
        MPTTester mptAlice(env, alice, {.holders = {bob}});

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance,
        });
        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 100);

        // bob holds the token but has never registered a confidential key or
        // converted anything, so his MPToken has no confidential fields yet.
        Account const bobNewKey("bobNewKey");
        mptAlice.generateKeyPair(bobNewKey);

        mptAlice.holderKeyUpdate({
            .account = bob,
            .holderPubKey = mptAlice.getPubKey(bobNewKey),
            .flags = tfHolderKeyRecovery,
            .err = tecNO_PERMISSION,
        });
    }

    void
    testPreclaimNoopKeyChange(FeatureBitset features)
    {
        testcase("HolderKeyUpdate preclaim: no-op key change");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice"), bob("bob");
        MPTTester mptAlice(env, alice, {.holders = {bob}});
        setupBobWithConfidentialBalance(env, mptAlice, alice, bob);

        // Submitting bob's own current key as the "new" key.
        mptAlice.holderKeyUpdate({
            .account = bob,
            .holderPubKey = mptAlice.getPubKey(bob),
            .flags = tfHolderKeyRecovery,
            .err = tecNO_PERMISSION,
        });
    }

    void
    testRotationSucceeds(FeatureBitset features)
    {
        testcase("HolderKeyUpdate: Rotation mode updates key and balances");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice"), bob("bob");
        MPTTester mptAlice(env, alice, {.holders = {bob}});
        setupBobWithConfidentialBalance(env, mptAlice, alice, bob);

        auto const prevVersion = mptAlice.getMPTokenVersion(bob);

        Account const bobNewKey("bobNewKey");
        mptAlice.generateKeyPair(bobNewKey);

        mptAlice.holderKeyUpdate({
            .account = bob,
            .holderPubKey = mptAlice.getPubKey(bobNewKey),
            .flags = tfHolderKeyRotation,
        });

        auto const sleMptoken = env.le(keylet::mptoken(mptAlice.issuanceID(), bob.id()));
        if (!BEAST_EXPECT(sleMptoken))
            return;

        auto const newPubKey = mptAlice.getPubKey(bobNewKey);
        BEAST_EXPECT(
            newPubKey && strHex((*sleMptoken)[sfHolderEncryptionKey]) == strHex(*newPubKey));
        BEAST_EXPECT(!sleMptoken->isFieldPresent(sfRecoveryKey));
        BEAST_EXPECT(mptAlice.getMPTokenVersion(bob) == prevVersion + 1);

        // The rotated balances must still decrypt to the same amounts, but
        // now only under the NEW private key (registered under bobNewKey).
        auto const spendingCt =
            mptAlice.getEncryptedBalance(bob, MPTTester::holderEncryptedSpending);
        auto const inboxCt = mptAlice.getEncryptedBalance(bob, MPTTester::holderEncryptedInbox);
        if (!BEAST_EXPECT(spendingCt.has_value()) || !BEAST_EXPECT(inboxCt.has_value()))
            return;

        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        auto const spendingAmt = mptAlice.decryptAmount(bobNewKey, *spendingCt);
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        auto const inboxAmt = mptAlice.decryptAmount(bobNewKey, *inboxCt);
        BEAST_EXPECT(spendingAmt && *spendingAmt == 40);
        BEAST_EXPECT(inboxAmt && *inboxAmt == 0);
    }

    void
    testRecoverySucceeds(FeatureBitset features)
    {
        testcase("HolderKeyUpdate: Recovery mode records pending recovery key only");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice"), bob("bob");
        MPTTester mptAlice(env, alice, {.holders = {bob}});
        setupBobWithConfidentialBalance(env, mptAlice, alice, bob);

        auto const prevVersion = mptAlice.getMPTokenVersion(bob);
        auto const prevSpending =
            mptAlice.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending);
        auto const prevInbox = mptAlice.getDecryptedBalance(bob, MPTTester::holderEncryptedInbox);
        auto const prevKey = mptAlice.getPubKey(bob);

        Account const bobRecoveryKey("bobRecoveryKey");
        mptAlice.generateKeyPair(bobRecoveryKey);

        mptAlice.holderKeyUpdate({
            .account = bob,
            .holderPubKey = mptAlice.getPubKey(bobRecoveryKey),
            .flags = tfHolderKeyRecovery,
        });

        auto const sleMptoken = env.le(keylet::mptoken(mptAlice.issuanceID(), bob.id()));
        if (!BEAST_EXPECT(sleMptoken))
            return;

        auto const recoveryKey = mptAlice.getPubKey(bobRecoveryKey);
        BEAST_EXPECT(
            sleMptoken->isFieldPresent(sfRecoveryKey) && recoveryKey &&
            strHex((*sleMptoken)[sfRecoveryKey]) == strHex(*recoveryKey));

        // The holder's encryption key and balances are untouched; still
        // decryptable with the ORIGINAL private key, and version unchanged.
        BEAST_EXPECT(prevKey && strHex((*sleMptoken)[sfHolderEncryptionKey]) == strHex(*prevKey));
        BEAST_EXPECT(mptAlice.getMPTokenVersion(bob) == prevVersion);
        BEAST_EXPECT(
            mptAlice.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) == prevSpending);
        BEAST_EXPECT(
            mptAlice.getDecryptedBalance(bob, MPTTester::holderEncryptedInbox) == prevInbox);
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testPreflightFlags(features);
        testPreflightIssuerCannotHoldConfidential(features);
        testPreflightHolderKeyLength(features);
        testPreflightRotationRequiresCiphertexts(features);
        testPreflightRecoveryRejectsCiphertexts(features);
        testPreflightBadCiphertext(features);
        testPreflightProof(features);

        testPreclaimObjectsExist(features);
        testPreclaimMissingConfidentialState(features);
        testPreclaimNoopKeyChange(features);

        testRotationSucceeds(features);
        testRecoverySucceeds(features);

        // Not yet coverable: preclaim's Schnorr PoK / Compact Pedersen
        // equality proof failures both return tecBAD_PROOF, but
        // verifyHolderKeyUpdateProof is currently a placeholder that always
        // returns tesSUCCESS (see ConfidentialTransfer.cpp). Tests for those
        // two cases should be added once real crypto-side verification lands.
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{testableAmendments()};
        testWithFeats(all);
    }
};

BEAST_DEFINE_TESTSUITE(ConfidentialMPTHolderKeyUpdate, app, xrpl);

}  // namespace xrpl
