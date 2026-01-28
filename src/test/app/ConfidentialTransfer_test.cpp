#include <test/jtx.h>
#include <test/jtx/trust.h>

#include <xrpl/protocol/ConfidentialTransfer.h>

#include <openssl/rand.h>

namespace ripple {

class ConfidentialTransfer_test : public beast::unit_test::suite
{
    // Get a bad ciphertext with valid structure but cryptographic invalid for
    // testing purposes. For preflight test purposes.
    static Buffer const&
    getBadCiphertext()
    {
        static Buffer const badCiphertext = []() {
            Buffer buf(ecGamalEncryptedTotalLength);
            std::memset(buf.data(), 0xFF, ecGamalEncryptedTotalLength);

            buf.data()[0] = 0x02;
            buf.data()[ecGamalEncryptedLength] = 0x02;
            return buf;
        }();

        return badCiphertext;
    }

    // Get a trivial buffer that is structurally and mathematically valid, but
    // contains invalid data that does not match the ledger state. For preclaim
    // test purposes.
    static Buffer const&
    getTrivialCiphertext()
    {
        static Buffer const trivialCiphertext = []() {
            Buffer buf(ecGamalEncryptedTotalLength);
            std::memset(buf.data(), 0, ecGamalEncryptedTotalLength);

            buf.data()[0] = 0x02;
            buf.data()[ecGamalEncryptedLength] = 0x02;

            buf.data()[ecGamalEncryptedLength - 1] = 0x01;
            buf.data()[ecGamalEncryptedTotalLength - 1] = 0x01;

            return buf;
        }();

        return trivialCiphertext;
    }

    static std::string const&
    getTrivialSendProofHex(size_t nRecipients)
    {
        static std::string const trivialProofHex = [nRecipients]() {
            size_t const sizeEquality =
                getMultiCiphertextEqualityProofSize(nRecipients);
            size_t const totalSize = sizeEquality + (2 * ecPedersenProofLength);

            Buffer buf(totalSize);
            std::memset(buf.data(), 0, totalSize);

            for (std::size_t i = 0; i < totalSize; i += ecGamalEncryptedLength)
            {
                buf.data()[i] = 0x02;
                if (i + ecGamalEncryptedLength - 1 < totalSize)
                    buf.data()[i + ecGamalEncryptedLength - 1] = 0x01;
            }

            return strHex(buf);
        }();

        return trivialProofHex;
    }

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
             .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 100);

        mptAlice.generateKeyPair(alice);

        mptAlice.set(
            {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        mptAlice.generateKeyPair(bob);

        mptAlice.convert({
            .account = bob,
            .amt = 0,
            .holderPubKey = mptAlice.getPubKey(bob),
        });

        mptAlice.convert({
            .account = bob,
            .amt = 20,
        });

        mptAlice.convert({
            .account = bob,
            .amt = 40,
        });

        mptAlice.convert({
            .account = bob,
            .amt = 40,
        });
    }

    void
    testConvertWithAuditor(FeatureBitset features)
    {
        testcase("Convert with auditor");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const auditor("auditor");
        MPTTester mptAlice(env, alice, {.holders = {bob}, .auditor = auditor});

        mptAlice.create(
            {.ownerCount = 1,
             .holderCount = 0,
             .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 100);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(auditor);

        mptAlice.set(
            {.account = alice,
             .issuerPubKey = mptAlice.getPubKey(alice),
             .auditorPubKey = mptAlice.getPubKey(auditor)});

        mptAlice.generateKeyPair(bob);

        mptAlice.convert({
            .account = bob,
            .amt = 0,
            .holderPubKey = mptAlice.getPubKey(bob),
        });

        mptAlice.convert({
            .account = bob,
            .amt = 20,
        });

        mptAlice.convert({
            .account = bob,
            .amt = 30,
        });
    }

    void
    testConvertPreflight(FeatureBitset features)
    {
        testcase("Convert preflight");
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
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set(
                {.account = alice,
                 .issuerPubKey = mptAlice.getPubKey(alice),
                 .err = temDISABLED});

            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
                 .holderPubKey = mptAlice.getPubKey(bob),
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
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.convert(
                {.account = alice,
                 .amt = 10,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .err = temMALFORMED});

            // blinding factor length is invalid
            mptAlice.convert(
                {.account = alice,
                 .amt = 10,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .blindingFactor = Buffer(10),
                 .err = temMALFORMED});

            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .holderEncryptedAmt = Buffer{},
                 .err = temBAD_CIPHERTEXT});

            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .issuerEncryptedAmt = Buffer{},
                 .err = temBAD_CIPHERTEXT});

            mptAlice.convert(
                {.account = bob,
                 .amt = maxMPTokenAmount + 1,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .err = temBAD_AMOUNT});

            mptAlice.convert(
                {.account = bob,
                 .amt = 1,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .holderEncryptedAmt = getBadCiphertext(),
                 .err = temBAD_CIPHERTEXT});

            mptAlice.convert(
                {.account = bob,
                 .amt = 1,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .issuerEncryptedAmt = getBadCiphertext(),
                 .err = temBAD_CIPHERTEXT});

            // invalid pub key
            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
                 .holderPubKey = Buffer{},
                 .err = temMALFORMED});
        }

        // when registering holder pub key, the transaction must include a
        // Schnorr proof of knowledge for the corresponding secret key
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
                 .fillSchnorrProof = false,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .err = temMALFORMED});

            mptAlice.convert(
                {.account = bob,
                 .amt = 0,
                 .fillSchnorrProof = false,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .err = temMALFORMED});

            // proof length is invalid
            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
                 .proof = std::string(10, 'A'),
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .err = temMALFORMED});
        }

        // when holder pub key already registered, Schnorr proof must not be
        // provided
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            // this will register bob's pub key,
            // and convert 10 to confidential balance
            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            // proof must not be provided after pub key was registered
            mptAlice.convert(
                {.account = bob,
                 .amt = 20,
                 .fillSchnorrProof = true,
                 .err = temMALFORMED});
        }
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
                {.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set(
                {.account = alice,
                 .issuerPubKey = mptAlice.getPubKey(alice),
                 .err = temDISABLED});
        }

        // pub key is invalid
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.ownerCount = 1,
                 .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set(
                {.account = alice,
                 .issuerPubKey = Buffer{},
                 .err = temMALFORMED});
        }

        // issuance has disabled confidential transfer
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            // no tfMPTCanPrivacy flag enabled
            mptAlice.create(
                {.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set(
                {.account = alice,
                 .issuerPubKey = mptAlice.getPubKey(alice),
                 .err = tecNO_PERMISSION});
        }
    }

    void
    testConvertPreclaim(FeatureBitset features)
    {
        testcase("Convert preclaim");
        using namespace test::jtx;

        // tfMPTCanPrivacy is not set on issuance
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
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
                 .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
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
                 .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.generateKeyPair(alice);

            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.destroy();
            mptAlice.generateKeyPair(bob);

            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
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
                 .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
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
                 .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            mptAlice.convert(
                {.account = bob,
                 .amt = 200,
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
                 .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
                 .holderPubKey = mptAlice.getPubKey(bob)});

            // cannot upload pk again
            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
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
                 .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.set({.account = alice, .holder = bob, .flags = tfMPTLock});

            mptAlice.generateKeyPair(bob);

            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .err = tecINSUFFICIENT_FUNDS});

            mptAlice.set(
                {.account = alice, .holder = bob, .flags = tfMPTUnlock});

            mptAlice.convert({
                .account = bob,
                .amt = 10,
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
                 .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTRequireAuth |
                     tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = alice, .holder = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            // Unauthorize bob
            mptAlice.authorize(
                {.account = alice, .holder = bob, .flags = tfMPTUnauthorize});

            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
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
                .holderPubKey = mptAlice.getPubKey(bob),
            });
        }

        // invalid proof when registering holder pub key
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
                 .proof = std::string(ecSchnorrProofLength * 2, 'A'),
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .err = tecBAD_PROOF});
        }
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
             .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 100);

        mptAlice.generateKeyPair(alice);

        mptAlice.set(
            {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        mptAlice.generateKeyPair(bob);

        mptAlice.convert({
            .account = bob,
            .amt = 40,
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
             .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 100);

        mptAlice.generateKeyPair(alice);

        mptAlice.set(
            {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        mptAlice.generateKeyPair(bob);

        mptAlice.convert({
            .account = bob,
            .amt = 40,
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
                 .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.generateKeyPair(alice);

            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.destroy();
            mptAlice.generateKeyPair(bob);

            mptAlice.mergeInbox({.account = bob, .err = tecOBJECT_NOT_FOUND});
        }

        // tfMPTCanPrivacy is not set on issuance
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
            mptAlice.pay(alice, bob, 100);

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
                 .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.generateKeyPair(alice);

            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

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
                 .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

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
             .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

        mptAlice.authorize({.account = bob});
        mptAlice.authorize({.account = carol});

        mptAlice.pay(alice, bob, 100);
        mptAlice.pay(alice, carol, 50);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);

        mptAlice.set(
            {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        // Convert 60 out of 100
        mptAlice.convert(
            {.account = bob,
             .amt = 60,
             .holderPubKey = mptAlice.getPubKey(bob)});

        // bob merge inbox
        mptAlice.mergeInbox({
            .account = bob,
        });

        // carol convert 20 to confidential
        mptAlice.convert(
            {.account = carol,
             .amt = 20,
             .holderPubKey = mptAlice.getPubKey(carol)});

        // carol merge inbox
        mptAlice.mergeInbox({
            .account = carol,
        });

        // bob sends 10 to carol
        mptAlice.send({
            .account = bob,
            .dest = carol,
            .amt = 10,
        });

        // bob sends 1 to carol again
        mptAlice.send({
            .account = bob,
            .dest = carol,
            .amt = 1,
        });

        mptAlice.mergeInbox({
            .account = carol,
        });

        // carol sends 15 backto bob
        mptAlice.send({
            .account = carol,
            .dest = bob,
            .amt = 15,
        });
    }

    void
    testSendWithAuditor(FeatureBitset features)
    {
        testcase("test confidential send with auditor");
        using namespace test::jtx;
        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const auditor("auditor");
        MPTTester mptAlice(
            env, alice, {.holders = {bob, carol}, .auditor = auditor});

        mptAlice.create(
            {.ownerCount = 1,
             .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

        mptAlice.authorize({.account = bob});
        mptAlice.authorize({.account = carol});

        mptAlice.pay(alice, bob, 100);
        mptAlice.pay(alice, carol, 50);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);
        mptAlice.generateKeyPair(auditor);

        mptAlice.set(
            {.account = alice,
             .issuerPubKey = mptAlice.getPubKey(alice),
             .auditorPubKey = mptAlice.getPubKey(auditor)});

        // Convert 60 out of 100
        mptAlice.convert(
            {.account = bob,
             .amt = 60,
             .holderPubKey = mptAlice.getPubKey(bob)});

        // bob merge inbox
        mptAlice.mergeInbox({
            .account = bob,
        });

        mptAlice.convert(
            {.account = carol,
             .amt = 20,
             .holderPubKey = mptAlice.getPubKey(carol)});

        // carol merge inbox
        mptAlice.mergeInbox({
            .account = carol,
        });

        // bob sends 10 to carol
        mptAlice.send({.account = bob, .dest = carol, .amt = 10});

        // bob sends 1 to carol again
        mptAlice.send({.account = bob, .dest = carol, .amt = 1});

        mptAlice.mergeInbox({
            .account = carol,
        });

        // carol sends 15 backto bob
        mptAlice.send({.account = carol, .dest = bob, .amt = 15});
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

            mptAlice.create();
            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});

            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .senderEncryptedAmt = Buffer(ecGamalEncryptedTotalLength),
                 .destEncryptedAmt = Buffer(ecGamalEncryptedTotalLength),
                 .issuerEncryptedAmt = Buffer(ecGamalEncryptedTotalLength),
                 .err = temDISABLED});
        }

        // test malformed
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create(
                {.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});
            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(carol);
            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});
            mptAlice.pay(alice, bob, 100);
            mptAlice.pay(alice, carol, 50);

            mptAlice.convert({
                .account = bob,
                .amt = 50,
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            mptAlice.convert({
                .account = carol,
                .amt = 40,
                .holderPubKey = mptAlice.getPubKey(carol),
            });

            // issuer can not be the same as sender
            mptAlice.send(
                {.account = alice,
                 .dest = carol,
                 .amt = 10,
                 .err = temMALFORMED});

            // can not send to self
            mptAlice.send(
                {.account = bob, .dest = bob, .amt = 10, .err = temMALFORMED});

            // sender encrypted amount wrong length
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .senderEncryptedAmt = Buffer(10),
                 .err = temBAD_CIPHERTEXT});
            // dest encrypted amount wrong length
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .destEncryptedAmt = Buffer(10),
                 .err = temBAD_CIPHERTEXT});
            // issuer encrypted amount wrong length
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .issuerEncryptedAmt = Buffer(10),
                 .err = temBAD_CIPHERTEXT});

            // sender encrypted amount malformed
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = getTrivialSendProofHex(3),
                 .senderEncryptedAmt = Buffer(ecGamalEncryptedTotalLength),
                 .amountCommitment = Buffer(ecPedersenCommitmentLength),
                 .balanceCommitment = Buffer(ecPedersenCommitmentLength),
                 .err = temBAD_CIPHERTEXT});
            // dest encrypted amount malformed
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = getTrivialSendProofHex(3),
                 .destEncryptedAmt = Buffer(ecGamalEncryptedTotalLength),
                 .amountCommitment = Buffer(ecPedersenCommitmentLength),
                 .balanceCommitment = Buffer(ecPedersenCommitmentLength),
                 .err = temBAD_CIPHERTEXT});
            // issuer encrypted amount malformed
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = getTrivialSendProofHex(3),
                 .issuerEncryptedAmt = Buffer(ecGamalEncryptedTotalLength),
                 .amountCommitment = Buffer(ecPedersenCommitmentLength),
                 .balanceCommitment = Buffer(ecPedersenCommitmentLength),
                 .err = temBAD_CIPHERTEXT});

            // invalid proof length
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = std::string(10, 'A'),
                 .amountCommitment = Buffer(ecPedersenCommitmentLength),
                 .balanceCommitment = Buffer(ecPedersenCommitmentLength),
                 .err = temMALFORMED});

            // invalid amount Pedersen commitment length
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = getTrivialSendProofHex(3),
                 .amountCommitment = Buffer(100),
                 .balanceCommitment = Buffer(ecPedersenCommitmentLength),
                 .err = temMALFORMED});

            // invalid balance Pedersen commitment length
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = getTrivialSendProofHex(3),
                 .amountCommitment = Buffer(ecPedersenCommitmentLength),
                 .balanceCommitment = Buffer(100),
                 .err = temMALFORMED});
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
            {.flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTRequireAuth |
                 tfMPTCanPrivacy});
        mptAlice.authorize({.account = bob});
        mptAlice.authorize({.account = alice, .holder = bob});
        mptAlice.authorize({.account = carol});
        mptAlice.authorize({.account = alice, .holder = carol});
        mptAlice.authorize({.account = dave});
        mptAlice.authorize({.account = alice, .holder = dave});

        // fund bob, carol (not dave or eve)
        mptAlice.pay(alice, bob, 100);
        mptAlice.pay(alice, carol, 50);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);
        mptAlice.generateKeyPair(dave);
        mptAlice.set(
            {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        // bob and carol convert some funds to confidential
        mptAlice.convert(
            {.account = bob,
             .amt = 60,
             .holderPubKey = mptAlice.getPubKey(bob),
             .err = tesSUCCESS});
        mptAlice.convert(
            {.account = carol,
             .amt = 20,
             .holderPubKey = mptAlice.getPubKey(carol),
             .err = tesSUCCESS});

        // bob and carol merge inbox
        mptAlice.mergeInbox({
            .account = bob,
        });
        mptAlice.mergeInbox({
            .account = carol,
        });

        // issuance not found
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create({.flags = tfMPTCanTransfer | tfMPTCanPrivacy});
            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});
            mptAlice.generateKeyPair(alice);
            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            // destroy the issuance
            mptAlice.destroy();

            Json::Value jv;
            jv[jss::Account] = bob.human();
            jv[jss::Destination] = carol.human();
            jv[jss::TransactionType] = jss::ConfidentialSend;
            jv[sfMPTokenIssuanceID] = to_string(mptAlice.issuanceID());
            jv[sfSenderEncryptedAmount] = strHex(getTrivialCiphertext());
            jv[sfDestinationEncryptedAmount] = strHex(getTrivialCiphertext());
            jv[sfIssuerEncryptedAmount] = strHex(getTrivialCiphertext());
            jv[sfAmountCommitment] = strHex(Buffer(ecPedersenCommitmentLength));
            jv[sfBalanceCommitment] =
                strHex(Buffer(ecPedersenCommitmentLength));
            jv[sfZKProof] = getTrivialSendProofHex(3);

            env(jv, ter(tecOBJECT_NOT_FOUND));
        }

        // destination does not exist
        {
            Account const unknown("unknown");
            mptAlice.send(
                {.account = bob,
                 .dest = unknown,
                 .amt = 10,
                 .senderEncryptedAmt = getTrivialCiphertext(),
                 .destEncryptedAmt = getTrivialCiphertext(),
                 .issuerEncryptedAmt = getTrivialCiphertext(),
                 .proof = getTrivialSendProofHex(3),
                 .amountCommitment = Buffer(ecPedersenCommitmentLength),
                 .balanceCommitment = Buffer(ecPedersenCommitmentLength),
                 .err = tecNO_TARGET});
        }

        // dave exists, but has no confidential fields (never converted)
        {
            mptAlice.send(
                {.account = bob,
                 .dest = dave,
                 .amt = 10,
                 .senderEncryptedAmt = getTrivialCiphertext(),
                 .destEncryptedAmt = getTrivialCiphertext(),
                 .issuerEncryptedAmt = getTrivialCiphertext(),
                 .proof = getTrivialSendProofHex(3),
                 .amountCommitment = Buffer(ecPedersenCommitmentLength),
                 .balanceCommitment = Buffer(ecPedersenCommitmentLength),
                 .err = tecNO_PERMISSION});
            mptAlice.send(
                {.account = dave,
                 .dest = carol,
                 .amt = 10,
                 .senderEncryptedAmt = getTrivialCiphertext(),
                 .destEncryptedAmt = getTrivialCiphertext(),
                 .issuerEncryptedAmt = getTrivialCiphertext(),
                 .proof = getTrivialSendProofHex(3),
                 .amountCommitment = Buffer(ecPedersenCommitmentLength),
                 .balanceCommitment = Buffer(ecPedersenCommitmentLength),
                 .err = tecNO_PERMISSION});
        }

        // destination exists but has no MPT object.
        {
            mptAlice.send(
                {.account = bob,
                 .dest = eve,
                 .amt = 10,
                 .senderEncryptedAmt = getTrivialCiphertext(),
                 .destEncryptedAmt = getTrivialCiphertext(),
                 .issuerEncryptedAmt = getTrivialCiphertext(),
                 .proof = getTrivialSendProofHex(3),
                 .amountCommitment = Buffer(ecPedersenCommitmentLength),
                 .balanceCommitment = Buffer(ecPedersenCommitmentLength),
                 .err = tecOBJECT_NOT_FOUND});
        }

        // issuance is locked globally
        {
            // lock issuance
            mptAlice.set({.account = alice, .flags = tfMPTLock});
            mptAlice.send(
                {.account = bob, .dest = carol, .amt = 10, .err = tecLOCKED});
            // unlock issuance
            mptAlice.set({.account = alice, .flags = tfMPTUnlock});
            // now can send
            mptAlice.send({.account = bob, .dest = carol, .amt = 1});
        }

        // sender is locked
        {
            // lock bob
            mptAlice.set({.account = alice, .holder = bob, .flags = tfMPTLock});
            mptAlice.send(
                {.account = bob, .dest = carol, .amt = 10, .err = tecLOCKED});
            // unlock bob
            mptAlice.set(
                {.account = alice, .holder = bob, .flags = tfMPTUnlock});
            // now can send
            mptAlice.send({.account = bob, .dest = carol, .amt = 2});
        }

        // destination is locked
        {
            // lock carol
            mptAlice.set(
                {.account = alice, .holder = carol, .flags = tfMPTLock});
            mptAlice.send(
                {.account = bob, .dest = carol, .amt = 10, .err = tecLOCKED});
            // unlock carol
            mptAlice.set(
                {.account = alice, .holder = carol, .flags = tfMPTUnlock});
            // now can send
            mptAlice.send({.account = bob, .dest = carol, .amt = 3});
        }

        // sender not authorized
        {
            // unauthorize bob
            mptAlice.authorize(
                {.account = alice, .holder = bob, .flags = tfMPTUnauthorize});
            mptAlice.send(
                {.account = bob, .dest = carol, .amt = 10, .err = tecNO_AUTH});
            // authorize bob again
            mptAlice.authorize({
                .account = alice,
                .holder = bob,
            });
            // now can send
            mptAlice.send({.account = bob, .dest = carol, .amt = 4});
        }

        // destination not authorized
        {
            // unauthorize carol
            mptAlice.authorize(
                {.account = alice, .holder = carol, .flags = tfMPTUnauthorize});
            mptAlice.send(
                {.account = bob, .dest = carol, .amt = 10, .err = tecNO_AUTH});
            // authorize carol again
            mptAlice.authorize({
                .account = alice,
                .holder = carol,
            });
            // now can send
            mptAlice.send({.account = bob, .dest = carol, .amt = 5});
        }

        // cannot send when MPTCanTransfer is not set
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create(
                {.ownerCount = 1, .flags = tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});

            mptAlice.pay(alice, bob, 100);
            mptAlice.pay(alice, carol, 50);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(carol);

            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            // Convert 60 out of 100
            mptAlice.convert(
                {.account = bob,
                 .amt = 60,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .err = tesSUCCESS});

            // bob merge inbox
            mptAlice.mergeInbox({
                .account = bob,
            });

            mptAlice.convert(
                {.account = carol,
                 .amt = 20,
                 .holderPubKey = mptAlice.getPubKey(carol),
                 .err = tesSUCCESS});

            // carol merge inbox
            mptAlice.mergeInbox({
                .account = carol,
            });

            // bob sends 10 to carol
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,  // will be encrypted internally
                 .err = tecNO_AUTH});
        }

        // bad proof
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create(
                {.ownerCount = 1,
                 .flags = tfMPTCanLock | tfMPTCanPrivacy | tfMPTCanTransfer});

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});

            mptAlice.pay(alice, bob, 100);
            mptAlice.pay(alice, carol, 50);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(carol);

            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.convert(
                {.account = bob,
                 .amt = 60,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .err = tesSUCCESS});

            mptAlice.mergeInbox({
                .account = bob,
            });

            mptAlice.convert(
                {.account = carol,
                 .amt = 20,
                 .holderPubKey = mptAlice.getPubKey(carol),
                 .err = tesSUCCESS});

            mptAlice.mergeInbox({
                .account = carol,
            });

            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = getTrivialSendProofHex(3),
                 .err = tecBAD_PROOF});
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
                 .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            mptAlice.convert({
                .account = bob,
                .amt = 100,
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
                 .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(carol);

            mptAlice.convert({
                .account = bob,
                .amt = 100,
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            mptAlice.convert({
                .account = carol,
                .amt = 0,
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
                 .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.generateKeyPair(alice);

            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            mptAlice.convert({
                .account = bob,
                .amt = 0,
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            mptAlice.authorize({
                .account = bob,
                .flags = tfMPTUnauthorize,
            });
        }

        // can delete mptoken if issuance has been destroyed and has
        // encrypted zero balance
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.ownerCount = 1,
                 .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.generateKeyPair(alice);

            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            mptAlice.convert({
                .account = bob,
                .amt = 0,
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
             .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 100);

        mptAlice.generateKeyPair(alice);

        mptAlice.set(
            {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        mptAlice.generateKeyPair(bob);

        mptAlice.convert({
            .account = bob,
            .amt = 40,
            .holderPubKey = mptAlice.getPubKey(bob),
        });

        mptAlice.mergeInbox({
            .account = bob,
        });

        mptAlice.convertBack({
            .account = bob,
            .amt = 30,
        });
    }

    void
    testConvertBackWithAuditor(FeatureBitset features)
    {
        testcase("Convert back with auditor");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const auditor("auditor");
        MPTTester mptAlice(env, alice, {.holders = {bob}, .auditor = auditor});

        mptAlice.create(
            {.ownerCount = 1,
             .holderCount = 0,
             .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 100);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(auditor);

        mptAlice.set(
            {.account = alice,
             .issuerPubKey = mptAlice.getPubKey(alice),
             .auditorPubKey = mptAlice.getPubKey(auditor)});

        mptAlice.generateKeyPair(bob);

        mptAlice.convert({
            .account = bob,
            .amt = 40,
            .holderPubKey = mptAlice.getPubKey(bob),
        });

        mptAlice.mergeInbox({
            .account = bob,
        });

        mptAlice.convertBack({
            .account = bob,
            .amt = 30,
        });
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
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.convertBack(
                {.account = bob, .amt = 30, .err = temDISABLED});
        }

        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            mptAlice.convert({
                .account = bob,
                .amt = 40,
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            mptAlice.mergeInbox({
                .account = bob,
            });

            mptAlice.convertBack(
                {.account = alice, .amt = 30, .err = temMALFORMED});

            mptAlice.convertBack(
                {.account = bob, .amt = 0, .err = temBAD_AMOUNT});

            mptAlice.convertBack(
                {.account = bob,
                 .amt = maxMPTokenAmount + 1,
                 .err = temBAD_AMOUNT});

            // invalid blinding factor length
            mptAlice.convertBack(
                {.account = alice,
                 .amt = 30,
                 .blindingFactor = Buffer{},
                 .err = temMALFORMED});

            mptAlice.convertBack(
                {.account = bob,
                 .amt = 30,
                 .holderEncryptedAmt = Buffer{},
                 .err = temBAD_CIPHERTEXT});

            mptAlice.convertBack(
                {.account = bob,
                 .amt = 30,
                 .issuerEncryptedAmt = Buffer{},
                 .err = temBAD_CIPHERTEXT});

            mptAlice.convertBack(
                {.account = bob,
                 .amt = 30,
                 .holderEncryptedAmt = getBadCiphertext(),
                 .err = temBAD_CIPHERTEXT});

            mptAlice.convertBack(
                {.account = bob,
                 .amt = 30,
                 .issuerEncryptedAmt = getBadCiphertext(),
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
                 .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.generateKeyPair(alice);

            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.destroy();
            mptAlice.generateKeyPair(bob);

            mptAlice.convertBack(
                {.account = bob, .amt = 30, .err = tecOBJECT_NOT_FOUND});
        }

        // tfMPTCanPrivacy is not set on issuance
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
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.convertBack(
                {.account = bob, .amt = 30, .err = tecNO_PERMISSION});
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
                 .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.convertBack(
                {.account = bob, .amt = 30, .err = tecOBJECT_NOT_FOUND});
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
                 .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            mptAlice.convertBack(
                {.account = bob, .amt = 30, .err = tecNO_PERMISSION});
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
                 .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});
            mptAlice.pay(alice, bob, 100);
            mptAlice.pay(alice, carol, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(carol);

            mptAlice.convert({
                .account = bob,
                .amt = 40,
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            mptAlice.mergeInbox({
                .account = bob,
            });

            mptAlice.convert({
                .account = carol,
                .amt = 40,
                .holderPubKey = mptAlice.getPubKey(carol),
            });

            mptAlice.convertBack(
                {.account = bob, .amt = 300, .err = tecINSUFFICIENT_FUNDS});
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
                 .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTRequireAuth |
                     tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = alice, .holder = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            mptAlice.convert({
                .account = bob,
                .amt = 40,
                .holderPubKey = mptAlice.getPubKey(bob),
            });
            mptAlice.mergeInbox({.account = bob});
            mptAlice.set({.account = alice, .holder = bob, .flags = tfMPTLock});

            mptAlice.convertBack({.account = bob, .amt = 10, .err = tecLOCKED});

            mptAlice.set(
                {.account = alice, .holder = bob, .flags = tfMPTUnlock});

            mptAlice.convertBack({
                .account = bob,
                .amt = 10,
            });

            mptAlice.authorize(
                {.account = alice, .holder = bob, .flags = tfMPTUnauthorize});

            mptAlice.convertBack(
                {.account = bob, .amt = 10, .err = tecNO_AUTH});

            mptAlice.authorize({
                .account = alice,
                .holder = bob,
            });

            mptAlice.convertBack({
                .account = bob,
                .amt = 10,
            });
        }
    }

    void
    testSendDepositPreauth(FeatureBitset features)
    {
        testcase("Send deposit preauth");
        using namespace test::jtx;
        Env env(*this, features);

        using namespace std::chrono;

        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const dpIssuer("dpIssuer");

        env.fund(XRP(50000), dpIssuer);
        env.close();
        char const credType[] = "abcde";
        MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

        mptAlice.create(
            {.ownerCount = 1,
             .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

        mptAlice.authorize({.account = bob});
        mptAlice.authorize({.account = carol});

        mptAlice.pay(alice, bob, 100);
        mptAlice.pay(alice, carol, 50);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);

        mptAlice.set(
            {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        // Bob require preauthorization
        env(fset(bob, asfDepositAuth));
        env.close();

        mptAlice.convert({
            .account = carol,
            .amt = 50,
            .holderPubKey = mptAlice.getPubKey(carol),
        });
        mptAlice.convert({
            .account = bob,
            .amt = 50,
            .holderPubKey = mptAlice.getPubKey(bob),
        });

        // carol merge inbox
        mptAlice.mergeInbox({
            .account = carol,
        });

        // bob merge inbox
        mptAlice.mergeInbox({
            .account = bob,
        });

        // carol sends 10 to bob, but not authorized
        mptAlice.send(
            {.account = carol,
             .dest = bob,
             .amt = 10,
             .err = tecNO_PERMISSION});

        // Bob authorize alice
        env(deposit::auth(bob, carol));
        env.close();

        mptAlice.send({.account = carol, .dest = bob, .amt = 10});

        // Create credentials
        env(credentials::create(bob, dpIssuer, credType));
        env.close();
        env(credentials::accept(bob, dpIssuer, credType));
        env.close();
        auto const jv = credentials::ledgerEntry(env, bob, dpIssuer, credType);
        std::string const credIdx = jv[jss::result][jss::index].asString();

        mptAlice.send(
            {.account = carol,
             .dest = bob,
             .amt = 10,
             .credentials = {{credIdx}}});

        // Bob revoke authorization
        env(deposit::unauth(bob, carol));
        env.close();

        mptAlice.send(
            {.account = carol,
             .dest = bob,
             .amt = 10,
             .err = tecNO_PERMISSION});

        mptAlice.send(
            {.account = carol,
             .dest = bob,
             .amt = 10,
             .credentials = {{credIdx}},
             .err = tecNO_PERMISSION});

        // Bob authorize credentials
        env(deposit::authCredentials(bob, {{dpIssuer, credType}}));
        env.close();

        mptAlice.send(
            {.account = carol,
             .dest = bob,
             .amt = 10,
             .err = tecNO_PERMISSION});

        mptAlice.send({
            .account = carol,
            .dest = bob,
            .amt = 10,
            .credentials = {{credIdx}},
        });
    }

    void
    testClawback(FeatureBitset features)
    {
        testcase("test ConfidentialClawback");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const dave("dave");
        MPTTester mptAlice(env, alice, {.holders = {bob, carol, dave}});

        mptAlice.create(
            {.flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanClawback |
                 tfMPTCanPrivacy});
        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 100);
        mptAlice.authorize({.account = carol});
        mptAlice.pay(alice, carol, 200);
        mptAlice.authorize({.account = dave});
        mptAlice.pay(alice, dave, 300);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);
        mptAlice.generateKeyPair(dave);
        mptAlice.set(
            {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        // setup bob.
        // after setup, bob's spending balance is 60, inbox balance is 0.
        {
            // bob converts 60 to confidential
            mptAlice.convert(
                {.account = bob,
                 .amt = 60,
                 .holderPubKey = mptAlice.getPubKey(bob)});

            // bob merge inbox
            mptAlice.mergeInbox({
                .account = bob,
            });
        }

        // setup carol.
        // after setup, carol's spending balance is 120, inbox balance is 0.
        {
            // carol converts 120 to confidential
            mptAlice.convert(
                {.account = carol,
                 .amt = 120,
                 .holderPubKey = mptAlice.getPubKey(carol)});

            // carol merge inbox
            mptAlice.mergeInbox({
                .account = carol,
            });
        }

        // setup dave.
        // dave will not merge inbox.
        // after setup, dave's inbox balance is 200, spending balance is 0.
        mptAlice.convert(
            {.account = dave,
             .amt = 200,
             .holderPubKey = mptAlice.getPubKey(dave)});

        // setup: carol confidential send 50 to bob.
        // after send, bob's inbox balance is 50, spending balance
        // remains 60. carol's inbox balance remains 0, spending balance
        // drops to 70.
        mptAlice.send({.account = carol, .dest = bob, .amt = 50});

        // alice clawback all confidential balance from bob, 110 in total.
        // bob has balance in both inbox and spending. These balances should
        // become zero after clawback, which is verified in the
        // confidentialClaw function.
        mptAlice.confidentialClaw(
            {.account = alice, .holder = bob, .amt = 110});

        // alice clawback all confidential balance from carol, which is 70.
        // carol only has balance in spending.
        mptAlice.confidentialClaw(
            {.account = alice, .holder = carol, .amt = 70});

        // alice clawback all confidential balance from dave, which is 200.
        // dave only has balance in inbox.
        mptAlice.confidentialClaw(
            {.account = alice, .holder = dave, .amt = 200});
    }

    void
    testClawbackWithAuditor(FeatureBitset features)
    {
        testcase("test ConfidentialClawback with auditor");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const dave("dave");
        Account const auditor("auditor");
        MPTTester mptAlice(
            env, alice, {.holders = {bob, carol, dave}, .auditor = auditor});

        mptAlice.create(
            {.flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanClawback |
                 tfMPTCanPrivacy});
        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 100);
        mptAlice.authorize({.account = carol});
        mptAlice.pay(alice, carol, 200);
        mptAlice.authorize({.account = dave});
        mptAlice.pay(alice, dave, 300);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);
        mptAlice.generateKeyPair(dave);
        mptAlice.generateKeyPair(auditor);
        mptAlice.set(
            {.account = alice,
             .issuerPubKey = mptAlice.getPubKey(alice),
             .auditorPubKey = mptAlice.getPubKey(auditor)});

        // setup bob.
        // after setup, bob's spending balance is 60, inbox balance is 0.
        {
            // bob converts 60 to confidential
            mptAlice.convert(
                {.account = bob,
                 .amt = 60,
                 .holderPubKey = mptAlice.getPubKey(bob)});

            // bob merge inbox
            mptAlice.mergeInbox({
                .account = bob,
            });
        }

        // setup carol.
        // after setup, carol's spending balance is 120, inbox balance is 0.
        {
            // carol converts 120 to confidential
            mptAlice.convert(
                {.account = carol,
                 .amt = 120,
                 .holderPubKey = mptAlice.getPubKey(carol)});

            // carol merge inbox
            mptAlice.mergeInbox({
                .account = carol,
            });
        }

        // setup dave.
        // dave will not merge inbox.
        // after setup, dave's inbox balance is 200, spending balance is 0.
        mptAlice.convert(
            {.account = dave,
             .amt = 200,
             .holderPubKey = mptAlice.getPubKey(dave)});

        // setup: carol confidential send 50 to bob.
        // after send, bob's inbox balance is 50, spending balance
        // remains 60. carol's inbox balance remains 0, spending balance
        // drops to 70.
        mptAlice.send({.account = carol, .dest = bob, .amt = 50});

        // alice clawback all confidential balance from bob, 110 in total.
        // bob has balance in both inbox and spending. These balances should
        // become zero after clawback, which is verified in the
        // confidentialClaw function.
        mptAlice.confidentialClaw(
            {.account = alice, .holder = bob, .amt = 110});

        // alice clawback all confidential balance from carol, which is 70.
        // carol only has balance in spending.
        mptAlice.confidentialClaw(
            {.account = alice, .holder = carol, .amt = 70});

        // alice clawback all confidential balance from dave, which is 200.
        // dave only has balance in inbox.
        mptAlice.confidentialClaw(
            {.account = alice, .holder = dave, .amt = 200});
    }

    void
    testClawbackPreflight(FeatureBitset features)
    {
        testcase("test ConfidentialClawback Preflight");
        using namespace test::jtx;

        // test feature disabled
        {
            Env env{*this, features - featureConfidentialTransfer};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create();
            mptAlice.authorize({.account = bob});

            mptAlice.confidentialClaw(
                {.account = alice,
                 .holder = bob,
                 .amt = 10,
                 .proof = "123",
                 .err = temDISABLED});
        }

        // test malformed
        {
            // set up
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create(
                {.ownerCount = 1,
                 .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});
            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(carol);
            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});
            mptAlice.pay(alice, bob, 100);
            mptAlice.pay(alice, carol, 50);

            // only issuer can clawback
            mptAlice.confidentialClaw(
                {.account = carol,
                 .holder = bob,
                 .amt = 10,
                 .err = temMALFORMED});

            // invalid issuance ID, whose issuer is not alice
            {
                Json::Value jv;
                jv[jss::Account] = alice.human();
                jv[sfHolder] = bob.human();
                jv[jss::TransactionType] = jss::ConfidentialClawback;
                jv[sfMPTAmount] = std::to_string(10);
                jv[sfZKProof] = "123";

                // wrong issuance ID
                jv[sfMPTokenIssuanceID] =
                    "00000004AE123A8556F3CF91154711376AFB0F894F832B3E";

                env(jv, ter(temMALFORMED));
            }

            // issuer cannot clawback from self
            mptAlice.confidentialClaw(
                {.account = alice,
                 .holder = alice,
                 .amt = 10,
                 .err = temMALFORMED});

            // invalid amount
            mptAlice.confidentialClaw(
                {.account = alice,
                 .holder = bob,
                 .amt = 0,
                 .err = temBAD_AMOUNT});

            // invalid proof length
            mptAlice.confidentialClaw(
                {.account = alice,
                 .holder = bob,
                 .amt = 10,
                 .proof = "123",
                 .err = temMALFORMED});
        }
    }

    void
    testClawbackPreclaim(FeatureBitset features)
    {
        testcase("Clawback Preclaim Errors");
        using namespace test::jtx;

        {
            // set up, alice is the issuer, bob and carol are authorized
            // holders. dave is not authorized. bob has confidential
            // balance, carol does not.
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const dave("dave");
            MPTTester mptAlice(env, alice, {.holders = {bob, carol, dave}});

            mptAlice.create(
                {.flags = tfMPTCanTransfer | tfMPTCanClawback |
                     tfMPTRequireAuth | tfMPTCanPrivacy});
            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = alice, .holder = bob});
            mptAlice.authorize({.account = carol});
            mptAlice.authorize({.account = alice, .holder = carol});

            mptAlice.pay(alice, bob, 100);
            mptAlice.pay(alice, carol, 50);
            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(carol);
            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.convert({
                .account = bob,
                .amt = 60,
                .holderPubKey = mptAlice.getPubKey(bob),
            });
            mptAlice.mergeInbox({
                .account = bob,
            });

            // holder does not exist
            {
                Account const unknown("unknown");
                mptAlice.confidentialClaw(
                    {.account = alice,
                     .holder = unknown,
                     .amt = 10,
                     .err = tecNO_TARGET});
            }

            // dave does not hold mpt at all, no MPT object
            {
                mptAlice.confidentialClaw(
                    {.account = alice,
                     .holder = dave,
                     .amt = 10,
                     .err = tecOBJECT_NOT_FOUND});
            }

            // carol has no confidential balance
            {
                mptAlice.confidentialClaw(
                    {.account = alice,
                     .holder = carol,
                     .amt = 10,
                     .err = tecNO_PERMISSION});
            }
        }

        // lsfMPTCanClawback not set
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.flags = tfMPTCanTransfer | tfMPTCanPrivacy});
            mptAlice.authorize({.account = bob});
            mptAlice.generateKeyPair(alice);
            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.confidentialClaw(
                {.account = alice,
                 .holder = bob,
                 .amt = 10,
                 .err = tecNO_PERMISSION});
        }

        // no issuer key
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});
            mptAlice.create({.flags = tfMPTCanClawback | tfMPTCanPrivacy});
            mptAlice.authorize({.account = bob});
            mptAlice.generateKeyPair(alice);

            mptAlice.confidentialClaw(
                {.account = alice,
                 .holder = bob,
                 .amt = 10,
                 .err = tecNO_PERMISSION});
        }

        // issuance not found
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});
            mptAlice.create({.flags = tfMPTCanClawback | tfMPTCanPrivacy});
            mptAlice.authorize({.account = bob});
            mptAlice.generateKeyPair(alice);
            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            // destroy the issuance
            mptAlice.destroy();

            Json::Value jv;
            jv[jss::Account] = alice.human();
            jv[sfHolder] = bob.human();
            jv[jss::TransactionType] = jss::ConfidentialClawback;
            jv[sfMPTAmount] = std::to_string(10);
            std::string const dummyProof(196, '0');
            jv[sfZKProof] = dummyProof;
            jv[sfMPTokenIssuanceID] = to_string(mptAlice.issuanceID());

            env(jv, ter(tecOBJECT_NOT_FOUND));
        }

        // helper function to set up accounts to test lock and unauthorize
        // cases. after set up, bob has confidential balance 60 in spending.
        auto setupAccounts = [&](Env& env,
                                 Account const& alice,
                                 Account const& bob) -> MPTTester {
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.flags = tfMPTCanTransfer | tfMPTCanClawback |
                     tfMPTRequireAuth | tfMPTCanLock | tfMPTCanPrivacy});
            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = alice, .holder = bob});
            mptAlice.pay(alice, bob, 100);
            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});
            mptAlice.convert(
                {.account = bob,
                 .amt = 60,
                 .holderPubKey = mptAlice.getPubKey(bob)});
            mptAlice.mergeInbox({
                .account = bob,
            });

            return mptAlice;
        };

        // lock should not block clawback. lock bob individually
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice = setupAccounts(env, alice, bob);
            mptAlice.set({.account = alice, .holder = bob, .flags = tfMPTLock});

            // clawback should still work
            mptAlice.confidentialClaw(
                {.account = alice, .holder = bob, .amt = 60});
        }

        // lock globally
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice = setupAccounts(env, alice, bob);
            mptAlice.set({.account = alice, .flags = tfMPTLock});

            // clawback should still work
            mptAlice.confidentialClaw(
                {.account = alice, .holder = bob, .amt = 60});
        }

        // unauthorize should not block clawback
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice = setupAccounts(env, alice, bob);

            // unauthorize bob
            mptAlice.authorize(
                {.account = alice, .holder = bob, .flags = tfMPTUnauthorize});
            // clawback should still work
            mptAlice.confidentialClaw(
                {.account = alice, .holder = bob, .amt = 60});
        }

        // insufficient funds, clawback amount exceeding confidential
        // outstanding amount
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice = setupAccounts(env, alice, bob);

            mptAlice.confidentialClaw(
                {.account = alice,
                 .holder = bob,
                 .amt = 10000,
                 .err = tecINSUFFICIENT_FUNDS});
        }
    }

    void
    testClawbackProof(FeatureBitset features)
    {
        testcase("ConfidentialClawback Proof");
        using namespace test::jtx;

        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");

        // lambda function to set up MPT with alice as issuer, bob and carol
        // as authorized holders, and fund 1000 mpt to bob and 2000 mpt to
        // carol.
        auto setupEnv = [&](Env& env) -> MPTTester {
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create(
                {.flags =
                     tfMPTCanTransfer | tfMPTCanClawback | tfMPTCanPrivacy});

            for (auto const& [acct, amt] :
                 {std::pair{bob, 1000}, {carol, 2000}})
            {
                mptAlice.authorize({.account = acct});
                mptAlice.pay(alice, acct, amt);
                mptAlice.generateKeyPair(acct);
            }

            mptAlice.generateKeyPair(alice);
            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            return mptAlice;
        };

        // lambda function to test a set of bad clawback amounts that should
        // return tecBAD_PROOF
        auto checkBadProofs = [&](MPTTester& mpt,
                                  Account const& holder,
                                  std::initializer_list<uint64_t> amts) {
            for (auto const badAmt : amts)
            {
                mpt.confidentialClaw(
                    {.account = alice,
                     .holder = holder,
                     .amt = badAmt,
                     .err = tecBAD_PROOF});
            }
        };

        // SCENARIO 1: clawback from inbox only or spending only balances.
        // bob converts 500 and merge inbox,
        // carol converts 1000, but not merge inbox.
        // after setup, bob has 500 in spending, carol has 1000 in inbox.
        {
            Env env{*this, features};
            auto mptAlice = setupEnv(env);

            // bob converts and merges
            mptAlice.convert(
                {.account = bob,
                 .amt = 500,
                 .holderPubKey = mptAlice.getPubKey(bob)});
            mptAlice.mergeInbox({
                .account = bob,
            });
            // carol converts without merge
            mptAlice.convert(
                {.account = carol,
                 .amt = 1000,
                 .holderPubKey = mptAlice.getPubKey(carol)});

            // verify proof fails with invalid clawback amount
            // bob: 500 in Spending, 0 in Inbox
            checkBadProofs(
                mptAlice, bob, {1, 10, 70, 100, 110, 200, 499, 501, 600});

            // carol: 1000 in Inbox, 0 in Spending
            checkBadProofs(
                mptAlice, carol, {1, 10, 50, 500, 777, 850, 999, 1001, 1200});

            // clawback with correct amount that passes proof verification
            mptAlice.confidentialClaw(
                {.account = alice, .holder = bob, .amt = 500});
            mptAlice.confidentialClaw(
                {.account = alice, .holder = carol, .amt = 1000});
        }

        // SCENARIO 2: clawback from mixed inbox and spending balances.
        // bob converts 300 to confidential and merge inbox,
        // carol converts 400 to confidential and merge inbox,
        // bob sends 100 to carol, carol sends 100 to bob.
        // After setup, bob has 100 in inbox and 200 in spending;
        // carol has 100 in inbox and 300 in spending.
        {
            Env env{*this, features};
            auto mptAlice = setupEnv(env);

            mptAlice.convert(
                {.account = bob,
                 .amt = 300,
                 .holderPubKey = mptAlice.getPubKey(bob)});
            mptAlice.mergeInbox({
                .account = bob,
            });
            mptAlice.convert(
                {.account = carol,
                 .amt = 400,
                 .holderPubKey = mptAlice.getPubKey(carol)});
            mptAlice.mergeInbox({
                .account = carol,
            });
            mptAlice.send({.account = bob, .dest = carol, .amt = 100});
            mptAlice.send({.account = carol, .dest = bob, .amt = 100});

            // verify proof fails with invalid clawback amount
            // bob: 100 in inbox, 200 in spending
            checkBadProofs(mptAlice, bob, {1, 10, 50, 100, 200, 299, 301, 400});

            // proof failure for incorrect amount when clawbacking from
            // carol carol: 100 in inbox, 300 in spending
            checkBadProofs(
                mptAlice, carol, {1, 10, 50, 100, 300, 399, 401, 501});

            // clawback with correct amount that passes proof verification
            mptAlice.confidentialClaw(
                {.account = alice, .holder = bob, .amt = 300});
            mptAlice.confidentialClaw(
                {.account = alice, .holder = carol, .amt = 400});
        }
    }

    void
    testMutatePrivacy(FeatureBitset features)
    {
        testcase("mutate lsfMPTCanPrivacy");
        using namespace test::jtx;

        // can not create mpt issuance with tmfMPTCannotMutatePrivacy
        // when featureDynamicMPT is disabled
        {
            Env env{*this, features - featureDynamicMPT};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.ownerCount = 0,
                 .mutableFlags = tmfMPTCannotMutatePrivacy,
                 .err = temDISABLED});
        }

        // can not create mpt issuance with tmfMPTCannotMutatePrivacy when
        // featureConfidentialTransfer is disabled
        {
            Env env{*this, features - featureConfidentialTransfer};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.ownerCount = 0,
                 .mutableFlags = tmfMPTCannotMutatePrivacy,
                 .err = temDISABLED});
        }

        // if lsmfMPTCannotMutatePrivacy is set, can not set/clear
        // lsfMPTCanPrivacy
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.ownerCount = 1,
                 .flags = tfMPTCanTransfer,
                 .mutableFlags = tmfMPTCannotMutatePrivacy});

            mptAlice.set(
                {.account = alice,
                 .mutableFlags = tmfMPTSetPrivacy,
                 .err = tecNO_PERMISSION});

            mptAlice.set(
                {.account = alice,
                 .mutableFlags = tmfMPTClearPrivacy,
                 .err = tecNO_PERMISSION});
        }

        // Toggle lsfMPTCanPrivacy
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.ownerCount = 1,
                 .flags = tfMPTCanTransfer | tfMPTCanPrivacy,
                 .mutableFlags = tmfMPTCanMutateCanLock});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            auto holderPubKeySet = false;
            auto verifyToggle = [&](TER expectedResult, uint64_t amt) {
                if (!holderPubKeySet)
                    mptAlice.convert(
                        {.account = bob,
                         .amt = amt,
                         .holderPubKey = mptAlice.getPubKey(bob),
                         .err = expectedResult});
                else
                    mptAlice.convert({
                        .account = bob,
                        .amt = amt,
                        .err = expectedResult,
                    });

                if (expectedResult == tesSUCCESS)
                {
                    holderPubKeySet = true;
                    mptAlice.mergeInbox({
                        .account = bob,
                    });

                    // make sure there's no confidential outstanding balance
                    // for the next toggle test
                    mptAlice.convertBack({
                        .account = bob,
                        .amt = amt,
                    });
                }
            };

            // set lsfMPTCanPrivacy, but no effect because lsfMPTCanPrivacy
            // was already set
            mptAlice.set({.account = alice, .mutableFlags = tmfMPTSetPrivacy});
            verifyToggle(tesSUCCESS, 10);

            // clear lsfMPTCanPrivacy
            mptAlice.set(
                {.account = alice, .mutableFlags = tmfMPTClearPrivacy});
            verifyToggle(tecNO_PERMISSION, 10);

            // can clear lsfMPTCanPrivacy again but has no effect
            // for privacy settings
            mptAlice.set(
                {.account = alice,
                 .mutableFlags = tmfMPTClearPrivacy | tmfMPTSetCanLock});
            verifyToggle(tecNO_PERMISSION, 20);

            // set lsfMPTCanPrivacy again
            mptAlice.set({.account = alice, .mutableFlags = tmfMPTSetPrivacy});
            verifyToggle(tesSUCCESS, 30);
        }

        // can not mutate lsfPrivacy when there's confidential
        // outstanding amount
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            // lsmfMPTCannotMutatePrivacy is false by default,
            // so that lsfMPTCanPrivacy can be mutated
            mptAlice.create(
                {.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.set(
                {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            // bob convert 50 to confidential
            mptAlice.convert(
                {.account = bob,
                 .amt = 50,
                 .holderPubKey = mptAlice.getPubKey(bob)});

            // set or clear lsfMPTCanPrivacy should fail because of
            // confidential outstanding balance
            mptAlice.set(
                {.account = alice,
                 .mutableFlags = tmfMPTSetPrivacy,
                 .err = tecNO_PERMISSION});
            mptAlice.set(
                {.account = alice,
                 .mutableFlags = tmfMPTClearPrivacy,
                 .err = tecNO_PERMISSION});

            // bob merge inbox
            mptAlice.mergeInbox({
                .account = bob,
            });

            // bob convert back all confidential balance
            mptAlice.convertBack({
                .account = bob,
                .amt = 50,
            });

            // now clear lsfMPTCanPrivacy should succeed,
            // because there's no confidential outstanding balance
            mptAlice.set(
                {.account = alice, .mutableFlags = tmfMPTClearPrivacy});

            // bob can not convert because lsfMPTCanPrivacy was cleared
            // successfully
            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .err = tecNO_PERMISSION});

            // can set lsfMPTCanPrivacy again when there's no confidential
            // outstanding balance
            mptAlice.set({.account = alice, .mutableFlags = tmfMPTSetPrivacy});
            mptAlice.convert({
                .account = bob,
                .amt = 10,
            });
        }
    }

    void
    testConvertBackProof(FeatureBitset features)
    {
        testcase("Convert back proof");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        MPTTester mptAlice(env, alice, {.holders = {bob}});

        mptAlice.create(
            {.ownerCount = 1,
             .holderCount = 0,
             .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 100);

        mptAlice.generateKeyPair(alice);

        mptAlice.set(
            {.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        mptAlice.generateKeyPair(bob);

        mptAlice.convert({
            .account = bob,
            .amt = 40,
            .holderPubKey = mptAlice.getPubKey(bob),
        });

        mptAlice.mergeInbox({
            .account = bob,
        });

        // for ease of understanding, generate all the fields here instead of
        // autofilling
        uint64_t const amt = 10;
        Buffer const blindingFactor = generateBlindingFactor();
        Buffer const pcBlindingFactor = generateBlindingFactor();

        auto const spendingBalance = mptAlice.getDecryptedBalance(
            bob, MPTTester::HOLDER_ENCRYPTED_SPENDING);
        BEAST_EXPECT(spendingBalance.has_value());
        auto const encryptedSpendingBalance = mptAlice.getEncryptedBalance(
            bob, MPTTester::HOLDER_ENCRYPTED_SPENDING);
        BEAST_EXPECT(
            encryptedSpendingBalance.has_value() &&
            !encryptedSpendingBalance->empty());

        Buffer const pedersenCommitment =
            mptAlice.getPedersenCommitment(*spendingBalance, pcBlindingFactor);
        Buffer const issuerCiphertext =
            mptAlice.encryptAmount(alice, amt, blindingFactor);
        Buffer const bobCiphertext =
            mptAlice.encryptAmount(bob, amt, blindingFactor);
        auto const version = mptAlice.getMPTokenVersion(bob);

        // generate a proof using a pedersen commitment using the wrong value
        {
            uint256 const contextHash = getConvertBackContextHash(
                bob, env.seq(bob), mptAlice.issuanceID(), amt, version);
            Buffer const badPedersenCommitment =
                mptAlice.getPedersenCommitment(1, pcBlindingFactor);
            Buffer const proof = mptAlice.getConvertBackProof(
                bob,
                amt,
                contextHash,
                bobCiphertext,
                issuerCiphertext,
                {},
                blindingFactor,
                {
                    .pedersenCommitment =
                        badPedersenCommitment,  // bad pedersen commitment
                    .amt = *spendingBalance,
                    .encryptedAmt = *encryptedSpendingBalance,
                    .blindingFactor = pcBlindingFactor,
                });

            mptAlice.convertBack(
                {.account = bob,
                 .amt = amt,
                 .proof = proof,
                 .holderEncryptedAmt = bobCiphertext,
                 .issuerEncryptedAmt = issuerCiphertext,
                 .blindingFactor = blindingFactor,
                 .pedersenCommitment = pedersenCommitment,
                 .err = tecBAD_PROOF});
        }

        // test when the pedersen commitment is wrong while the proof is
        // right
        {
            // generate the context hash again because bob's sequence
            // incremented from prev txn
            uint256 const contextHash = getConvertBackContextHash(
                bob, env.seq(bob), mptAlice.issuanceID(), amt, version);

            Buffer const badPedersenCommitment =
                mptAlice.getPedersenCommitment(1, pcBlindingFactor);
            Buffer const proof = mptAlice.getConvertBackProof(
                bob,
                amt,
                contextHash,
                bobCiphertext,
                issuerCiphertext,
                {},
                blindingFactor,
                {
                    .pedersenCommitment = pedersenCommitment,
                    .amt = *spendingBalance,
                    .encryptedAmt = *encryptedSpendingBalance,
                    .blindingFactor = pcBlindingFactor,
                });

            mptAlice.convertBack(
                {.account = bob,
                 .amt = amt,
                 .proof = proof,
                 .holderEncryptedAmt = bobCiphertext,
                 .issuerEncryptedAmt = issuerCiphertext,
                 .blindingFactor = blindingFactor,
                 .pedersenCommitment =
                     badPedersenCommitment,  // wrong pc used here
                 .err = tecBAD_PROOF});
        }

        // the pc blinding factor for generating the pc is different from the
        // one used to generate pedersen proof
        {
            // generate the context hash again because bob's sequence
            // incremented from prev txn
            uint256 const contextHash = getConvertBackContextHash(
                bob, env.seq(bob), mptAlice.issuanceID(), amt, version);

            Buffer const proof = mptAlice.getConvertBackProof(
                bob,
                amt,
                contextHash,
                bobCiphertext,
                issuerCiphertext,
                {},
                blindingFactor,
                {
                    .pedersenCommitment = pedersenCommitment,
                    .amt = *spendingBalance,
                    .encryptedAmt = *encryptedSpendingBalance,
                    .blindingFactor =
                        generateBlindingFactor(),  // bad blinding factor
                });

            mptAlice.convertBack(
                {.account = bob,
                 .amt = amt,
                 .proof = proof,
                 .holderEncryptedAmt = bobCiphertext,
                 .issuerEncryptedAmt = issuerCiphertext,
                 .blindingFactor = blindingFactor,
                 .pedersenCommitment = pedersenCommitment,
                 .err = tecBAD_PROOF});
        }

        // a correct proof
        {
            // generate the context hash again because bob's sequence
            // incremented from prev txn
            uint256 const contextHash = getConvertBackContextHash(
                bob, env.seq(bob), mptAlice.issuanceID(), amt, version);

            Buffer const proof = mptAlice.getConvertBackProof(
                bob,
                amt,
                contextHash,
                bobCiphertext,
                issuerCiphertext,
                {},
                blindingFactor,
                {
                    .pedersenCommitment = pedersenCommitment,
                    .amt = *spendingBalance,
                    .encryptedAmt = *encryptedSpendingBalance,
                    .blindingFactor = pcBlindingFactor,
                });

            mptAlice.convertBack({
                .account = bob,
                .amt = amt,
                .proof = proof,
                .holderEncryptedAmt = bobCiphertext,
                .issuerEncryptedAmt = issuerCiphertext,
                .blindingFactor = blindingFactor,
                .pedersenCommitment = pedersenCommitment,
            });
        }
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testConvert(features);
        testConvertPreflight(features);
        testConvertPreclaim(features);
        testConvertWithAuditor(features);

        testMergeInbox(features);
        testMergeInboxPreflight(features);
        testMergeInboxPreclaim(features);

        testSetPreflight(features);

        // ConfidentialSend
        testSend(features);
        testSendPreflight(features);
        testSendPreclaim(features);
        testSendDepositPreauth(features);
        testSendWithAuditor(features);

        // ConfidentialClawback
        testClawback(features);
        testClawbackPreflight(features);
        testClawbackPreclaim(features);
        testClawbackProof(features);
        testClawbackWithAuditor(features);

        testDelete(features);

        testConvertBack(features);
        testConvertBackPreflight(features);
        testConvertBackPreclaim(features);
        testConvertBackWithAuditor(features);
        testConvertBackProof(features);

        testMutatePrivacy(features);
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
