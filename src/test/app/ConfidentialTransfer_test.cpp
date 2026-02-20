#include <test/jtx.h>
#include <test/jtx/trust.h>

#include <xrpl/protocol/ConfidentialTransfer.h>

#include <openssl/rand.h>

namespace xrpl {

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

    // Returns a valid compressed EC point (33 bytes) that can pass preflight
    // validation but contains invalid data for preclaim test purposes.
    static Buffer const&
    getTrivialCommitment()
    {
        static Buffer const trivialCommitment = []() {
            Buffer buf(ecPedersenCommitmentLength);
            std::memset(buf.data(), 0, ecPedersenCommitmentLength);

            // 0x02 prefix for compressed EC point with even y-coordinate
            buf.data()[0] = 0x02;
            // Set last byte to make it a valid x-coordinate on the curve
            buf.data()[ecPedersenCommitmentLength - 1] = 0x01;

            return buf;
        }();

        return trivialCommitment;
    }

    std::string
    getTrivialSendProofHex(size_t nRecipients)
    {
        size_t const sizeEquality = getMultiCiphertextEqualityProofSize(nRecipients);
        size_t const totalSize = sizeEquality + (2 * ecPedersenProofLength) + ecDoubleBulletproofLength;

        Buffer buf(totalSize);
        std::memset(buf.data(), 0, totalSize);

        for (std::size_t i = 0; i < totalSize; i += ecGamalEncryptedLength)
        {
            buf.data()[i] = 0x02;
            if (i + ecGamalEncryptedLength - 1 < totalSize)
                buf.data()[i + ecGamalEncryptedLength - 1] = 0x01;
        }

        return strHex(buf);
    }

    void
    testConvert(FeatureBitset features)
    {
        testcase("Convert");
        using namespace test::jtx;

        // Basic convert test
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

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

        // Edge case: minimum amount (1)
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 1);

            mptAlice.generateKeyPair(alice);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);
            mptAlice.convert({
                .account = bob,
                .amt = 0,
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            mptAlice.convert({
                .account = bob,
                .amt = 1,
            });
        }

        // Edge case: maxMPTokenAmount
        // Using raw JSON to avoid automatic decryption checks in MPTTester
        // which don't work for very large amounts (brute-force decryption is slow)
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, maxMPTokenAmount);

            mptAlice.generateKeyPair(alice);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            // First convert with amt=0 to register public key (uses MPTTester)
            mptAlice.convert({
                .account = bob,
                .amt = 0,
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            // Second convert with maxMPTokenAmount using raw JSON
            Buffer const blindingFactor = generateBlindingFactor();
            auto const holderCiphertext = mptAlice.encryptAmount(bob, maxMPTokenAmount, blindingFactor);
            auto const issuerCiphertext = mptAlice.encryptAmount(alice, maxMPTokenAmount, blindingFactor);

            Json::Value jv;
            jv[jss::Account] = bob.human();
            jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
            jv[sfMPTokenIssuanceID] = to_string(mptAlice.issuanceID());
            jv[sfMPTAmount.jsonName] = std::to_string(maxMPTokenAmount);
            jv[sfHolderEncryptedAmount.jsonName] = strHex(holderCiphertext);
            jv[sfIssuerEncryptedAmount.jsonName] = strHex(issuerCiphertext);
            jv[sfBlindingFactor.jsonName] = strHex(blindingFactor);

            env(jv, ter(tesSUCCESS));

            // Verify the public balance was reduced
            env.require(mptbalance(mptAlice, bob, 0));
        }
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

        mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

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

        // Alice (issuer) tries to convert her own tokens - should fail
        {
            Env env{*this, features};
            Account const alice("alice");
            MPTTester mptAlice(env, alice);

            mptAlice.create({.flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});
            mptAlice.generateKeyPair(alice);

            mptAlice.convert(
                {.account = alice, .amt = 10, .holderPubKey = mptAlice.getPubKey(alice), .err = temMALFORMED});
        }

        {
            Env env{*this, features - featureConfidentialTransfer};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice), .err = temDISABLED});

            mptAlice.convert({.account = bob, .amt = 10, .holderPubKey = mptAlice.getPubKey(bob), .err = temDISABLED});
        }

        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.convert(
                {.account = alice, .amt = 10, .holderPubKey = mptAlice.getPubKey(bob), .err = temMALFORMED});

            // blinding factor length is invalid
            mptAlice.convert(
                {.account = alice,
                 .amt = 10,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .blindingFactor = Buffer(10),
                 .err = temMALFORMED});

            // Holder encrypted amount is empty (length 0)
            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .holderEncryptedAmt = Buffer{},
                 .err = temBAD_CIPHERTEXT});

            // Issuer encrypted amount is empty (length 0)
            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .issuerEncryptedAmt = Buffer{},
                 .err = temBAD_CIPHERTEXT});

            // Auditor encrypted amount has invalid length (must be 66 bytes)
            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .auditorEncryptedAmt = Buffer(10),
                 .err = temBAD_CIPHERTEXT});

            // Auditor encrypted amount has correct length but invalid data
            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .auditorEncryptedAmt = getBadCiphertext(),
                 .err = temBAD_CIPHERTEXT});

            // Amount exceeds maximum allowed MPT amount
            mptAlice.convert(
                {.account = bob,
                 .amt = maxMPTokenAmount + 1,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .err = temBAD_AMOUNT});

            // Holder encrypted amount has correct length but invalid data
            mptAlice.convert(
                {.account = bob,
                 .amt = 1,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .holderEncryptedAmt = getBadCiphertext(),
                 .err = temBAD_CIPHERTEXT});

            // Issuer encrypted amount has correct length but invalid data (not
            // a valid EC point)
            mptAlice.convert(
                {.account = bob,
                 .amt = 1,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .issuerEncryptedAmt = getBadCiphertext(),
                 .err = temBAD_CIPHERTEXT});

            // Holder public key is invalid (empty buffer)
            mptAlice.convert({.account = bob, .amt = 10, .holderPubKey = Buffer{}, .err = temMALFORMED});

            // Holder public key has correct length but invalid EC point data
            mptAlice.convert({.account = bob, .amt = 10, .holderPubKey = Buffer(ecPubKeyLength), .err = temMALFORMED});
        }

        // when registering holder pub key, the transaction must include a
        // Schnorr proof of knowledge for the corresponding secret key
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

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

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            // this will register bob's pub key,
            // and convert 10 to confidential balance
            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            // proof must not be provided after pub key was registered
            mptAlice.convert({.account = bob, .amt = 20, .fillSchnorrProof = true, .err = temMALFORMED});
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

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice), .err = temDISABLED});
        }

        // pub key is invalid
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            // Issuer pub key is invalid (empty)
            mptAlice.set({.account = alice, .issuerPubKey = Buffer{}, .err = temMALFORMED});

            // Issuer pub key has correct length but invalid EC point data
            mptAlice.set({.account = alice, .issuerPubKey = Buffer(ecPubKeyLength), .err = temMALFORMED});

            // Auditor key is invalid length
            mptAlice.set(
                {.account = alice,
                 .issuerPubKey = mptAlice.getPubKey(alice),
                 .auditorPubKey = Buffer(10),
                 .err = temMALFORMED});

            // Auditor key has correct length but invalid EC point data
            mptAlice.set(
                {.account = alice,
                 .issuerPubKey = mptAlice.getPubKey(alice),
                 .auditorPubKey = Buffer(ecPubKeyLength),
                 .err = temMALFORMED});

            // Cannot set auditor key without issuer key
            mptAlice.set({.account = alice, .auditorPubKey = mptAlice.getPubKey(alice), .err = temMALFORMED});

            // Cannot set Holder and issuer Keys in the same transaction
            mptAlice.set(
                {.account = alice, .holder = bob, .issuerPubKey = mptAlice.getPubKey(alice), .err = temMALFORMED});

            // Cannot set Holder and auditor Keys in the same transaction
            mptAlice.set(
                {.account = alice, .holder = bob, .auditorPubKey = mptAlice.getPubKey(alice), .err = temMALFORMED});
        }

        // issuance has disabled confidential transfer
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            // no tfMPTCanPrivacy flag enabled
            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice), .err = tecNO_PERMISSION});
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

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.convert(
                {.account = bob, .amt = 10, .holderPubKey = mptAlice.getPubKey(bob), .err = tecNO_PERMISSION});
        }

        // issuer has not uploaded their sfIssuerElGamalPublicKey
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.convert(
                {.account = bob, .amt = 10, .holderPubKey = mptAlice.getPubKey(bob), .err = tecNO_PERMISSION});
        }

        // issuance does not exist
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.destroy();
            mptAlice.generateKeyPair(bob);

            mptAlice.convert(
                {.account = bob, .amt = 10, .holderPubKey = mptAlice.getPubKey(bob), .err = tecOBJECT_NOT_FOUND});
        }

        // bob has not created MPToken
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.convert(
                {.account = bob, .amt = 10, .holderPubKey = mptAlice.getPubKey(bob), .err = tecOBJECT_NOT_FOUND});
        }

        // Verification of Issuer and and holder ciphertexts
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .holderEncryptedAmt = getTrivialCiphertext(),
                 .err = tecBAD_PROOF});

            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .issuerEncryptedAmt = getTrivialCiphertext(),
                 .err = tecBAD_PROOF});
        }

        // trying to convert more than what bob has
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            mptAlice.convert(
                {.account = bob, .amt = 200, .holderPubKey = mptAlice.getPubKey(bob), .err = tecINSUFFICIENT_FUNDS});
        }

        // holder cannot upload pk again
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            mptAlice.convert({.account = bob, .amt = 10, .holderPubKey = mptAlice.getPubKey(bob)});

            // cannot upload pk again
            mptAlice.convert({.account = bob, .amt = 10, .holderPubKey = mptAlice.getPubKey(bob), .err = tecDUPLICATE});
        }

        // cannot convert if locked
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.set({.account = alice, .holder = bob, .flags = tfMPTLock});

            mptAlice.generateKeyPair(bob);

            mptAlice.convert(
                {.account = bob, .amt = 10, .holderPubKey = mptAlice.getPubKey(bob), .err = tecINSUFFICIENT_FUNDS});

            mptAlice.set({.account = alice, .holder = bob, .flags = tfMPTUnlock});

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
                {.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTRequireAuth | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = alice, .holder = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            // Unauthorize bob
            mptAlice.authorize({.account = alice, .holder = bob, .flags = tfMPTUnauthorize});

            mptAlice.convert(
                {.account = bob, .amt = 10, .holderPubKey = mptAlice.getPubKey(bob), .err = tecINSUFFICIENT_FUNDS});

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

        // cannot convert if auditor key is set, but auditor amount is not
        // provided
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const auditor("auditor");
            MPTTester mptAlice(env, alice, {.holders = {bob}, .auditor = auditor});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(auditor);

            mptAlice.set(
                {.account = alice,
                 .issuerPubKey = mptAlice.getPubKey(alice),
                 .auditorPubKey = mptAlice.getPubKey(auditor)});

            // no auditor encrypted amt provided
            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
                 .fillAuditorEncryptedAmt = false,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .err = tecNO_PERMISSION});
        }

        // cannot convert if tx include auditor ciphertext, but does not have
        // auditing enabled
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            // there is no auditor key set
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .auditorEncryptedAmt = getTrivialCiphertext(),
                 .err = tecNO_PERMISSION});
        }

        // Auditor key set successfully, auditor ciphertext mathematically
        // correct, but contains invalid data (mismatching amount).
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const auditor("auditor");
            MPTTester mptAlice(env, alice, {.holders = {bob}, .auditor = auditor});

            mptAlice.create({.flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(auditor);

            mptAlice.set(
                {.account = alice,
                 .issuerPubKey = mptAlice.getPubKey(alice),
                 .auditorPubKey = mptAlice.getPubKey(auditor)});

            mptAlice.convert(
                {.account = bob,
                 .amt = 10,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .auditorEncryptedAmt = getTrivialCiphertext(),
                 .err = tecBAD_PROOF});
        }

        // invalid proof when registering holder pub key
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

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

        mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 100);

        mptAlice.generateKeyPair(alice);

        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

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

        mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 100);

        mptAlice.generateKeyPair(alice);

        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

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

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

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

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock});

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

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.mergeInbox({.account = bob, .err = tecOBJECT_NOT_FOUND});
        }

        // bob doesn't have encrypted balances
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

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

        mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

        mptAlice.authorize({.account = bob});
        mptAlice.authorize({.account = carol});

        mptAlice.pay(alice, bob, 100);
        mptAlice.pay(alice, carol, 50);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);

        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        // Convert 60 out of 100
        mptAlice.convert({.account = bob, .amt = 60, .holderPubKey = mptAlice.getPubKey(bob)});

        // bob merge inbox
        mptAlice.mergeInbox({
            .account = bob,
        });

        // carol convert 20 to confidential
        mptAlice.convert({.account = carol, .amt = 20, .holderPubKey = mptAlice.getPubKey(carol)});

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
        MPTTester mptAlice(env, alice, {.holders = {bob, carol}, .auditor = auditor});

        mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

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
        mptAlice.convert({.account = bob, .amt = 60, .holderPubKey = mptAlice.getPubKey(bob)});

        // bob merge inbox
        mptAlice.mergeInbox({
            .account = bob,
        });

        mptAlice.convert({.account = carol, .amt = 20, .holderPubKey = mptAlice.getPubKey(carol)});

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
        testcase("test ConfidentialMPTSend Preflight");
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

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});
            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(carol);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});
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
            mptAlice.send({.account = alice, .dest = carol, .amt = 10, .err = temMALFORMED});

            // can not send to self
            mptAlice.send({.account = bob, .dest = bob, .amt = 10, .err = temMALFORMED});

            // sender encrypted amount wrong length
            mptAlice.send(
                {.account = bob, .dest = carol, .amt = 10, .senderEncryptedAmt = Buffer(10), .err = temBAD_CIPHERTEXT});
            // dest encrypted amount wrong length
            mptAlice.send(
                {.account = bob, .dest = carol, .amt = 10, .destEncryptedAmt = Buffer(10), .err = temBAD_CIPHERTEXT});
            // issuer encrypted amount wrong length
            mptAlice.send(
                {.account = bob, .dest = carol, .amt = 10, .issuerEncryptedAmt = Buffer(10), .err = temBAD_CIPHERTEXT});

            // sender encrypted amount malformed
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = getTrivialSendProofHex(3),
                 .senderEncryptedAmt = Buffer(ecGamalEncryptedTotalLength),
                 .amountCommitment = getTrivialCommitment(),
                 .balanceCommitment = getTrivialCommitment(),
                 .err = temBAD_CIPHERTEXT});
            // dest encrypted amount malformed
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = getTrivialSendProofHex(3),
                 .destEncryptedAmt = Buffer(ecGamalEncryptedTotalLength),
                 .amountCommitment = getTrivialCommitment(),
                 .balanceCommitment = getTrivialCommitment(),
                 .err = temBAD_CIPHERTEXT});
            // issuer encrypted amount malformed
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = getTrivialSendProofHex(3),
                 .issuerEncryptedAmt = Buffer(ecGamalEncryptedTotalLength),
                 .amountCommitment = getTrivialCommitment(),
                 .balanceCommitment = getTrivialCommitment(),
                 .err = temBAD_CIPHERTEXT});

            // invalid proof length
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = std::string(10, 'A'),
                 .amountCommitment = getTrivialCommitment(),
                 .balanceCommitment = getTrivialCommitment(),
                 .err = temMALFORMED});

            // invalid amount Pedersen commitment length
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = getTrivialSendProofHex(3),
                 .amountCommitment = Buffer(100),
                 .balanceCommitment = getTrivialCommitment(),
                 .err = temMALFORMED});

            // invalid balance Pedersen commitment length
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = getTrivialSendProofHex(3),
                 .amountCommitment = getTrivialCommitment(),
                 .balanceCommitment = Buffer(100),
                 .err = temMALFORMED});

            // amount Pedersen commitment has correct length but invalid EC point data
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = getTrivialSendProofHex(3),
                 .amountCommitment = Buffer(ecPedersenCommitmentLength),
                 .balanceCommitment = getTrivialCommitment(),
                 .err = temMALFORMED});

            // balance Pedersen commitment has correct length but invalid EC point data
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = getTrivialSendProofHex(3),
                 .amountCommitment = getTrivialCommitment(),
                 .balanceCommitment = Buffer(ecPedersenCommitmentLength),
                 .err = temMALFORMED});
        }

        // test bad ciphertext
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const auditor("auditor");
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}, .auditor = auditor});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});
            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(carol);
            mptAlice.generateKeyPair(auditor);

            mptAlice.set(
                {.account = alice,
                 .issuerPubKey = mptAlice.getPubKey(alice),
                 .auditorPubKey = mptAlice.getPubKey(auditor)});
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

            // auditor encrypted amount wrong length
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = getTrivialSendProofHex(4),
                 .auditorEncryptedAmt = Buffer(10),
                 .amountCommitment = getTrivialCommitment(),
                 .balanceCommitment = getTrivialCommitment(),
                 .err = temBAD_CIPHERTEXT});

            // auditor encrypted amount (correct length, invalid data)
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = getTrivialSendProofHex(4),
                 .auditorEncryptedAmt = getBadCiphertext(),
                 .amountCommitment = getTrivialCommitment(),
                 .balanceCommitment = getTrivialCommitment(),
                 .err = temBAD_CIPHERTEXT});
        }
    }

    void
    testSendPreclaim(FeatureBitset features)
    {
        testcase("test ConfidentialMPTSend Preclaim");

        using namespace test::jtx;
        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const dave("dave");
        Account const eve("eve");
        MPTTester mptAlice(env, alice, {.holders = {bob, carol, dave, eve}});

        // authorize bob, carol, dave (not eve)
        mptAlice.create({.flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTRequireAuth | tfMPTCanPrivacy});
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
        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        // bob and carol convert some funds to confidential
        mptAlice.convert({.account = bob, .amt = 60, .holderPubKey = mptAlice.getPubKey(bob), .err = tesSUCCESS});
        mptAlice.convert({.account = carol, .amt = 20, .holderPubKey = mptAlice.getPubKey(carol), .err = tesSUCCESS});

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
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            // destroy the issuance
            mptAlice.destroy();

            Json::Value jv;
            jv[jss::Account] = bob.human();
            jv[jss::Destination] = carol.human();
            jv[jss::TransactionType] = jss::ConfidentialMPTSend;
            jv[sfMPTokenIssuanceID] = to_string(mptAlice.issuanceID());
            jv[sfSenderEncryptedAmount] = strHex(getTrivialCiphertext());
            jv[sfDestinationEncryptedAmount] = strHex(getTrivialCiphertext());
            jv[sfIssuerEncryptedAmount] = strHex(getTrivialCiphertext());
            jv[sfAmountCommitment] = strHex(getTrivialCommitment());
            jv[sfBalanceCommitment] = strHex(getTrivialCommitment());
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
                 .proof = getTrivialSendProofHex(3),
                 .senderEncryptedAmt = getTrivialCiphertext(),
                 .destEncryptedAmt = getTrivialCiphertext(),
                 .issuerEncryptedAmt = getTrivialCiphertext(),
                 .amountCommitment = getTrivialCommitment(),
                 .balanceCommitment = getTrivialCommitment(),
                 .err = tecNO_TARGET});
        }

        // dave exists, but has no confidential fields (never converted)
        {
            mptAlice.send(
                {.account = bob,
                 .dest = dave,
                 .amt = 10,
                 .proof = getTrivialSendProofHex(3),
                 .senderEncryptedAmt = getTrivialCiphertext(),
                 .destEncryptedAmt = getTrivialCiphertext(),
                 .issuerEncryptedAmt = getTrivialCiphertext(),
                 .amountCommitment = getTrivialCommitment(),
                 .balanceCommitment = getTrivialCommitment(),
                 .err = tecNO_PERMISSION});
            mptAlice.send(
                {.account = dave,
                 .dest = carol,
                 .amt = 10,
                 .proof = getTrivialSendProofHex(3),
                 .senderEncryptedAmt = getTrivialCiphertext(),
                 .destEncryptedAmt = getTrivialCiphertext(),
                 .issuerEncryptedAmt = getTrivialCiphertext(),
                 .amountCommitment = getTrivialCommitment(),
                 .balanceCommitment = getTrivialCommitment(),
                 .err = tecNO_PERMISSION});
        }

        // destination exists but has no MPT object.
        {
            mptAlice.send(
                {.account = bob,
                 .dest = eve,
                 .amt = 10,
                 .proof = getTrivialSendProofHex(3),
                 .senderEncryptedAmt = getTrivialCiphertext(),
                 .destEncryptedAmt = getTrivialCiphertext(),
                 .issuerEncryptedAmt = getTrivialCiphertext(),
                 .amountCommitment = getTrivialCommitment(),
                 .balanceCommitment = getTrivialCommitment(),
                 .err = tecOBJECT_NOT_FOUND});
        }

        // issuance is locked globally
        {
            // lock issuance
            mptAlice.set({.account = alice, .flags = tfMPTLock});
            mptAlice.send({.account = bob, .dest = carol, .amt = 10, .err = tecLOCKED});
            // unlock issuance
            mptAlice.set({.account = alice, .flags = tfMPTUnlock});
            // now can send
            mptAlice.send({.account = bob, .dest = carol, .amt = 1});
        }

        // sender is locked
        {
            // lock bob
            mptAlice.set({.account = alice, .holder = bob, .flags = tfMPTLock});
            mptAlice.send({.account = bob, .dest = carol, .amt = 10, .err = tecLOCKED});
            // unlock bob
            mptAlice.set({.account = alice, .holder = bob, .flags = tfMPTUnlock});
            // now can send
            mptAlice.send({.account = bob, .dest = carol, .amt = 2});
        }

        // destination is locked
        {
            // lock carol
            mptAlice.set({.account = alice, .holder = carol, .flags = tfMPTLock});
            mptAlice.send({.account = bob, .dest = carol, .amt = 10, .err = tecLOCKED});
            // unlock carol
            mptAlice.set({.account = alice, .holder = carol, .flags = tfMPTUnlock});
            // now can send
            mptAlice.send({.account = bob, .dest = carol, .amt = 3});
        }

        // sender not authorized
        {
            // unauthorize bob
            mptAlice.authorize({.account = alice, .holder = bob, .flags = tfMPTUnauthorize});
            mptAlice.send({.account = bob, .dest = carol, .amt = 10, .err = tecNO_AUTH});
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
            mptAlice.authorize({.account = alice, .holder = carol, .flags = tfMPTUnauthorize});
            mptAlice.send({.account = bob, .dest = carol, .amt = 10, .err = tecNO_AUTH});
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

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});

            mptAlice.pay(alice, bob, 100);
            mptAlice.pay(alice, carol, 50);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(carol);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            // Convert 60 out of 100
            mptAlice.convert({.account = bob, .amt = 60, .holderPubKey = mptAlice.getPubKey(bob), .err = tesSUCCESS});

            // bob merge inbox
            mptAlice.mergeInbox({
                .account = bob,
            });

            mptAlice.convert(
                {.account = carol, .amt = 20, .holderPubKey = mptAlice.getPubKey(carol), .err = tesSUCCESS});

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

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanLock | tfMPTCanPrivacy | tfMPTCanTransfer});

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});

            mptAlice.pay(alice, bob, 100);
            mptAlice.pay(alice, carol, 50);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(carol);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.convert({.account = bob, .amt = 60, .holderPubKey = mptAlice.getPubKey(bob), .err = tesSUCCESS});

            mptAlice.mergeInbox({
                .account = bob,
            });

            mptAlice.convert(
                {.account = carol, .amt = 20, .holderPubKey = mptAlice.getPubKey(carol), .err = tesSUCCESS});

            mptAlice.mergeInbox({
                .account = carol,
            });

            mptAlice.send(
                {.account = bob, .dest = carol, .amt = 10, .proof = getTrivialSendProofHex(3), .err = tecBAD_PROOF});
        }

        // No Auditor key set, but auditor encrypted amt provided
        {
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = getTrivialSendProofHex(4),
                 .auditorEncryptedAmt = getTrivialCiphertext(),
                 .err = tecNO_PERMISSION});
        }

        // Auditor CipherText is Valid, but does not match the Txn Amount
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const auditor("auditor");
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}, .auditor = auditor});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});
            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(carol);
            mptAlice.generateKeyPair(auditor);

            mptAlice.set(
                {.account = alice,
                 .issuerPubKey = mptAlice.getPubKey(alice),
                 .auditorPubKey = mptAlice.getPubKey(auditor)});
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

            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 10,
                 .proof = getTrivialSendProofHex(4),
                 .auditorEncryptedAmt = getTrivialCiphertext(),
                 .amountCommitment = getTrivialCommitment(),
                 .balanceCommitment = getTrivialCommitment(),
                 .err = tecBAD_PROOF});
        }
    }

    void
    testSendRangeProof(FeatureBitset features)
    {
        testcase("test ConfidentialMPTSend Range Proof");

        using namespace test::jtx;
        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

        mptAlice.create({.ownerCount = 1, .flags = tfMPTCanLock | tfMPTCanPrivacy | tfMPTCanTransfer});
        mptAlice.authorize({.account = bob});
        mptAlice.authorize({.account = carol});

        mptAlice.pay(alice, bob, 1000);
        mptAlice.pay(alice, carol, 1000);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);

        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        {
            // Bob converts 60
            mptAlice.convert({.account = bob, .amt = 60, .holderPubKey = mptAlice.getPubKey(bob)});
            mptAlice.mergeInbox({.account = bob});

            mptAlice.convert({.account = carol, .amt = 50, .holderPubKey = mptAlice.getPubKey(carol)});
            mptAlice.mergeInbox({.account = carol});

            // Bob has 60, tries to send 70. Invalid remaining balance.
            mptAlice.send({.account = bob, .dest = carol, .amt = 70, .err = tecBAD_PROOF});

            // Bob has 60, tries to send 61. Invalid remaining balance.
            mptAlice.send({.account = bob, .dest = carol, .amt = 61, .err = tecBAD_PROOF});

            // Bob has 60, sends 60. Remainder is exactly 0. Valid remaining balance.
            mptAlice.send({.account = bob, .dest = carol, .amt = 60, .err = tesSUCCESS});
        }

        {
            // Bob converts 100.
            mptAlice.convert({.account = bob, .amt = 100});
            mptAlice.mergeInbox({.account = bob});

            // Bob has 100, tries to send 2^64-1. Invalid remaining balance.
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 0xFFFFFFFFFFFFFFFF,  // Max uint64
                 .err = tecBAD_PROOF});

            // Bob sends 1, remaining 99.
            mptAlice.send({.account = bob, .dest = carol, .amt = 1, .err = tesSUCCESS});

            // Bob sends 100, but only has 99. Invalid remaining balance.
            mptAlice.send({.account = bob, .dest = carol, .amt = 100, .err = tecBAD_PROOF});
        }

        // todo: test m exceeding range, require using scala and refactor
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

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            mptAlice.convert({
                .account = bob,
                .amt = 100,
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            mptAlice.authorize({.account = bob, .flags = tfMPTUnauthorize, .err = tecHAS_OBLIGATIONS});
        }

        // cannot delete mptoken where it has encrypted balance
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

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
            mptAlice.authorize({.account = carol, .flags = tfMPTUnauthorize, .err = tecHAS_OBLIGATIONS});
        }

        // can delete mptoken if outstanding confidential balance is zero
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

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

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

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

        // Basic convert back test
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

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

            // todo: this test fails because proof generation for convertback fails if remainder amount is 0
            // mptAlice.convertBack({
            //     .account = bob,
            //     .amt = 10,
            // });
        }

        // Edge case: minimum amount (1)
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 2);

            mptAlice.generateKeyPair(alice);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            mptAlice.convert({
                .account = bob,
                .amt = 2,
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            mptAlice.mergeInbox({
                .account = bob,
            });

            mptAlice.convertBack({
                .account = bob,
                .amt = 1,
            });
        }

        // Edge case: maxMPTokenAmount
        // Using raw JSON to avoid automatic decryption checks in MPTTester
        // which don't work for very large amounts (brute-force decryption is slow)
        // TODO: improve this test once there is bounded decryption or optimized decryption for large amounts
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, maxMPTokenAmount);

            mptAlice.generateKeyPair(alice);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            // Convert maxMPTokenAmount to confidential using raw JSON
            Buffer const convertBlindingFactor = generateBlindingFactor();
            auto const convertHolderCiphertext = mptAlice.encryptAmount(bob, maxMPTokenAmount, convertBlindingFactor);
            auto const convertIssuerCiphertext = mptAlice.encryptAmount(alice, maxMPTokenAmount, convertBlindingFactor);
            auto const convertContextHash =
                getConvertContextHash(bob.id(), env.seq(bob), mptAlice.issuanceID(), maxMPTokenAmount);
            auto const schnorrProof = mptAlice.getSchnorrProof(bob, convertContextHash);
            BEAST_EXPECT(schnorrProof.has_value());

            {
                Json::Value jv;
                jv[jss::Account] = bob.human();
                jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
                jv[sfMPTokenIssuanceID] = to_string(mptAlice.issuanceID());
                jv[sfMPTAmount.jsonName] = std::to_string(maxMPTokenAmount);
                jv[sfHolderElGamalPublicKey.jsonName] = strHex(*mptAlice.getPubKey(bob));
                jv[sfHolderEncryptedAmount.jsonName] = strHex(convertHolderCiphertext);
                jv[sfIssuerEncryptedAmount.jsonName] = strHex(convertIssuerCiphertext);
                jv[sfBlindingFactor.jsonName] = strHex(convertBlindingFactor);
                jv[sfZKProof.jsonName] = strHex(*schnorrProof);

                env(jv, ter(tesSUCCESS));
            }

            // Merge inbox using raw JSON - moves funds from inbox to spending balance
            {
                Json::Value jv;
                jv[jss::Account] = bob.human();
                jv[jss::TransactionType] = jss::ConfidentialMPTMergeInbox;
                jv[sfMPTokenIssuanceID] = to_string(mptAlice.issuanceID());

                env(jv, ter(tesSUCCESS));
            }

            // ConvertBack maxMPTokenAmount - 1 using raw JSON
            // After convert + merge, spending balance = maxMPTokenAmount
            // We convert back maxMPTokenAmount - 1 to leave remainder of 1
            std::uint64_t const convertBackAmt = maxMPTokenAmount - 1;

            Buffer const convertBackBlindingFactor = generateBlindingFactor();
            auto const convertBackHolderCiphertext =
                mptAlice.encryptAmount(bob, convertBackAmt, convertBackBlindingFactor);
            auto const convertBackIssuerCiphertext =
                mptAlice.encryptAmount(alice, convertBackAmt, convertBackBlindingFactor);

            // Get the encrypted spending balance from ledger (no decryption needed)
            auto const encryptedSpendingBalance =
                mptAlice.getEncryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING);
            BEAST_EXPECT(encryptedSpendingBalance.has_value());

            // Generate pedersen commitment for the known spending balance
            Buffer const pcBlindingFactor = generateBlindingFactor();
            Buffer const pedersenCommitment = mptAlice.getPedersenCommitment(maxMPTokenAmount, pcBlindingFactor);

            // Generate the proof using known spending balance value
            auto const version = mptAlice.getMPTokenVersion(bob);
            uint256 const convertBackContextHash =
                getConvertBackContextHash(bob.id(), env.seq(bob), mptAlice.issuanceID(), convertBackAmt, version);

            Buffer const proof = mptAlice.getConvertBackProof(
                bob,
                convertBackAmt,
                convertBackContextHash,
                {
                    .pedersenCommitment = pedersenCommitment,
                    .amt = maxMPTokenAmount,
                    .encryptedAmt = *encryptedSpendingBalance,
                    .blindingFactor = pcBlindingFactor,
                });

            {
                Json::Value jv;
                jv[jss::Account] = bob.human();
                jv[jss::TransactionType] = jss::ConfidentialMPTConvertBack;
                jv[sfMPTokenIssuanceID] = to_string(mptAlice.issuanceID());
                jv[sfMPTAmount.jsonName] = std::to_string(convertBackAmt);
                jv[sfHolderEncryptedAmount.jsonName] = strHex(convertBackHolderCiphertext);
                jv[sfIssuerEncryptedAmount.jsonName] = strHex(convertBackIssuerCiphertext);
                jv[sfBlindingFactor.jsonName] = strHex(convertBackBlindingFactor);
                jv[sfBalanceCommitment.jsonName] = strHex(pedersenCommitment);
                jv[sfZKProof.jsonName] = strHex(proof);

                env(jv, ter(tesSUCCESS));
            }

            // Verify the public balance was restored (minus 1 remaining in confidential)
            env.require(mptbalance(mptAlice, bob, convertBackAmt));
        }
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

        mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

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

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.convertBack({.account = bob, .amt = 30, .err = temDISABLED});
        }

        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            mptAlice.convert({
                .account = bob,
                .amt = 40,
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            mptAlice.mergeInbox({
                .account = bob,
            });

            mptAlice.convertBack({.account = alice, .amt = 30, .err = temMALFORMED});

            mptAlice.convertBack({.account = bob, .amt = 0, .err = temBAD_AMOUNT});

            mptAlice.convertBack({.account = bob, .amt = maxMPTokenAmount + 1, .err = temBAD_AMOUNT});

            // invalid blinding factor length
            mptAlice.convertBack({.account = alice, .amt = 30, .blindingFactor = Buffer{}, .err = temMALFORMED});

            // Balance commitment has correct length but invalid EC point data
            mptAlice.convertBack(
                {.account = bob,
                 .amt = 30,
                 .pedersenCommitment = Buffer(ecPedersenCommitmentLength),
                 .err = temMALFORMED});

            mptAlice.convertBack({.account = bob, .amt = 30, .holderEncryptedAmt = Buffer{}, .err = temBAD_CIPHERTEXT});

            mptAlice.convertBack({.account = bob, .amt = 30, .issuerEncryptedAmt = Buffer{}, .err = temBAD_CIPHERTEXT});

            mptAlice.convertBack(
                {.account = bob, .amt = 30, .holderEncryptedAmt = getBadCiphertext(), .err = temBAD_CIPHERTEXT});

            mptAlice.convertBack(
                {.account = bob, .amt = 30, .issuerEncryptedAmt = getBadCiphertext(), .err = temBAD_CIPHERTEXT});

            mptAlice.convertBack(
                {.account = bob, .amt = 30, .auditorEncryptedAmt = Buffer(10), .err = temBAD_CIPHERTEXT});

            mptAlice.convertBack(
                {.account = bob, .amt = 30, .auditorEncryptedAmt = getBadCiphertext(), .err = temBAD_CIPHERTEXT});

            // invalid proof length
            mptAlice.convertBack({.account = bob, .amt = 30, .proof = Buffer{}, .err = temMALFORMED});

            mptAlice.convertBack({.account = bob, .amt = 30, .proof = Buffer(100), .err = temMALFORMED});
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

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.destroy();
            mptAlice.generateKeyPair(bob);

            mptAlice.convertBack({.account = bob, .amt = 30, .err = tecOBJECT_NOT_FOUND});
        }

        // tfMPTCanPrivacy is not set on issuance
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.convertBack({.account = bob, .amt = 30, .err = tecNO_PERMISSION});
        }

        // no mptoken
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.convertBack({.account = bob, .amt = 30, .err = tecOBJECT_NOT_FOUND});
        }

        // bob doesn't have encrypted balances
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            mptAlice.convertBack({.account = bob, .amt = 30, .err = tecNO_PERMISSION});
        }

        // bob tries to convert back more than COA
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});
            mptAlice.pay(alice, bob, 100);
            mptAlice.pay(alice, carol, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

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

            mptAlice.convertBack({.account = bob, .amt = 300, .err = tecINSUFFICIENT_FUNDS});
        }

        // cannot convert if locked or unauth
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTRequireAuth | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = alice, .holder = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            mptAlice.convert({
                .account = bob,
                .amt = 40,
                .holderPubKey = mptAlice.getPubKey(bob),
            });
            mptAlice.mergeInbox({.account = bob});
            mptAlice.set({.account = alice, .holder = bob, .flags = tfMPTLock});

            mptAlice.convertBack({.account = bob, .amt = 10, .err = tecLOCKED});

            mptAlice.set({.account = alice, .holder = bob, .flags = tfMPTUnlock});

            mptAlice.convertBack({
                .account = bob,
                .amt = 10,
            });

            mptAlice.authorize({.account = alice, .holder = bob, .flags = tfMPTUnauthorize});

            mptAlice.convertBack({.account = bob, .amt = 10, .err = tecNO_AUTH});

            mptAlice.authorize({
                .account = alice,
                .holder = bob,
            });

            mptAlice.convertBack({
                .account = bob,
                .amt = 10,
            });
        }

        // Verification of holder and issuer ciphertexts during convertBack
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});
            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.convert({.account = bob, .amt = 50, .holderPubKey = mptAlice.getPubKey(bob)});
            mptAlice.mergeInbox({.account = bob});

            // Holder encrypted amount is valid format but mathematically incorrect for this convertBack
            mptAlice.convertBack(
                {.account = bob, .amt = 10, .holderEncryptedAmt = getTrivialCiphertext(), .err = tecBAD_PROOF});

            // Issuer encrypted amount is valid format but mathematically incorrect for this convertBack
            mptAlice.convertBack(
                {.account = bob, .amt = 10, .issuerEncryptedAmt = getTrivialCiphertext(), .err = tecBAD_PROOF});
        }

        // Alice has NOT set an auditor key, but Bob provides
        // auditorEncryptedAmt
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});
            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            // Bob converts funds to confidential so he has something to convert
            // back
            mptAlice.convert({.account = bob, .amt = 50, .holderPubKey = mptAlice.getPubKey(bob)});
            mptAlice.mergeInbox({.account = bob});

            mptAlice.convertBack(
                {.account = bob,
                 .amt = 10,
                 // Provide valid ciphertext to pass preflight
                 .auditorEncryptedAmt = getTrivialCiphertext(),
                 .err = tecNO_PERMISSION});
        }

        // we set the auditor key, but convertBack omits auditorEncryptedAmt
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const auditor("auditor");
            MPTTester mptAlice(env, alice, {.holders = {bob}, .auditor = auditor});

            mptAlice.create({.flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});
            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(auditor);
            mptAlice.set(
                {.account = alice,
                 .issuerPubKey = mptAlice.getPubKey(alice),
                 .auditorPubKey = mptAlice.getPubKey(auditor)});

            // Convert funds so Bob has a balance
            mptAlice.convert({
                .account = bob,
                .amt = 50,
                .holderPubKey = mptAlice.getPubKey(bob),
            });
            mptAlice.mergeInbox({.account = bob});

            // ConvertBack WITHOUT auditorEncryptedAmt
            mptAlice.convertBack({
                .account = bob,
                .amt = 10,
                .fillAuditorEncryptedAmt = false,
                .err = tecNO_PERMISSION,
            });

            // ConvertBack where auditor ciphertext mathematically
            // correct, but contains invalid data (mismatching amount).
            mptAlice.convertBack(
                {.account = bob, .amt = 10, .auditorEncryptedAmt = getTrivialCiphertext(), .err = tecBAD_PROOF});
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

        mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

        mptAlice.authorize({.account = bob});
        mptAlice.authorize({.account = carol});

        mptAlice.pay(alice, bob, 100);
        mptAlice.pay(alice, carol, 50);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);

        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

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
        mptAlice.send({.account = carol, .dest = bob, .amt = 10, .err = tecNO_PERMISSION});

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

        mptAlice.send({.account = carol, .dest = bob, .amt = 10, .credentials = {{credIdx}}});

        // Bob revoke authorization
        env(deposit::unauth(bob, carol));
        env.close();

        mptAlice.send({.account = carol, .dest = bob, .amt = 10, .err = tecNO_PERMISSION});

        mptAlice.send({.account = carol, .dest = bob, .amt = 10, .credentials = {{credIdx}}, .err = tecNO_PERMISSION});

        // Bob authorize credentials
        env(deposit::authCredentials(bob, {{dpIssuer, credType}}));
        env.close();

        mptAlice.send({.account = carol, .dest = bob, .amt = 10, .err = tecNO_PERMISSION});

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
        testcase("test ConfidentialMPTClawback");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const dave("dave");
        MPTTester mptAlice(env, alice, {.holders = {bob, carol, dave}});

        mptAlice.create({.flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanClawback | tfMPTCanPrivacy});
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
        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        // setup bob.
        // after setup, bob's spending balance is 60, inbox balance is 0.
        {
            // bob converts 60 to confidential
            mptAlice.convert({.account = bob, .amt = 60, .holderPubKey = mptAlice.getPubKey(bob)});

            // bob merge inbox
            mptAlice.mergeInbox({
                .account = bob,
            });
        }

        // setup carol.
        // after setup, carol's spending balance is 120, inbox balance is 0.
        {
            // carol converts 120 to confidential
            mptAlice.convert({.account = carol, .amt = 120, .holderPubKey = mptAlice.getPubKey(carol)});

            // carol merge inbox
            mptAlice.mergeInbox({
                .account = carol,
            });
        }

        // setup dave.
        // dave will not merge inbox.
        // after setup, dave's inbox balance is 200, spending balance is 0.
        mptAlice.convert({.account = dave, .amt = 200, .holderPubKey = mptAlice.getPubKey(dave)});

        // setup: carol confidential send 50 to bob.
        // after send, bob's inbox balance is 50, spending balance
        // remains 60. carol's inbox balance remains 0, spending balance
        // drops to 70.
        mptAlice.send({.account = carol, .dest = bob, .amt = 50});

        // alice clawback all confidential balance from bob, 110 in total.
        // bob has balance in both inbox and spending. These balances should
        // become zero after clawback, which is verified in the
        // confidentialClaw function.
        mptAlice.confidentialClaw({.account = alice, .holder = bob, .amt = 110});

        // alice clawback all confidential balance from carol, which is 70.
        // carol only has balance in spending.
        mptAlice.confidentialClaw({.account = alice, .holder = carol, .amt = 70});

        // alice clawback all confidential balance from dave, which is 200.
        // dave only has balance in inbox.
        mptAlice.confidentialClaw({.account = alice, .holder = dave, .amt = 200});
    }

    void
    testClawbackWithAuditor(FeatureBitset features)
    {
        testcase("test ConfidentialMPTClawback with auditor");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const dave("dave");
        Account const auditor("auditor");
        MPTTester mptAlice(env, alice, {.holders = {bob, carol, dave}, .auditor = auditor});

        mptAlice.create({.flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanClawback | tfMPTCanPrivacy});
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
            mptAlice.convert({.account = bob, .amt = 60, .holderPubKey = mptAlice.getPubKey(bob)});

            // bob merge inbox
            mptAlice.mergeInbox({
                .account = bob,
            });
        }

        // setup carol.
        // after setup, carol's spending balance is 120, inbox balance is 0.
        {
            // carol converts 120 to confidential
            mptAlice.convert({.account = carol, .amt = 120, .holderPubKey = mptAlice.getPubKey(carol)});

            // carol merge inbox
            mptAlice.mergeInbox({
                .account = carol,
            });
        }

        // setup dave.
        // dave will not merge inbox.
        // after setup, dave's inbox balance is 200, spending balance is 0.
        mptAlice.convert({.account = dave, .amt = 200, .holderPubKey = mptAlice.getPubKey(dave)});

        // setup: carol confidential send 50 to bob.
        // after send, bob's inbox balance is 50, spending balance
        // remains 60. carol's inbox balance remains 0, spending balance
        // drops to 70.
        mptAlice.send({.account = carol, .dest = bob, .amt = 50});

        // alice clawback all confidential balance from bob, 110 in total.
        // bob has balance in both inbox and spending. These balances should
        // become zero after clawback, which is verified in the
        // confidentialClaw function.
        mptAlice.confidentialClaw({.account = alice, .holder = bob, .amt = 110});

        // alice clawback all confidential balance from carol, which is 70.
        // carol only has balance in spending.
        mptAlice.confidentialClaw({.account = alice, .holder = carol, .amt = 70});

        // alice clawback all confidential balance from dave, which is 200.
        // dave only has balance in inbox.
        mptAlice.confidentialClaw({.account = alice, .holder = dave, .amt = 200});
    }

    void
    testClawbackPreflight(FeatureBitset features)
    {
        testcase("test ConfidentialMPTClawback Preflight");
        using namespace test::jtx;

        // test feature disabled
        {
            Env env{*this, features - featureConfidentialTransfer};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create();
            mptAlice.authorize({.account = bob});

            mptAlice.confidentialClaw({.account = alice, .holder = bob, .amt = 10, .proof = "123", .err = temDISABLED});
        }

        // test malformed
        {
            // set up
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});
            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(carol);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});
            mptAlice.pay(alice, bob, 100);
            mptAlice.pay(alice, carol, 50);

            // only issuer can clawback
            mptAlice.confidentialClaw({.account = carol, .holder = bob, .amt = 10, .err = temMALFORMED});

            // invalid issuance ID, whose issuer is not alice
            {
                Json::Value jv;
                jv[jss::Account] = alice.human();
                jv[sfHolder] = bob.human();
                jv[jss::TransactionType] = jss::ConfidentialMPTClawback;
                jv[sfMPTAmount] = std::to_string(10);
                jv[sfZKProof] = "123";

                // wrong issuance ID
                jv[sfMPTokenIssuanceID] = "00000004AE123A8556F3CF91154711376AFB0F894F832B3E";

                env(jv, ter(temMALFORMED));
            }

            // issuer cannot clawback from self
            mptAlice.confidentialClaw({.account = alice, .holder = alice, .amt = 10, .err = temMALFORMED});

            // invalid amount
            mptAlice.confidentialClaw({.account = alice, .holder = bob, .amt = 0, .err = temBAD_AMOUNT});

            // invalid proof length
            mptAlice.confidentialClaw(
                {.account = alice, .holder = bob, .amt = 10, .proof = "123", .err = temMALFORMED});
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

            mptAlice.create({.flags = tfMPTCanTransfer | tfMPTCanClawback | tfMPTRequireAuth | tfMPTCanPrivacy});
            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = alice, .holder = bob});
            mptAlice.authorize({.account = carol});
            mptAlice.authorize({.account = alice, .holder = carol});

            mptAlice.pay(alice, bob, 100);
            mptAlice.pay(alice, carol, 50);
            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(carol);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

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
                mptAlice.confidentialClaw({.account = alice, .holder = unknown, .amt = 10, .err = tecNO_TARGET});
            }

            // dave does not hold mpt at all, no MPT object
            {
                mptAlice.confidentialClaw({.account = alice, .holder = dave, .amt = 10, .err = tecOBJECT_NOT_FOUND});
            }

            // carol has no confidential balance
            {
                mptAlice.confidentialClaw({.account = alice, .holder = carol, .amt = 10, .err = tecNO_PERMISSION});
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
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.confidentialClaw({.account = alice, .holder = bob, .amt = 10, .err = tecNO_PERMISSION});
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

            mptAlice.confidentialClaw({.account = alice, .holder = bob, .amt = 10, .err = tecNO_PERMISSION});
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
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            // destroy the issuance
            mptAlice.destroy();

            Json::Value jv;
            jv[jss::Account] = alice.human();
            jv[sfHolder] = bob.human();
            jv[jss::TransactionType] = jss::ConfidentialMPTClawback;
            jv[sfMPTAmount] = std::to_string(10);
            std::string const dummyProof(196, '0');
            jv[sfZKProof] = dummyProof;
            jv[sfMPTokenIssuanceID] = to_string(mptAlice.issuanceID());

            env(jv, ter(tecOBJECT_NOT_FOUND));
        }

        // helper function to set up accounts to test lock and unauthorize
        // cases. after set up, bob has confidential balance 60 in spending.
        auto setupAccounts = [&](Env& env, Account const& alice, Account const& bob) -> MPTTester {
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.flags = tfMPTCanTransfer | tfMPTCanClawback | tfMPTRequireAuth | tfMPTCanLock | tfMPTCanPrivacy});
            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = alice, .holder = bob});
            mptAlice.pay(alice, bob, 100);
            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});
            mptAlice.convert({.account = bob, .amt = 60, .holderPubKey = mptAlice.getPubKey(bob)});
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
            mptAlice.confidentialClaw({.account = alice, .holder = bob, .amt = 60});
        }

        // lock globally
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice = setupAccounts(env, alice, bob);
            mptAlice.set({.account = alice, .flags = tfMPTLock});

            // clawback should still work
            mptAlice.confidentialClaw({.account = alice, .holder = bob, .amt = 60});
        }

        // unauthorize should not block clawback
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice = setupAccounts(env, alice, bob);

            // unauthorize bob
            mptAlice.authorize({.account = alice, .holder = bob, .flags = tfMPTUnauthorize});
            // clawback should still work
            mptAlice.confidentialClaw({.account = alice, .holder = bob, .amt = 60});
        }

        // insufficient funds, clawback amount exceeding confidential
        // outstanding amount
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice = setupAccounts(env, alice, bob);

            mptAlice.confidentialClaw({.account = alice, .holder = bob, .amt = 10000, .err = tecINSUFFICIENT_FUNDS});
        }
    }

    void
    testClawbackProof(FeatureBitset features)
    {
        testcase("ConfidentialMPTClawback Proof");
        using namespace test::jtx;

        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");

        // lambda function to set up MPT with alice as issuer, bob and carol
        // as authorized holders, and fund 1000 mpt to bob and 2000 mpt to
        // carol.
        auto setupEnv = [&](Env& env) -> MPTTester {
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create({.flags = tfMPTCanTransfer | tfMPTCanClawback | tfMPTCanPrivacy});

            for (auto const& [acct, amt] : {std::pair{bob, 1000}, {carol, 2000}})
            {
                mptAlice.authorize({.account = acct});
                mptAlice.pay(alice, acct, amt);
                mptAlice.generateKeyPair(acct);
            }

            mptAlice.generateKeyPair(alice);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            return mptAlice;
        };

        // lambda function to test a set of bad clawback amounts that should
        // return tecBAD_PROOF
        auto checkBadProofs = [&](MPTTester& mpt, Account const& holder, std::initializer_list<uint64_t> amts) {
            for (auto const badAmt : amts)
            {
                mpt.confidentialClaw({.account = alice, .holder = holder, .amt = badAmt, .err = tecBAD_PROOF});
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
            mptAlice.convert({.account = bob, .amt = 500, .holderPubKey = mptAlice.getPubKey(bob)});
            mptAlice.mergeInbox({
                .account = bob,
            });
            // carol converts without merge
            mptAlice.convert({.account = carol, .amt = 1000, .holderPubKey = mptAlice.getPubKey(carol)});

            // verify proof fails with invalid clawback amount
            // bob: 500 in Spending, 0 in Inbox
            checkBadProofs(mptAlice, bob, {1, 10, 70, 100, 110, 200, 499, 501, 600});

            // carol: 1000 in Inbox, 0 in Spending
            checkBadProofs(mptAlice, carol, {1, 10, 50, 500, 777, 850, 999, 1001, 1200});

            // clawback with correct amount that passes proof verification
            mptAlice.confidentialClaw({.account = alice, .holder = bob, .amt = 500});
            mptAlice.confidentialClaw({.account = alice, .holder = carol, .amt = 1000});
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

            mptAlice.convert({.account = bob, .amt = 300, .holderPubKey = mptAlice.getPubKey(bob)});
            mptAlice.mergeInbox({
                .account = bob,
            });
            mptAlice.convert({.account = carol, .amt = 400, .holderPubKey = mptAlice.getPubKey(carol)});
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
            checkBadProofs(mptAlice, carol, {1, 10, 50, 100, 300, 399, 401, 501});

            // clawback with correct amount that passes proof verification
            mptAlice.confidentialClaw({.account = alice, .holder = bob, .amt = 300});
            mptAlice.confidentialClaw({.account = alice, .holder = carol, .amt = 400});
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

            mptAlice.create({.ownerCount = 0, .mutableFlags = tmfMPTCannotMutatePrivacy, .err = temDISABLED});
        }

        // can not create mpt issuance with tmfMPTCannotMutatePrivacy when
        // featureConfidentialTransfer is disabled
        {
            Env env{*this, features - featureConfidentialTransfer};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 0, .mutableFlags = tmfMPTCannotMutatePrivacy, .err = temDISABLED});
        }

        // if lsmfMPTCannotMutatePrivacy is set, can not set/clear
        // lsfMPTCanPrivacy
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer, .mutableFlags = tmfMPTCannotMutatePrivacy});

            mptAlice.set({.account = alice, .mutableFlags = tmfMPTSetPrivacy, .err = tecNO_PERMISSION});

            mptAlice.set({.account = alice, .mutableFlags = tmfMPTClearPrivacy, .err = tecNO_PERMISSION});
        }

        // Toggle lsfMPTCanPrivacy
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create(
                {.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanPrivacy, .mutableFlags = tmfMPTCanMutateCanLock});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            auto holderPubKeySet = false;
            auto verifyToggle = [&](TER expectedResult, uint64_t amt) {
                if (!holderPubKeySet)
                    mptAlice.convert(
                        {.account = bob, .amt = amt, .holderPubKey = mptAlice.getPubKey(bob), .err = expectedResult});
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
            mptAlice.set({.account = alice, .mutableFlags = tmfMPTClearPrivacy});
            verifyToggle(tecNO_PERMISSION, 10);

            // can clear lsfMPTCanPrivacy again but has no effect
            // for privacy settings
            mptAlice.set({.account = alice, .mutableFlags = tmfMPTClearPrivacy | tmfMPTSetCanLock});
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
            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanPrivacy});

            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            // bob convert 50 to confidential
            mptAlice.convert({.account = bob, .amt = 50, .holderPubKey = mptAlice.getPubKey(bob)});

            // set or clear lsfMPTCanPrivacy should fail because of
            // confidential outstanding balance
            mptAlice.set({.account = alice, .mutableFlags = tmfMPTSetPrivacy, .err = tecNO_PERMISSION});
            mptAlice.set({.account = alice, .mutableFlags = tmfMPTClearPrivacy, .err = tecNO_PERMISSION});

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
            mptAlice.set({.account = alice, .mutableFlags = tmfMPTClearPrivacy});

            // bob can not convert because lsfMPTCanPrivacy was cleared
            // successfully
            mptAlice.convert(
                {.account = bob, .amt = 10, .holderPubKey = mptAlice.getPubKey(bob), .err = tecNO_PERMISSION});

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
    testConvertBackPedersenProof(FeatureBitset features)
    {
        testcase("Convert back pedersen proof");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        MPTTester mptAlice(env, alice, {.holders = {bob}});

        // --------------- Setup test --------------- //
        mptAlice.create(
            {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 100);

        mptAlice.generateKeyPair(alice);

        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

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

        auto const spendingBalance = mptAlice.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING);
        BEAST_EXPECT(spendingBalance.has_value());
        auto const encryptedSpendingBalance = mptAlice.getEncryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING);
        BEAST_EXPECT(encryptedSpendingBalance.has_value() && !encryptedSpendingBalance->empty());

        Buffer const pedersenCommitment = mptAlice.getPedersenCommitment(*spendingBalance, pcBlindingFactor);
        Buffer const issuerCiphertext = mptAlice.encryptAmount(alice, amt, blindingFactor);
        Buffer const bobCiphertext = mptAlice.encryptAmount(bob, amt, blindingFactor);
        auto const version = mptAlice.getMPTokenVersion(bob);

        // --------------- Finish setup --------------- //

        // These tests verify that the pedersen linkage proof validation
        // correctly rejects proofs generated with incorrect parameters.
        // The pedersen linkage proof proves that the balance commitment
        // PC = balance*G + rho*H is derived from the holder's encrypted
        // spending balance.

        // Helper to combine pedersen proof and bulletproof
        auto const combineProofs = [](Buffer const& pedersenProof, Buffer const& bulletproof) {
            Buffer combinedProof(pedersenProof.size() + bulletproof.size());
            std::memcpy(combinedProof.data(), pedersenProof.data(), pedersenProof.size());
            std::memcpy(combinedProof.data() + pedersenProof.size(), bulletproof.data(), bulletproof.size());
            return combinedProof;
        };

        auto const holderPubKey = mptAlice.getPubKey(bob);
        BEAST_EXPECT(holderPubKey.has_value());

        // Test 1: Proof generated with wrong pedersen commitment value.
        // The proof uses PC(1, rho) but the transaction submits PC(balance, rho).
        // Verification fails because the proof doesn't match the submitted commitment.
        {
            uint256 const contextHash =
                getConvertBackContextHash(bob, env.seq(bob), mptAlice.issuanceID(), amt, version);
            Buffer const badPedersenCommitment = mptAlice.getPedersenCommitment(1, pcBlindingFactor);
            Buffer const proof = mptAlice.getConvertBackProof(
                bob,
                amt,
                contextHash,
                {
                    .pedersenCommitment = badPedersenCommitment,  // wrong pedersen commitment
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

        // Test 2: Proof generated with wrong blinding factor (rho).
        // The pedersen commitment PC = balance*G + rho*H requires the same rho
        // used in proof generation. Using a different rho breaks the linkage.
        {
            uint256 const contextHash =
                getConvertBackContextHash(bob, env.seq(bob), mptAlice.issuanceID(), amt, version);

            Buffer const proof = mptAlice.getConvertBackProof(
                bob,
                amt,
                contextHash,
                {
                    .pedersenCommitment = pedersenCommitment,
                    .amt = *spendingBalance,
                    .encryptedAmt = *encryptedSpendingBalance,
                    .blindingFactor = generateBlindingFactor(),  // wrong blinding factor
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

        // Test 3: Proof generated with wrong balance value.
        // The proof claims balance=1 but the encrypted spending balance contains
        // the actual balance. Verification fails because the values don't match.
        {
            uint256 const contextHash =
                getConvertBackContextHash(bob, env.seq(bob), mptAlice.issuanceID(), amt, version);

            Buffer const proof = mptAlice.getConvertBackProof(
                bob,
                amt,
                contextHash,
                {
                    .pedersenCommitment = pedersenCommitment,
                    .amt = 1,  // wrong balance
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

        // Test 4: Correct proof but wrong pedersen commitment in transaction.
        // The proof is generated correctly, but the transaction submits a
        // different pedersen commitment. Verification fails because the
        // submitted commitment doesn't match what the proof was generated for.
        {
            uint256 const contextHash =
                getConvertBackContextHash(bob, env.seq(bob), mptAlice.issuanceID(), amt, version);
            Buffer const badPedersenCommitment = mptAlice.getPedersenCommitment(1, pcBlindingFactor);
            Buffer const proof = mptAlice.getConvertBackProof(
                bob,
                amt,
                contextHash,
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
                 .pedersenCommitment = badPedersenCommitment,  // wrong pedersen commitment
                 .err = tecBAD_PROOF});
        }

        // Test 5: Proof generated with wrong context hash.
        // The context hash binds the proof to a specific transaction (account,
        // sequence, issuanceID, amount, version). Using a different context hash
        // makes the proof invalid for this transaction, preventing replay attacks.
        {
            uint256 const contextHash =
                getConvertBackContextHash(bob, env.seq(bob), mptAlice.issuanceID(), amt, version);
            uint256 const badContextHash{1};
            Buffer const pedersenProof = mptAlice.getBalanceLinkageProof(
                bob,
                badContextHash,  // wrong context hash
                *holderPubKey,
                {
                    .pedersenCommitment = pedersenCommitment,
                    .amt = *spendingBalance,
                    .encryptedAmt = *encryptedSpendingBalance,
                    .blindingFactor = pcBlindingFactor,
                });

            // Bulletproof uses correct context hash so only pedersen proof fails
            Buffer const bulletproof =
                mptAlice.getBulletproof({*spendingBalance - amt}, {pcBlindingFactor}, contextHash);

            Buffer const proof = combineProofs(pedersenProof, bulletproof);

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

        // Test 6: Correct proof to verify the test setup is valid.
        // All parameters are correct, so the transaction should succeed.
        {
            uint256 const contextHash =
                getConvertBackContextHash(bob, env.seq(bob), mptAlice.issuanceID(), amt, version);

            Buffer const proof = mptAlice.getConvertBackProof(
                bob,
                amt,
                contextHash,
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
    testConvertBackBulletproof(FeatureBitset features)
    {
        testcase("Convert back bulletproof");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        MPTTester mptAlice(env, alice, {.holders = {bob}});

        // --------------- Setup test --------------- //
        mptAlice.create(
            {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanPrivacy});

        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 100);

        mptAlice.generateKeyPair(alice);

        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

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

        auto const spendingBalance = mptAlice.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING);
        BEAST_EXPECT(spendingBalance.has_value());
        auto const encryptedSpendingBalance = mptAlice.getEncryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING);
        BEAST_EXPECT(encryptedSpendingBalance.has_value() && !encryptedSpendingBalance->empty());

        Buffer const pedersenCommitment = mptAlice.getPedersenCommitment(*spendingBalance, pcBlindingFactor);
        Buffer const issuerCiphertext = mptAlice.encryptAmount(alice, amt, blindingFactor);
        Buffer const bobCiphertext = mptAlice.encryptAmount(bob, amt, blindingFactor);
        auto const version = mptAlice.getMPTokenVersion(bob);

        // --------------- Finish setup --------------- //

        // These tests verify that the bulletproof (range proof) validation
        // correctly rejects proofs generated with incorrect parameters.
        // The bulletproof proves that the remaining balance (balance - amount)
        // is non-negative, i.e., in the range [0, 2^64-1]. This prevents
        // overdrafts where a user tries to convert back more than they have.

        // Helper to combine pedersen proof and bulletproof
        auto const combineProofs = [](Buffer const& pedersenProof, Buffer const& bulletproof) {
            Buffer combinedProof(pedersenProof.size() + bulletproof.size());
            std::memcpy(combinedProof.data(), pedersenProof.data(), pedersenProof.size());
            std::memcpy(combinedProof.data() + pedersenProof.size(), bulletproof.data(), bulletproof.size());
            return combinedProof;
        };

        auto const holderPubKey = mptAlice.getPubKey(bob);
        BEAST_EXPECT(holderPubKey.has_value());

        // Helper to generate pedersen proof with correct parameters.
        // The pedersen proof links the encrypted balance to the pedersen commitment.
        auto const getPedersenProof = [&](uint256 const& contextHash) {
            return mptAlice.getBalanceLinkageProof(
                bob,
                contextHash,
                *holderPubKey,
                {
                    .pedersenCommitment = pedersenCommitment,
                    .amt = *spendingBalance,
                    .encryptedAmt = *encryptedSpendingBalance,
                    .blindingFactor = pcBlindingFactor,
                });
        };

        // Test 1: Bulletproof generated with wrong remaining balance.
        // The bulletproof claims remaining balance is 1, but the pedersen
        // commitment was created with (balance - amount). The verifier computes
        // PC_rem = PC - amount*G and checks if the bulletproof matches, which fails.
        {
            uint256 const contextHash =
                getConvertBackContextHash(bob, env.seq(bob), mptAlice.issuanceID(), amt, version);

            Buffer const bulletproof = mptAlice.getBulletproof(
                {1},  // wrong remaining balance
                {pcBlindingFactor},
                contextHash);

            Buffer const proof = combineProofs(getPedersenProof(contextHash), bulletproof);

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

        // Test 2: Bulletproof generated with wrong blinding factor.
        // The bulletproof must use the same blinding factor (rho) as the pedersen
        // commitment PC = (balance - amount)*G + rho*H. Using a different rho
        // creates a commitment mismatch and verification fails.
        {
            uint256 const contextHash =
                getConvertBackContextHash(bob, env.seq(bob), mptAlice.issuanceID(), amt, version);

            Buffer const bulletproof = mptAlice.getBulletproof(
                {*spendingBalance - amt},
                {generateBlindingFactor()},  // wrong blinding factor
                contextHash);

            Buffer const proof = combineProofs(getPedersenProof(contextHash), bulletproof);

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

        // Test 3: Bulletproof generated with wrong context hash.
        // The context hash binds the proof to a specific transaction (account,
        // sequence, issuanceID, amount, version). Using a different context hash
        // makes the proof invalid for this transaction, preventing replay attacks.
        {
            uint256 const contextHash =
                getConvertBackContextHash(bob, env.seq(bob), mptAlice.issuanceID(), amt, version);

            uint256 const badContextHash{1};
            Buffer const bulletproof = mptAlice.getBulletproof(
                {*spendingBalance - amt},
                {pcBlindingFactor},
                badContextHash);  // wrong context hash

            Buffer const proof = combineProofs(getPedersenProof(contextHash), bulletproof);

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

        // Test 4: Correct proof to verify the test setup is valid.
        // All parameters are correct, so the transaction should succeed.
        {
            uint256 const contextHash =
                getConvertBackContextHash(bob, env.seq(bob), mptAlice.issuanceID(), amt, version);

            Buffer const proof = mptAlice.getConvertBackProof(
                bob,
                amt,
                contextHash,
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
        // ConfidentialMPTConvert
        testConvert(features);
        testConvertPreflight(features);
        testConvertPreclaim(features);
        testConvertWithAuditor(features);

        // ConfidentialMPTMergeInbox
        testMergeInbox(features);
        testMergeInboxPreflight(features);
        testMergeInboxPreclaim(features);

        testSetPreflight(features);

        // ConfidentialMPTSend
        testSend(features);
        testSendPreflight(features);
        testSendPreclaim(features);
        testSendRangeProof(features);
        testSendDepositPreauth(features);
        testSendWithAuditor(features);

        // ConfidentialMPTClawback
        testClawback(features);
        testClawbackPreflight(features);
        testClawbackPreclaim(features);
        testClawbackProof(features);
        testClawbackWithAuditor(features);

        testDelete(features);

        // ConfidentialMPTConvertBack
        testConvertBack(features);
        testConvertBackPreflight(features);
        testConvertBackPreclaim(features);
        testConvertBackWithAuditor(features);
        testConvertBackPedersenProof(features);
        testConvertBackBulletproof(features);

        // todo: this test fails because proof generation for convertback fails if remainder amount is 0
        //  testMutatePrivacy(features);
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

BEAST_DEFINE_TESTSUITE(ConfidentialTransfer, app, xrpl);
}  // namespace xrpl
