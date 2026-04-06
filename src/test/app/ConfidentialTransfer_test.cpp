#include <test/jtx.h>
#include <test/jtx/ticket.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Protocol.h>

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

            buf.data()[0] = ecCompressedPrefixEvenY;
            buf.data()[ecGamalEncryptedLength] = ecCompressedPrefixEvenY;
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

            buf.data()[0] = ecCompressedPrefixEvenY;
            buf.data()[ecGamalEncryptedLength] = ecCompressedPrefixEvenY;

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

            buf.data()[0] = ecCompressedPrefixEvenY;
            // Set last byte to make it a valid x-coordinate on the curve
            buf.data()[ecPedersenCommitmentLength - 1] = 0x01;

            return buf;
        }();

        return trivialCommitment;
    }

    std::string
    getTrivialSendProofHex(size_t nRecipients)
    {
        size_t const sizeEquality = getEqualityProofSize(nRecipients);
        size_t const totalSize =
            sizeEquality + (2 * ecPedersenProofLength) + ecDoubleBulletproofLength;

        Buffer buf(totalSize);
        std::memset(buf.data(), 0, totalSize);

        for (std::size_t i = 0; i < totalSize; i += ecGamalEncryptedLength)
        {
            buf.data()[i] = ecCompressedPrefixEvenY;
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

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
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

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
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

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
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
            auto const holderCiphertext =
                mptAlice.encryptAmount(bob, maxMPTokenAmount, blindingFactor);
            auto const issuerCiphertext =
                mptAlice.encryptAmount(alice, maxMPTokenAmount, blindingFactor);

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
        MPTTester mptAlice(
            env,
            alice,
            {
                .holders = {bob},
                .auditor = auditor,
            });

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
        });

        mptAlice.authorize({
            .account = bob,
        });
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

            mptAlice.create({
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });
            mptAlice.generateKeyPair(alice);

            mptAlice.convert({
                .account = alice,
                .amt = 10,
                .holderPubKey = mptAlice.getPubKey(alice),
                .err = temMALFORMED,
            });
        }

        {
            Env env{*this, features - featureConfidentialTransfer};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(alice),
                .err = temDISABLED,
            });

            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .holderPubKey = mptAlice.getPubKey(bob),
                .err = temDISABLED,
            });
        }

        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.convert({
                .account = alice,
                .amt = 10,
                .holderPubKey = mptAlice.getPubKey(bob),
                .err = temMALFORMED,
            });

            // Holder encrypted amount is empty (length 0)
            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .holderPubKey = mptAlice.getPubKey(bob),
                .holderEncryptedAmt = Buffer{},
                .err = temBAD_CIPHERTEXT,
            });

            // Issuer encrypted amount is empty (length 0)
            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .holderPubKey = mptAlice.getPubKey(bob),
                .issuerEncryptedAmt = Buffer{},
                .err = temBAD_CIPHERTEXT,
            });

            // Auditor encrypted amount has invalid length (must be 66 bytes)
            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .holderPubKey = mptAlice.getPubKey(bob),
                .auditorEncryptedAmt = makeZeroBuffer(10),
                .err = temBAD_CIPHERTEXT,
            });

            // Auditor encrypted amount has correct length but invalid data
            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .holderPubKey = mptAlice.getPubKey(bob),
                .auditorEncryptedAmt = getBadCiphertext(),
                .err = temBAD_CIPHERTEXT,
            });

            // Amount exceeds maximum allowed MPT amount
            mptAlice.convert({
                .account = bob,
                .amt = maxMPTokenAmount + 1,
                .holderPubKey = mptAlice.getPubKey(bob),
                .err = temBAD_AMOUNT,
            });

            // Holder encrypted amount has correct length but invalid data
            mptAlice.convert({
                .account = bob,
                .amt = 1,
                .holderPubKey = mptAlice.getPubKey(bob),
                .holderEncryptedAmt = getBadCiphertext(),
                .err = temBAD_CIPHERTEXT,
            });

            // Issuer encrypted amount has correct length but invalid data (not
            // a valid EC point)
            mptAlice.convert({
                .account = bob,
                .amt = 1,
                .holderPubKey = mptAlice.getPubKey(bob),
                .issuerEncryptedAmt = getBadCiphertext(),
                .err = temBAD_CIPHERTEXT,
            });

            // Holder public key is invalid (empty buffer)
            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .holderPubKey = Buffer{},
                .err = temMALFORMED,
            });

            // Holder public key has correct length but invalid EC point data
            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .holderPubKey = makeZeroBuffer(ecPubKeyLength),
                .err = temMALFORMED,
            });
        }

        // when registering holder pub key, the transaction must include a
        // Schnorr proof of knowledge for the corresponding secret key
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .fillSchnorrProof = false,
                .holderPubKey = mptAlice.getPubKey(bob),
                .err = temMALFORMED,
            });

            mptAlice.convert({
                .account = bob,
                .amt = 0,
                .fillSchnorrProof = false,
                .holderPubKey = mptAlice.getPubKey(bob),
                .err = temMALFORMED,
            });

            // proof length is invalid
            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .proof = std::string(10, 'A'),
                .holderPubKey = mptAlice.getPubKey(bob),
                .err = temMALFORMED,
            });
        }

        // when holder pub key already registered, Schnorr proof must not be
        // provided
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
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
            mptAlice.convert({
                .account = bob,
                .amt = 20,
                .fillSchnorrProof = true,
                .err = temMALFORMED,
            });
        }
    }

    void
    testSet(FeatureBitset features)
    {
        testcase("Set");
        using namespace test::jtx;

        // Set keys on issuance that already has confidential amounts enabled
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const auditor("auditor");
            MPTTester mptAlice(env, alice, {.holders = {}, .auditor = auditor});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(auditor);

            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(alice),
                .auditorPubKey = mptAlice.getPubKey(auditor),
            });
        }

        // Enable confidential amounts flag only (no keys)
        {
            Env env{*this, features};
            Account const alice("alice");
            MPTTester mptAlice(env, alice, {.holders = {}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock,
            });

            mptAlice.set({
                .account = alice,
                .mutableFlags = tmfMPTSetCanConfidentialAmount,
            });
        }

        // Set keys when enabling confidential amounts in the same tx
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const auditor("auditor");
            MPTTester mptAlice(env, alice, {.holders = {}, .auditor = auditor});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock,
            });

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(auditor);

            mptAlice.set({
                .account = alice,
                .mutableFlags = tmfMPTSetCanConfidentialAmount,
                .issuerPubKey = mptAlice.getPubKey(alice),
                .auditorPubKey = mptAlice.getPubKey(auditor),
            });

            // Verify lsfMPTCanConfidentialAmount flag is set
            BEAST_EXPECT(mptAlice.checkFlags(
                lsfMPTCanTransfer | lsfMPTCanLock | lsfMPTCanConfidentialAmount));

            // Verify keys are persisted on the issuance
            auto const sle = env.le(keylet::mptIssuance(mptAlice.issuanceID()));
            BEAST_EXPECT(sle);
            BEAST_EXPECT(sle->isFieldPresent(sfIssuerEncryptionKey));
            BEAST_EXPECT(sle->isFieldPresent(sfAuditorEncryptionKey));
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

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(alice),
                .err = temDISABLED,
            });
        }

        // pub key is invalid
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            // Issuer pub key is invalid (empty)
            mptAlice.set({
                .account = alice,
                .issuerPubKey = Buffer{},
                .err = temMALFORMED,
            });

            // Issuer pub key has correct length but invalid EC point data
            mptAlice.set({
                .account = alice,
                .issuerPubKey = makeZeroBuffer(ecPubKeyLength),
                .err = temMALFORMED,
            });

            // Auditor key is invalid length
            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(alice),
                .auditorPubKey = makeZeroBuffer(10),
                .err = temMALFORMED,
            });

            // Auditor key has correct length but invalid EC point data
            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(alice),
                .auditorPubKey = makeZeroBuffer(ecPubKeyLength),
                .err = temMALFORMED,
            });

            // Cannot set auditor key without issuer key
            mptAlice.set({
                .account = alice,
                .auditorPubKey = mptAlice.getPubKey(alice),
                .err = temMALFORMED,
            });

            // Cannot set Holder and issuer Keys in the same transaction
            mptAlice.set({
                .account = alice,
                .holder = bob,
                .issuerPubKey = mptAlice.getPubKey(alice),
                .err = temMALFORMED,
            });

            // Cannot set keys while clearing confidential amount
            mptAlice.set({
                .account = alice,
                .mutableFlags = tmfMPTClearCanConfidentialAmount,
                .issuerPubKey = mptAlice.getPubKey(alice),
                .err = temINVALID_FLAG,
            });

            // Cannot set Holder and auditor Keys in the same transaction
            mptAlice.set({
                .account = alice,
                .holder = bob,
                .auditorPubKey = mptAlice.getPubKey(alice),
                .err = temMALFORMED,
            });
        }
    }

    void
    testSetPreclaim(FeatureBitset features)
    {
        testcase("Set preclaim");
        using namespace test::jtx;

        // Cannot set issuer key if confidential amounts not enabled
        {
            Env env{*this, features};
            Account const alice("alice");
            MPTTester mptAlice(env, alice, {.holders = {}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock,
            });

            mptAlice.generateKeyPair(alice);

            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(alice),
                .err = tecNO_PERMISSION,
            });
        }

        // Cannot update issuer public key once set
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            // First set issuer key - should succeed
            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(alice),
            });

            // Try to update issuer key - should fail
            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(bob),
                .err = tecNO_PERMISSION,
            });
        }

        // Cannot update issuer and auditor public keys once set
        // Note: trying to set only auditor key fails in preflight (temMALFORMED)
        // so we must provide both keys, which fails on issuer key check first
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const auditor("auditor");
            MPTTester mptAlice(env, alice, {.holders = {bob}, .auditor = auditor});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(auditor);

            // Set issuer and auditor keys - should succeed
            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(alice),
                .auditorPubKey = mptAlice.getPubKey(auditor),
            });

            // Try to update both keys - fails on issuer key check first
            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(bob),
                .auditorPubKey = mptAlice.getPubKey(alice),
                .err = tecNO_PERMISSION,
            });
        }

        // Cannot set auditor key if confidential amounts not enabled
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const auditor("auditor");
            MPTTester mptAlice(env, alice, {.holders = {}, .auditor = auditor});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock,
            });

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(auditor);

            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(alice),
                .auditorPubKey = mptAlice.getPubKey(auditor),
                .err = tecNO_PERMISSION,
            });
        }

        // Cannot set keys when mutation of canConfidentialAmount is disallowed
        {
            Env env{*this, features};
            Account const alice("alice");
            MPTTester mptAlice(env, alice, {.holders = {}});

            // Create with tmfMPTCannotMutateCanConfidentialAmount
            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock,
                .mutableFlags = tmfMPTCannotMutateCanConfidentialAmount,
            });

            mptAlice.generateKeyPair(alice);

            // Trying to enable confidential amounts and set keys fails
            // because the issuance cannot mutate canConfidentialAmount
            mptAlice.set({
                .account = alice,
                .mutableFlags = tmfMPTSetCanConfidentialAmount,
                .issuerPubKey = mptAlice.getPubKey(alice),
                .err = tecNO_PERMISSION,
            });
        }

        // Set issuer key first, then auditor key in a separate tx
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const auditor("auditor");
            MPTTester mptAlice(env, alice, {.holders = {}, .auditor = auditor});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(auditor);

            // Set issuer key only
            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(alice),
            });

            // Set auditor key in a separate tx - requires issuer key in tx
            // (preflight enforces auditor key requires issuer key)
            // This fails because issuer key is already set on ledger
            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(alice),
                .auditorPubKey = mptAlice.getPubKey(auditor),
                .err = tecNO_PERMISSION,
            });
        }
    }

    void
    testConvertPreclaim(FeatureBitset features)
    {
        testcase("Convert preclaim");
        using namespace test::jtx;

        // tfMPTCanConfidentialAmount is not set on issuance
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .holderPubKey = mptAlice.getPubKey(bob),
                .err = tecNO_PERMISSION,
            });
        }

        // issuer has not uploaded their sfIssuerEncryptionKey
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .holderPubKey = mptAlice.getPubKey(bob),
                .err = tecNO_PERMISSION,
            });
        }

        // issuance does not exist
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.destroy();
            mptAlice.generateKeyPair(bob);

            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .holderPubKey = mptAlice.getPubKey(bob),
                .err = tecOBJECT_NOT_FOUND,
            });
        }

        // bob has not created MPToken
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .holderPubKey = mptAlice.getPubKey(bob),
                .err = tecOBJECT_NOT_FOUND,
            });
        }

        // Verification of Issuer and and holder ciphertexts
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .holderPubKey = mptAlice.getPubKey(bob),
                .holderEncryptedAmt = getTrivialCiphertext(),
                .err = tecBAD_PROOF,
            });

            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .holderPubKey = mptAlice.getPubKey(bob),
                .issuerEncryptedAmt = getTrivialCiphertext(),
                .err = tecBAD_PROOF,
            });
        }

        // trying to convert more than what bob has
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            mptAlice.convert({
                .account = bob,
                .amt = 200,
                .holderPubKey = mptAlice.getPubKey(bob),
                .err = tecINSUFFICIENT_FUNDS,
            });
        }

        // holder cannot upload pk again
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            mptAlice.convert({.account = bob, .amt = 10, .holderPubKey = mptAlice.getPubKey(bob)});

            // cannot upload pk again
            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .holderPubKey = mptAlice.getPubKey(bob),
                .err = tecDUPLICATE,
            });
        }

        // cannot convert if locked
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.set({
                .account = alice,
                .holder = bob,
                .flags = tfMPTLock,
            });

            mptAlice.generateKeyPair(bob);

            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .holderPubKey = mptAlice.getPubKey(bob),
                .err = tecLOCKED,
            });

            mptAlice.set({
                .account = alice,
                .holder = bob,
                .flags = tfMPTUnlock,
            });

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

            mptAlice.create({
                .ownerCount = 1,
                .flags =
                    tfMPTCanTransfer | tfMPTCanLock | tfMPTRequireAuth | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.authorize({
                .account = alice,
                .holder = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            // Unauthorize bob
            mptAlice.authorize({
                .account = alice,
                .holder = bob,
                .flags = tfMPTUnauthorize,
            });

            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .holderPubKey = mptAlice.getPubKey(bob),
                .err = tecNO_AUTH,
            });

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

        // frozen account cannot bypass freeze check with amount=0
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            // lock bob
            mptAlice.set({
                .account = alice,
                .holder = bob,
                .flags = tfMPTLock,
            });

            mptAlice.generateKeyPair(bob);

            // amount=0 should still be rejected when locked
            mptAlice.convert({
                .account = bob,
                .amt = 0,
                .holderPubKey = mptAlice.getPubKey(bob),
                .err = tecLOCKED,
            });
        }

        // unauthorized account cannot bypass auth check with amount=0
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags =
                    tfMPTCanTransfer | tfMPTCanLock | tfMPTRequireAuth | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.authorize({
                .account = alice,
                .holder = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            // Unauthorize bob
            mptAlice.authorize({
                .account = alice,
                .holder = bob,
                .flags = tfMPTUnauthorize,
            });

            // amount=0 should still be rejected when unauthorized
            mptAlice.convert({
                .account = bob,
                .amt = 0,
                .holderPubKey = mptAlice.getPubKey(bob),
                .err = tecNO_AUTH,
            });
        }

        // cannot convert if auditor key is set, but auditor amount is not
        // provided
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const auditor("auditor");
            MPTTester mptAlice(
                env,
                alice,
                {
                    .holders = {bob},
                    .auditor = auditor,
                });

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(auditor);

            mptAlice.set(
                {.account = alice,
                 .issuerPubKey = mptAlice.getPubKey(alice),
                 .auditorPubKey = mptAlice.getPubKey(auditor)});

            // no auditor encrypted amt provided
            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .fillAuditorEncryptedAmt = false,
                .holderPubKey = mptAlice.getPubKey(bob),
                .err = tecNO_PERMISSION,
            });
        }

        // cannot convert if tx include auditor ciphertext, but does not have
        // auditing enabled
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            // there is no auditor key set
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .holderPubKey = mptAlice.getPubKey(bob),
                .auditorEncryptedAmt = getTrivialCiphertext(),
                .err = tecNO_PERMISSION,
            });
        }

        // Auditor key set successfully, auditor ciphertext mathematically
        // correct, but contains invalid data (mismatching amount).
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const auditor("auditor");
            MPTTester mptAlice(
                env,
                alice,
                {
                    .holders = {bob},
                    .auditor = auditor,
                });

            mptAlice.create({
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(auditor);

            mptAlice.set(
                {.account = alice,
                 .issuerPubKey = mptAlice.getPubKey(alice),
                 .auditorPubKey = mptAlice.getPubKey(auditor)});

            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .holderPubKey = mptAlice.getPubKey(bob),
                .auditorEncryptedAmt = getTrivialCiphertext(),
                .err = tecBAD_PROOF,
            });
        }

        // invalid proof when registering holder pub key
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .proof = std::string(ecSchnorrProofLength * 2, 'A'),
                .holderPubKey = mptAlice.getPubKey(bob),
                .err = tecBAD_PROOF,
            });
        }

        // no holder key on ledger and no key in tx
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            // bob has not registered a holder key, and doesn't provide one
            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .err = tecNO_PERMISSION,
            });
        }

        // all public balance already converted, try to convert more
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            // convert entire public balance
            mptAlice.convert({
                .account = bob,
                .amt = 100,
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            env.require(mptbalance(mptAlice, bob, 0));

            // try to convert 1 more — no public balance left
            mptAlice.convert({
                .account = bob,
                .amt = 1,
                .err = tecINSUFFICIENT_FUNDS,
            });
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

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
        });

        mptAlice.authorize({
            .account = bob,
        });
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

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
        });

        mptAlice.authorize({
            .account = bob,
        });
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
            .account = alice,
            .err = temMALFORMED,
        });

        env.disableFeature(featureConfidentialTransfer);
        env.close();

        mptAlice.mergeInbox({
            .account = bob,
            .err = temDISABLED,
        });
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

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.destroy();
            mptAlice.generateKeyPair(bob);

            mptAlice.mergeInbox({
                .account = bob,
                .err = tecOBJECT_NOT_FOUND,
            });
        }

        // tfMPTCanConfidentialAmount is not set on issuance
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.mergeInbox({
                .account = bob,
                .err = tecNO_PERMISSION,
            });
        }

        // no mptoken
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.mergeInbox({
                .account = bob,
                .err = tecOBJECT_NOT_FOUND,
            });
        }

        // bob doesn't have encrypted balances
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            mptAlice.mergeInbox({
                .account = bob,
                .err = tecNO_PERMISSION,
            });
        }

        // holder is locked
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.convert({
                .account = bob,
                .amt = 50,
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            // lock bob
            mptAlice.set({
                .account = alice,
                .holder = bob,
                .flags = tfMPTLock,
            });

            mptAlice.mergeInbox({
                .account = bob,
                .err = tecLOCKED,
            });

            // unlock bob
            mptAlice.set({
                .account = alice,
                .holder = bob,
                .flags = tfMPTUnlock,
            });

            // should succeed now
            mptAlice.mergeInbox({
                .account = bob,
            });
        }

        // holder not authorized
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags =
                    tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount | tfMPTRequireAuth,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.authorize({
                .account = alice,
                .holder = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.convert({
                .account = bob,
                .amt = 50,
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            // unauthorize bob
            mptAlice.authorize({
                .account = alice,
                .holder = bob,
                .flags = tfMPTUnauthorize,
            });

            mptAlice.mergeInbox({
                .account = bob,
                .err = tecNO_AUTH,
            });

            // authorize bob again
            mptAlice.authorize({
                .account = alice,
                .holder = bob,
            });

            // should succeed now
            mptAlice.mergeInbox({
                .account = bob,
            });
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

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
        });

        mptAlice.authorize({
            .account = bob,
        });
        mptAlice.authorize({
            .account = carol,
        });

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

        // carol sends 15 back to bob
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
            env,
            alice,
            {
                .holders = {bob, carol},
                .auditor = auditor,
            });

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
        });

        mptAlice.authorize({
            .account = bob,
        });
        mptAlice.authorize({
            .account = carol,
        });

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

        // carol sends 15 back to bob
        mptAlice.send({
            .account = carol,
            .dest = bob,
            .amt = 15,
        });
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
            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.authorize({
                .account = carol,
            });

            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .senderEncryptedAmt = makeZeroBuffer(ecGamalEncryptedTotalLength),
                .destEncryptedAmt = makeZeroBuffer(ecGamalEncryptedTotalLength),
                .issuerEncryptedAmt = makeZeroBuffer(ecGamalEncryptedTotalLength),
                .err = temDISABLED,
            });
        }

        // test malformed
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.authorize({
                .account = carol,
            });
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
            mptAlice.send({
                .account = alice,
                .dest = carol,
                .amt = 10,
                .err = temMALFORMED,
            });

            // can not send to self
            mptAlice.send({
                .account = bob,
                .dest = bob,
                .amt = 10,
                .err = temMALFORMED,
            });

            // sender encrypted amount wrong length
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .senderEncryptedAmt = makeZeroBuffer(10),
                .err = temBAD_CIPHERTEXT,
            });

            // dest encrypted amount wrong length
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .destEncryptedAmt = makeZeroBuffer(10),
                .err = temBAD_CIPHERTEXT,
            });

            // issuer encrypted amount wrong length
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .issuerEncryptedAmt = makeZeroBuffer(10),
                .err = temBAD_CIPHERTEXT,
            });

            // sender encrypted amount malformed
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(3),
                .senderEncryptedAmt = makeZeroBuffer(ecGamalEncryptedTotalLength),
                .amountCommitment = getTrivialCommitment(),
                .balanceCommitment = getTrivialCommitment(),
                .err = temBAD_CIPHERTEXT,
            });

            // dest encrypted amount malformed
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(3),
                .destEncryptedAmt = makeZeroBuffer(ecGamalEncryptedTotalLength),
                .amountCommitment = getTrivialCommitment(),
                .balanceCommitment = getTrivialCommitment(),
                .err = temBAD_CIPHERTEXT,
            });

            // issuer encrypted amount malformed
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(3),
                .issuerEncryptedAmt = makeZeroBuffer(ecGamalEncryptedTotalLength),
                .amountCommitment = getTrivialCommitment(),
                .balanceCommitment = getTrivialCommitment(),
                .err = temBAD_CIPHERTEXT,
            });

            // invalid proof length
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = std::string(10, 'A'),
                .amountCommitment = getTrivialCommitment(),
                .balanceCommitment = getTrivialCommitment(),
                .err = temMALFORMED,
            });

            // invalid amount Pedersen commitment length
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(3),
                .amountCommitment = makeZeroBuffer(100),
                .balanceCommitment = getTrivialCommitment(),
                .err = temMALFORMED,
            });

            // invalid balance Pedersen commitment length
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(3),
                .amountCommitment = getTrivialCommitment(),
                .balanceCommitment = makeZeroBuffer(100),
                .err = temMALFORMED,
            });

            // amount Pedersen commitment has correct length but invalid EC point data
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(3),
                .amountCommitment = makeZeroBuffer(ecPedersenCommitmentLength),
                .balanceCommitment = getTrivialCommitment(),
                .err = temMALFORMED,
            });

            // balance Pedersen commitment has correct length but invalid EC point data
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(3),
                .amountCommitment = getTrivialCommitment(),
                .balanceCommitment = makeZeroBuffer(ecPedersenCommitmentLength),
                .err = temMALFORMED,
            });
        }

        // test bad ciphertext
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const auditor("auditor");
            MPTTester mptAlice(
                env,
                alice,
                {
                    .holders = {bob, carol},
                    .auditor = auditor,
                });

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.authorize({
                .account = carol,
            });
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
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(4),
                .auditorEncryptedAmt = makeZeroBuffer(10),
                .amountCommitment = getTrivialCommitment(),
                .balanceCommitment = getTrivialCommitment(),
                .err = temBAD_CIPHERTEXT,
            });

            // auditor encrypted amount (correct length, invalid data)
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(4),
                .auditorEncryptedAmt = getBadCiphertext(),
                .amountCommitment = getTrivialCommitment(),
                .balanceCommitment = getTrivialCommitment(),
                .err = temBAD_CIPHERTEXT,
            });
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
        mptAlice.create({
            .flags =
                tfMPTCanTransfer | tfMPTCanLock | tfMPTRequireAuth | tfMPTCanConfidentialAmount,
        });
        mptAlice.authorize({
            .account = bob,
        });
        mptAlice.authorize({
            .account = alice,
            .holder = bob,
        });
        mptAlice.authorize({
            .account = carol,
        });
        mptAlice.authorize({
            .account = alice,
            .holder = carol,
        });
        mptAlice.authorize({
            .account = dave,
        });
        mptAlice.authorize({
            .account = alice,
            .holder = dave,
        });

        // fund bob, carol (not dave or eve)
        mptAlice.pay(alice, bob, 100);
        mptAlice.pay(alice, carol, 50);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);
        mptAlice.generateKeyPair(dave);
        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        // bob and carol convert some funds to confidential
        mptAlice.convert({
            .account = bob,
            .amt = 60,
            .holderPubKey = mptAlice.getPubKey(bob),
            .err = tesSUCCESS,
        });
        mptAlice.convert({
            .account = carol,
            .amt = 20,
            .holderPubKey = mptAlice.getPubKey(carol),
            .err = tesSUCCESS,
        });

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

            mptAlice.create({
                .flags = tfMPTCanTransfer | tfMPTCanConfidentialAmount,
            });
            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.authorize({
                .account = carol,
            });
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
            mptAlice.send({
                .account = bob,
                .dest = unknown,
                .amt = 10,
                .proof = getTrivialSendProofHex(3),
                .senderEncryptedAmt = getTrivialCiphertext(),
                .destEncryptedAmt = getTrivialCiphertext(),
                .issuerEncryptedAmt = getTrivialCiphertext(),
                .amountCommitment = getTrivialCommitment(),
                .balanceCommitment = getTrivialCommitment(),
                .err = tecNO_TARGET,
            });
        }

        // dave exists, but has no confidential fields (never converted)
        {
            mptAlice.send({
                .account = bob,
                .dest = dave,
                .amt = 10,
                .proof = getTrivialSendProofHex(3),
                .senderEncryptedAmt = getTrivialCiphertext(),
                .destEncryptedAmt = getTrivialCiphertext(),
                .issuerEncryptedAmt = getTrivialCiphertext(),
                .amountCommitment = getTrivialCommitment(),
                .balanceCommitment = getTrivialCommitment(),
                .err = tecNO_PERMISSION,
            });
            mptAlice.send({
                .account = dave,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(3),
                .senderEncryptedAmt = getTrivialCiphertext(),
                .destEncryptedAmt = getTrivialCiphertext(),
                .issuerEncryptedAmt = getTrivialCiphertext(),
                .amountCommitment = getTrivialCommitment(),
                .balanceCommitment = getTrivialCommitment(),
                .err = tecNO_PERMISSION,
            });
        }

        // destination exists but has no MPT object.
        {
            mptAlice.send({
                .account = bob,
                .dest = eve,
                .amt = 10,
                .proof = getTrivialSendProofHex(3),
                .senderEncryptedAmt = getTrivialCiphertext(),
                .destEncryptedAmt = getTrivialCiphertext(),
                .issuerEncryptedAmt = getTrivialCiphertext(),
                .amountCommitment = getTrivialCommitment(),
                .balanceCommitment = getTrivialCommitment(),
                .err = tecOBJECT_NOT_FOUND,
            });
        }

        // issuance is locked globally
        {
            // lock issuance
            mptAlice.set({
                .account = alice,
                .flags = tfMPTLock,
            });
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .err = tecLOCKED,
            });
            // unlock issuance
            mptAlice.set({
                .account = alice,
                .flags = tfMPTUnlock,
            });
            // now can send
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 1,
            });
        }

        // sender is locked
        {
            // lock bob
            mptAlice.set({
                .account = alice,
                .holder = bob,
                .flags = tfMPTLock,
            });
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .err = tecLOCKED,
            });
            // unlock bob
            mptAlice.set({
                .account = alice,
                .holder = bob,
                .flags = tfMPTUnlock,
            });
            // now can send
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 2,
            });
        }

        // destination is locked
        {
            // lock carol
            mptAlice.set({
                .account = alice,
                .holder = carol,
                .flags = tfMPTLock,
            });
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .err = tecLOCKED,
            });
            // unlock carol
            mptAlice.set({
                .account = alice,
                .holder = carol,
                .flags = tfMPTUnlock,
            });
            // now can send
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 3,
            });
        }

        // sender not authorized
        {
            // unauthorize bob
            mptAlice.authorize({
                .account = alice,
                .holder = bob,
                .flags = tfMPTUnauthorize,
            });
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .err = tecNO_AUTH,
            });
            // authorize bob again
            mptAlice.authorize({
                .account = alice,
                .holder = bob,
            });
            // now can send
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 4,
            });
        }

        // destination not authorized
        {
            // unauthorize carol
            mptAlice.authorize({
                .account = alice,
                .holder = carol,
                .flags = tfMPTUnauthorize,
            });
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .err = tecNO_AUTH,
            });
            // authorize carol again
            mptAlice.authorize({
                .account = alice,
                .holder = carol,
            });
            // now can send
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 5,
            });
        }

        // cannot send when MPTCanTransfer is not set
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.authorize({
                .account = carol,
            });

            mptAlice.pay(alice, bob, 100);
            mptAlice.pay(alice, carol, 50);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(carol);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            // Convert 60 out of 100
            mptAlice.convert({
                .account = bob,
                .amt = 60,
                .holderPubKey = mptAlice.getPubKey(bob),
                .err = tesSUCCESS,
            });

            // bob merge inbox
            mptAlice.mergeInbox({
                .account = bob,
            });

            mptAlice.convert({
                .account = carol,
                .amt = 20,
                .holderPubKey = mptAlice.getPubKey(carol),
                .err = tesSUCCESS,
            });

            // carol merge inbox
            mptAlice.mergeInbox({
                .account = carol,
            });

            // bob sends 10 to carol
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,  // will be encrypted internally
                .err = tecNO_AUTH,
            });
        }

        // bad proof
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanLock | tfMPTCanConfidentialAmount | tfMPTCanTransfer,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.authorize({
                .account = carol,
            });

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
                .err = tesSUCCESS,
            });

            mptAlice.mergeInbox({
                .account = bob,
            });

            mptAlice.convert({
                .account = carol,
                .amt = 20,
                .holderPubKey = mptAlice.getPubKey(carol),
                .err = tesSUCCESS,
            });

            mptAlice.mergeInbox({
                .account = carol,
            });

            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(3),
                .err = tecBAD_PROOF,
            });
        }

        // No Auditor key set, but auditor encrypted amt provided
        {
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(4),
                .auditorEncryptedAmt = getTrivialCiphertext(),
                .err = tecNO_PERMISSION,
            });
        }

        // Auditor CipherText is Valid, but does not match the Txn Amount
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const auditor("auditor");
            MPTTester mptAlice(
                env,
                alice,
                {
                    .holders = {bob, carol},
                    .auditor = auditor,
                });

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.authorize({
                .account = carol,
            });
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

            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(4),
                .auditorEncryptedAmt = getTrivialCiphertext(),
                .amountCommitment = getTrivialCommitment(),
                .balanceCommitment = getTrivialCommitment(),
                .err = tecBAD_PROOF,
            });
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

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanLock | tfMPTCanConfidentialAmount | tfMPTCanTransfer,
        });
        mptAlice.authorize({
            .account = bob,
        });
        mptAlice.authorize({
            .account = carol,
        });

        mptAlice.pay(alice, bob, 1000);
        mptAlice.pay(alice, carol, 1000);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);

        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        {
            // Bob converts 60
            mptAlice.convert({.account = bob, .amt = 60, .holderPubKey = mptAlice.getPubKey(bob)});
            mptAlice.mergeInbox({
                .account = bob,
            });

            mptAlice.convert(
                {.account = carol, .amt = 50, .holderPubKey = mptAlice.getPubKey(carol)});
            mptAlice.mergeInbox({
                .account = carol,
            });

            // Bob has 60, tries to send 70. Invalid remaining balance.
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 70,
                .err = tecBAD_PROOF,
            });

            // Bob has 60, tries to send 61. Invalid remaining balance.
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 61,
                .err = tecBAD_PROOF,
            });

            // Bob has 60, sends 60. Remainder is exactly 0. Valid remaining balance.
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 60,
                .err = tesSUCCESS,
            });
        }

        {
            // Bob converts 100.
            mptAlice.convert({
                .account = bob,
                .amt = 100,
            });
            mptAlice.mergeInbox({
                .account = bob,
            });

            // Bob has 100, tries to send 2^64-1. Invalid remaining balance.
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 0xFFFFFFFFFFFFFFFF,  // Max uint64
                .err = tecBAD_PROOF,
            });

            // Bob sends 1, remaining 99.
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 1,
                .err = tesSUCCESS,
            });

            // Bob sends 100, but only has 99. Invalid remaining balance.
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 100,
                .err = tecBAD_PROOF,
            });
        }

        // send when spending balance is 0 (key registered, inbox merged, but nothing converted)
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });
            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});
            mptAlice.pay(alice, bob, 100);
            mptAlice.pay(alice, carol, 50);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(carol);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            // Register keys only (amt=0) for both parties, then merge — spending stays 0.
            mptAlice.convert({.account = bob, .amt = 0, .holderPubKey = mptAlice.getPubKey(bob)});
            mptAlice.mergeInbox({.account = bob});
            mptAlice.convert(
                {.account = carol, .amt = 0, .holderPubKey = mptAlice.getPubKey(carol)});

            BEAST_EXPECT(
                mptAlice.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 0);

            // Trying to send any amount with 0 spending balance must fail:
            // the range proof for < 0 is invalid.
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 1,
                .err = tecBAD_PROOF,
            });

            BEAST_EXPECT(
                mptAlice.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 0);
        }

        // todo: test m exceeding range, require using scala and refactor
    }

    /* TODO: uncomment when MPT crypto supports proof generation with value 0
     * Tests verifier behavior when the send amount is 0.
     *
     * The equality proof library and range proof library do not
     * support generating proofs for amt=0 (they require a positive witness).
     * To test the VERIFIER without crashing the helper, we bypass normal proof
     * generation by supplying explicit ciphertexts, commitments, and a dummy
     * (all-zero) proof.  The preflight has no temBAD_AMOUNT guard for
     * ConfidentialMPTSend, so all validation occurs in verifySendProofs.
     */
    /*void
    testSendZeroAmount(FeatureBitset features)
    {
        testcase("Send: zero amount — equality and range proof verifier behavior");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
        });
        mptAlice.authorize({.account = bob});
        mptAlice.authorize({.account = carol});
        mptAlice.pay(alice, bob, 100);
        mptAlice.pay(alice, carol, 50);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);

        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        mptAlice.convert({.account = bob, .amt = 100, .holderPubKey = mptAlice.getPubKey(bob)});
        mptAlice.mergeInbox({.account = bob});

        mptAlice.convert({.account = carol, .amt = 50, .holderPubKey = mptAlice.getPubKey(carol)});
        mptAlice.mergeInbox({.account = carol});

        Buffer const bf = generateBlindingFactor();

        // equality proof verification for amt=0.
        // Encrypt 0 under each participant's key.  The amount commitment is
        // getTrivialCommitment() — a valid EC point that passes preflight's
        // isValidCompressedECPoint check but is not the true PC for amt=0.
        // The dummy ZKProof's equality component must be rejected by
        // verifyMultiCiphertextEqualityProof.
        mptAlice.send({
            .account = bob,
            .dest = carol,
            .amt = 0,
            .proof = getTrivialSendProofHex(3),
            .senderEncryptedAmt = mptAlice.encryptAmount(bob, 0, bf),
            .destEncryptedAmt = mptAlice.encryptAmount(carol, 0, bf),
            .issuerEncryptedAmt = mptAlice.encryptAmount(alice, 0, bf),
            .amountCommitment = getTrivialCommitment(),
            .balanceCommitment = getTrivialCommitment(),
            .err = tecBAD_PROOF,
        });

        // range proof verification for amt=0.
        // Identical construction; focuses on the bulletproof range check
        // embedded in ZKProof.  The range proof for amount=0 with a dummy
        // (all-zero) proof must also be rejected.
        Buffer const bf2 = generateBlindingFactor();
        mptAlice.send({
            .account = bob,
            .dest = carol,
            .amt = 0,
            .proof = getTrivialSendProofHex(3),
            .senderEncryptedAmt = mptAlice.encryptAmount(bob, 0, bf2),
            .destEncryptedAmt = mptAlice.encryptAmount(carol, 0, bf2),
            .issuerEncryptedAmt = mptAlice.encryptAmount(alice, 0, bf2),
            .amountCommitment = getTrivialCommitment(),
            .balanceCommitment = getTrivialCommitment(),
            .err = tecBAD_PROOF,
        });

        // All rejected sends must leave balances unchanged.
        BEAST_EXPECT(
            mptAlice.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 100);
        BEAST_EXPECT(
            mptAlice.getDecryptedBalance(carol, MPTTester::HOLDER_ENCRYPTED_INBOX) == 0);
    }*/

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

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            mptAlice.convert({
                .account = bob,
                .amt = 100,
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            mptAlice.authorize({
                .account = bob,
                .flags = tfMPTUnauthorize,
                .err = tecHAS_OBLIGATIONS,
            });
        }

        // cannot delete mptoken where it has encrypted balance
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.authorize({
                .account = carol,
            });
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
            mptAlice.authorize({
                .account = carol,
                .flags = tfMPTUnauthorize,
                .err = tecHAS_OBLIGATIONS,
            });
        }

        // can delete mptoken if outstanding confidential balance is zero
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
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

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
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
        // test with convert back and delete
        // can delete mptoken if converted back (COA returns to zero)
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});
            mptAlice.generateKeyPair(bob);

            mptAlice.convert({
                .account = bob,
                .amt = 100,
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            mptAlice.mergeInbox({
                .account = bob,
            });

            mptAlice.convertBack({
                .account = bob,
                .amt = 100,
            });

            mptAlice.pay(bob, alice, 100);

            // Should be able to delete as Confidential Outstanding amount is 0
            mptAlice.authorize({
                .account = bob,
                .flags = tfMPTUnauthorize,
            });
        }

        // removeEmptyHolding: vault share MPToken with confidential balance
        // fields should not be deleted on VaultWithdraw
        {
            Env env{*this, features | featureSingleAssetVault};
            Account const issuer("issuer");
            Account const owner("owner");
            Account const depositor("depositor");

            MPTTester mptt{env, issuer, {.holders = {owner, depositor}}};
            mptt.create({
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanClawback,
            });
            PrettyAsset asset = mptt.issuanceID();
            mptt.authorize({.account = owner});
            mptt.authorize({.account = depositor});
            env(pay(issuer, depositor, asset(1000)));
            env.close();

            test::jtx::Vault vault{env};
            auto [tx, vaultKeylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            // Get the share MPTID from vault
            auto const vaultSle = env.le(vaultKeylet);
            BEAST_EXPECT(vaultSle != nullptr);
            MPTID share = vaultSle->at(sfShareMPTID);

            // Depositor deposits into vault
            tx = vault.deposit(
                {.depositor = depositor, .id = vaultKeylet.key, .amount = asset(100)});
            env(tx);
            env.close();

            // Verify depositor has share tokens
            auto shareMpt = env.le(keylet::mptoken(share, depositor.id()));
            BEAST_EXPECT(shareMpt != nullptr);

            // Inject confidential balance fields on the share MPToken
            // to simulate a scenario where vault shares somehow have
            // confidential balances
            env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) {
                // Set lsfMPTCanConfidentialAmount on the share issuance
                // so the invariant allows encrypted fields on the MPToken
                auto issuance = std::const_pointer_cast<SLE>(view.read(keylet::mptIssuance(share)));
                if (!issuance)
                    return false;
                issuance->setFlag(lsfMPTCanConfidentialAmount);
                view.rawReplace(issuance);

                auto const k = keylet::mptoken(share, depositor.id());
                auto sle = std::const_pointer_cast<SLE>(view.read(k));
                if (!sle)
                    return false;
                // Inject dummy confidential balance fields
                Buffer dummyCiphertext(ecGamalEncryptedTotalLength);
                std::memset(dummyCiphertext.data(), 0, ecGamalEncryptedTotalLength);
                dummyCiphertext.data()[0] = ecCompressedPrefixEvenY;
                dummyCiphertext.data()[ecGamalEncryptedLength] = ecCompressedPrefixEvenY;
                dummyCiphertext.data()[ecGamalEncryptedLength - 1] = 0x01;
                dummyCiphertext.data()[ecGamalEncryptedTotalLength - 1] = 0x01;
                sle->setFieldVL(sfConfidentialBalanceSpending, dummyCiphertext);
                sle->setFieldVL(sfConfidentialBalanceInbox, dummyCiphertext);
                sle->setFieldVL(sfIssuerEncryptedBalance, dummyCiphertext);
                view.rawReplace(sle);
                return true;
            });

            // Withdraw everything - which should fail because of the confidential balance fields
            tx = vault.withdraw(
                {.depositor = depositor, .id = vaultKeylet.key, .amount = asset(100)});
            env(tx);

            // The share MPToken should still exist because the
            // withdrawal failed due to confidential balance obligations
            shareMpt = env.le(keylet::mptoken(share, depositor.id()));
            BEAST_EXPECT(shareMpt != nullptr);
        }
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

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
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

            mptAlice.convertBack({
                .account = bob,
                .amt = 10,
            });
        }

        // Edge case: minimum amount (1)
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
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
        // TODO: improve this test once there is bounded decryption or optimized decryption for
        // large amounts
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, maxMPTokenAmount);

            mptAlice.generateKeyPair(alice);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            // Convert maxMPTokenAmount to confidential using raw JSON
            Buffer const convertBlindingFactor = generateBlindingFactor();
            auto const convertHolderCiphertext =
                mptAlice.encryptAmount(bob, maxMPTokenAmount, convertBlindingFactor);
            auto const convertIssuerCiphertext =
                mptAlice.encryptAmount(alice, maxMPTokenAmount, convertBlindingFactor);
            auto const convertContextHash =
                getConvertContextHash(bob.id(), mptAlice.issuanceID(), env.seq(bob));
            auto const schnorrProof = mptAlice.getSchnorrProof(bob, convertContextHash);
            BEAST_EXPECT(schnorrProof.has_value());

            {
                Json::Value jv;
                jv[jss::Account] = bob.human();
                jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
                jv[sfMPTokenIssuanceID] = to_string(mptAlice.issuanceID());
                jv[sfMPTAmount.jsonName] = std::to_string(maxMPTokenAmount);
                jv[sfHolderEncryptionKey.jsonName] = strHex(*mptAlice.getPubKey(bob));
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
            Buffer const pedersenCommitment =
                mptAlice.getPedersenCommitment(maxMPTokenAmount, pcBlindingFactor);

            // Generate the proof using known spending balance value
            auto const version = mptAlice.getMPTokenVersion(bob);
            uint256 const convertBackContextHash =
                getConvertBackContextHash(bob.id(), mptAlice.issuanceID(), env.seq(bob), version);

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
        MPTTester mptAlice(
            env,
            alice,
            {
                .holders = {bob},
                .auditor = auditor,
            });

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
        });

        mptAlice.authorize({
            .account = bob,
        });
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

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.convertBack({
                .account = bob,
                .amt = 30,
                .err = temDISABLED,
            });
        }

        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
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
                .account = alice,
                .amt = 30,
                .err = temMALFORMED,
            });

            mptAlice.convertBack({
                .account = bob,
                .amt = 0,
                .err = temBAD_AMOUNT,
            });

            mptAlice.convertBack({
                .account = bob,
                .amt = maxMPTokenAmount + 1,
                .err = temBAD_AMOUNT,
            });

            // Balance commitment has correct length but invalid EC point data
            mptAlice.convertBack({
                .account = bob,
                .amt = 30,
                .pedersenCommitment = makeZeroBuffer(ecPedersenCommitmentLength),
                .err = temMALFORMED,
            });

            mptAlice.convertBack({
                .account = bob,
                .amt = 30,
                .holderEncryptedAmt = Buffer{},
                .err = temBAD_CIPHERTEXT,
            });

            mptAlice.convertBack({
                .account = bob,
                .amt = 30,
                .issuerEncryptedAmt = Buffer{},
                .err = temBAD_CIPHERTEXT,
            });

            mptAlice.convertBack({
                .account = bob,
                .amt = 30,
                .holderEncryptedAmt = getBadCiphertext(),
                .err = temBAD_CIPHERTEXT,
            });

            mptAlice.convertBack({
                .account = bob,
                .amt = 30,
                .issuerEncryptedAmt = getBadCiphertext(),
                .err = temBAD_CIPHERTEXT,
            });

            mptAlice.convertBack({
                .account = bob,
                .amt = 30,
                .auditorEncryptedAmt = makeZeroBuffer(10),
                .err = temBAD_CIPHERTEXT,
            });

            mptAlice.convertBack({
                .account = bob,
                .amt = 30,
                .auditorEncryptedAmt = getBadCiphertext(),
                .err = temBAD_CIPHERTEXT,
            });

            // invalid proof length
            mptAlice.convertBack({
                .account = bob,
                .amt = 30,
                .proof = Buffer{},
                .err = temMALFORMED,
            });

            mptAlice.convertBack({
                .account = bob,
                .amt = 30,
                .proof = makeZeroBuffer(100),
                .err = temMALFORMED,
            });
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

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.destroy();
            mptAlice.generateKeyPair(bob);

            mptAlice.convertBack({
                .account = bob,
                .amt = 30,
                .err = tecOBJECT_NOT_FOUND,
            });
        }

        // tfMPTCanConfidentialAmount is not set on issuance
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.convertBack({
                .account = bob,
                .amt = 30,
                .err = tecNO_PERMISSION,
            });
        }

        // no mptoken
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.convertBack({
                .account = bob,
                .amt = 30,
                .err = tecOBJECT_NOT_FOUND,
            });
        }

        // bob doesn't have encrypted balances
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            mptAlice.convertBack({
                .account = bob,
                .amt = 30,
                .err = tecNO_PERMISSION,
            });
        }

        // bob tries to convert back more than COA
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.authorize({
                .account = carol,
            });
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

            mptAlice.convertBack({
                .account = bob,
                .amt = 300,
                .err = tecINSUFFICIENT_FUNDS,
            });
        }

        // cannot convert if locked or unauth
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags =
                    tfMPTCanTransfer | tfMPTCanLock | tfMPTRequireAuth | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.authorize({
                .account = alice,
                .holder = bob,
            });
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

            mptAlice.set({
                .account = alice,
                .holder = bob,
                .flags = tfMPTLock,
            });

            mptAlice.convertBack({
                .account = bob,
                .amt = 10,
                .err = tecLOCKED,
            });

            mptAlice.set({
                .account = alice,
                .holder = bob,
                .flags = tfMPTUnlock,
            });

            mptAlice.convertBack({
                .account = bob,
                .amt = 10,
            });

            mptAlice.authorize({
                .account = alice,
                .holder = bob,
                .flags = tfMPTUnauthorize,
            });

            mptAlice.convertBack({
                .account = bob,
                .amt = 10,
                .err = tecNO_AUTH,
            });

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

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });
            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.convert({.account = bob, .amt = 50, .holderPubKey = mptAlice.getPubKey(bob)});
            mptAlice.mergeInbox({
                .account = bob,
            });

            // Holder encrypted amount is valid format but mathematically incorrect for this
            // convertBack
            mptAlice.convertBack({
                .account = bob,
                .amt = 10,
                .holderEncryptedAmt = getTrivialCiphertext(),
                .err = tecBAD_PROOF,
            });

            // Issuer encrypted amount is valid format but mathematically incorrect for this
            // convertBack
            mptAlice.convertBack({
                .account = bob,
                .amt = 10,
                .issuerEncryptedAmt = getTrivialCiphertext(),
                .err = tecBAD_PROOF,
            });
        }

        // Alice has NOT set an auditor key, but Bob provides
        // auditorEncryptedAmt
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });
            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            // Bob converts funds to confidential so he has something to convert
            // back
            mptAlice.convert({.account = bob, .amt = 50, .holderPubKey = mptAlice.getPubKey(bob)});
            mptAlice.mergeInbox({
                .account = bob,
            });

            mptAlice.convertBack({
                .account = bob,
                .amt = 10,
                // Provide valid ciphertext to pass preflight
                .auditorEncryptedAmt = getTrivialCiphertext(),
                .err = tecNO_PERMISSION,
            });
        }

        // we set the auditor key, but convertBack omits auditorEncryptedAmt
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const auditor("auditor");
            MPTTester mptAlice(
                env,
                alice,
                {
                    .holders = {bob},
                    .auditor = auditor,
                });

            mptAlice.create({
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });
            mptAlice.authorize({
                .account = bob,
            });
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
            mptAlice.mergeInbox({
                .account = bob,
            });

            // ConvertBack WITHOUT auditorEncryptedAmt
            mptAlice.convertBack({
                .account = bob,
                .amt = 10,
                .fillAuditorEncryptedAmt = false,
                .err = tecNO_PERMISSION,
            });

            // ConvertBack where auditor ciphertext mathematically
            // correct, but contains invalid data (mismatching amount).
            mptAlice.convertBack({
                .account = bob,
                .amt = 10,
                .auditorEncryptedAmt = getTrivialCiphertext(),
                .err = tecBAD_PROOF,
            });
        }
    }

    void
    testSendDepositPreauth(FeatureBitset features)
    {
        testcase("Send deposit preauth");
        using namespace test::jtx;

        // When an account enables lsfDepositAuth (via asfDepositAuth flag),
        // it requires explicit authorization before accepting incoming payments.
        //
        // There are two authorization mechanisms:
        //
        // 1. DIRECT ACCOUNT AUTHORIZATION (deposit::auth)
        //    - Bob directly authorizes Carol: deposit::auth(bob, carol)
        //    - Simple 1-to-1 trust relationship
        //    - Carol can send to Bob without credentials
        //
        // 2. CREDENTIAL-BASED AUTHORIZATION (deposit::authCredentials)
        //    - A trusted third party (dpIssuer) issues credentials
        //    - Bob authorizes a credential TYPE from an issuer
        //    - Anyone holding that credential can send to Bob
        //    - Requires sender to include credential ID in transaction

        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const dpIssuer("dpIssuer");
        char const credType[] = "KYC_VERIFIED";

        // Common setup: create MPT with privacy, convert both carol and bob
        auto setupMPT = [&](Env& env, MPTTester& mpt) {
            mpt.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });
            mpt.authorize({
                .account = bob,
            });
            mpt.authorize({
                .account = carol,
            });
            mpt.pay(alice, bob, 100);
            mpt.pay(alice, carol, 100);

            mpt.generateKeyPair(alice);
            mpt.generateKeyPair(bob);
            mpt.generateKeyPair(carol);
            mpt.set({.account = alice, .issuerPubKey = mpt.getPubKey(alice)});

            mpt.convert({.account = carol, .amt = 50, .holderPubKey = mpt.getPubKey(carol)});
            mpt.convert({.account = bob, .amt = 50, .holderPubKey = mpt.getPubKey(bob)});
            mpt.mergeInbox({
                .account = carol,
            });
            mpt.mergeInbox({
                .account = bob,
            });

            env(fset(bob, asfDepositAuth));
            env.close();
        };

        // Create and accept credential for an account
        auto createCredential = [&](Env& env, Account const& subject) -> std::string {
            env(credentials::create(subject, dpIssuer, credType));
            env.close();
            env(credentials::accept(subject, dpIssuer, credType));
            env.close();
            auto const jv = credentials::ledgerEntry(env, subject, dpIssuer, credType);
            return jv[jss::result][jss::index].asString();
        };

        // TEST 1: Direct Account Authorization
        {
            Env env(*this, features);
            MPTTester mpt(env, alice, {.holders = {bob, carol}});
            setupMPT(env, mpt);

            // Carol cannot send to Bob without authorization
            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .err = tecNO_PERMISSION,
            });

            // Bob directly authorizes Carol
            env(deposit::auth(bob, carol));
            env.close();

            // Now Carol can send to Bob
            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
            });
            mpt.mergeInbox({
                .account = bob,
            });

            // Bob revokes Carol's authorization
            env(deposit::unauth(bob, carol));
            env.close();

            // Carol can no longer send to Bob
            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .err = tecNO_PERMISSION,
            });
        }

        // TEST 2: Credential-Based Authorization
        {
            Env env(*this, features);
            env.fund(XRP(50000), dpIssuer);
            env.close();

            MPTTester mpt(env, alice, {.holders = {bob, carol}});
            setupMPT(env, mpt);

            auto const credIdx = createCredential(env, carol);

            // Carol cannot send yet - Bob hasn't authorized this credential type
            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .credentials = {{credIdx}},
                .err = tecNO_PERMISSION,
            });

            // Bob authorizes the credential type from dpIssuer
            env(deposit::authCredentials(bob, {{dpIssuer, credType}}));
            env.close();

            // Carol still cannot send without including credential
            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .err = tecNO_PERMISSION,
            });

            // Carol CAN send when including her credential
            mpt.send({.account = carol, .dest = bob, .amt = 10, .credentials = {{credIdx}}});
            mpt.mergeInbox({
                .account = bob,
            });
        }

        // TEST 3: Direct Auth Takes Precedence Over Credentials
        {
            Env env(*this, features);
            env.fund(XRP(50000), dpIssuer);
            env.close();

            MPTTester mpt(env, alice, {.holders = {bob, carol}});
            setupMPT(env, mpt);

            auto const credIdx = createCredential(env, carol);

            // Bob directly authorizes Carol (no credential needed)
            env(deposit::auth(bob, carol));
            env.close();

            // Carol can send without credentials (direct auth)
            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
            });
            mpt.mergeInbox({
                .account = bob,
            });

            // Carol can also send WITH credentials (still works)
            mpt.send({.account = carol, .dest = bob, .amt = 10, .credentials = {{credIdx}}});
            mpt.mergeInbox({
                .account = bob,
            });

            // Bob revokes direct authorization
            env(deposit::unauth(bob, carol));
            env.close();

            // Carol cannot send without credentials anymore
            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .err = tecNO_PERMISSION,
            });

            // But credential-based auth not set up, so this also fails
            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .credentials = {{credIdx}},
                .err = tecNO_PERMISSION,
            });

            // Bob authorizes the credential type
            env(deposit::authCredentials(bob, {{dpIssuer, credType}}));
            env.close();

            // Now Carol can send with credentials
            mpt.send({.account = carol, .dest = bob, .amt = 10, .credentials = {{credIdx}}});
        }
    }

    void
    testSendCredentialValidation(FeatureBitset features)
    {
        testcase("Send credential validation");
        using namespace test::jtx;

        // Tests for credentials::checkFields (preflight) and
        // credentials::valid (preclaim) validation.
        //
        // Preflight checks (temMALFORMED):
        //   - Empty credentials array
        //   - Array size exceeds maxCredentialsArraySize (8)
        //   - Duplicate credential IDs in array
        //
        // Preclaim checks (tecBAD_CREDENTIALS):
        //   - Credential doesn't exist
        //   - Credential doesn't belong to source account
        //   - Credential not accepted (lsfAccepted flag not set)

        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const dpIssuer("dpIssuer");
        char const credType[] = "KYC";

        // Common setup: create MPT with privacy, convert carol and bob to confidential
        auto setupBasic = [&](Env& env, MPTTester& mpt) {
            mpt.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });
            mpt.authorize({
                .account = bob,
            });
            mpt.authorize({
                .account = carol,
            });
            mpt.pay(alice, bob, 100);
            mpt.pay(alice, carol, 100);

            mpt.generateKeyPair(alice);
            mpt.generateKeyPair(bob);
            mpt.generateKeyPair(carol);
            mpt.set({.account = alice, .issuerPubKey = mpt.getPubKey(alice)});

            mpt.convert({.account = carol, .amt = 50, .holderPubKey = mpt.getPubKey(carol)});
            mpt.convert({.account = bob, .amt = 50, .holderPubKey = mpt.getPubKey(bob)});
            mpt.mergeInbox({
                .account = carol,
            });
            mpt.mergeInbox({
                .account = bob,
            });
        };

        // TEST 1: Preflight - Empty Credentials Array
        {
            Env env(*this, features);
            MPTTester mpt(env, alice, {.holders = {bob, carol}});
            setupBasic(env, mpt);

            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .credentials = std::vector<std::string>{},
                .err = temMALFORMED,
            });
        }

        // TEST 2: Preflight - Credentials Array Too Large
        {
            Env env(*this, features);
            MPTTester mpt(env, alice, {.holders = {bob, carol}});
            setupBasic(env, mpt);

            std::vector<std::string> tooManyCredentials;
            for (int i = 0; i < 9; ++i)
                tooManyCredentials.push_back(to_string(uint256(i)));

            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .credentials = tooManyCredentials,
                .err = temMALFORMED,
            });
        }

        // TEST 3: Preflight - Duplicate Credentials
        {
            Env env(*this, features);
            env.fund(XRP(50000), dpIssuer);
            env.close();
            MPTTester mpt(env, alice, {.holders = {bob, carol}});
            setupBasic(env, mpt);

            env(credentials::create(carol, dpIssuer, credType));
            env.close();
            env(credentials::accept(carol, dpIssuer, credType));
            env.close();

            auto const jv = credentials::ledgerEntry(env, carol, dpIssuer, credType);
            std::string const credIdx = jv[jss::result][jss::index].asString();

            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .credentials = {{credIdx, credIdx}},
                .err = temMALFORMED,
            });
        }

        // TEST 4: Preclaim - Credential Doesn't Exist
        {
            Env env(*this, features);
            MPTTester mpt(env, alice, {.holders = {bob, carol}});
            setupBasic(env, mpt);

            std::string const fakeCredIdx = to_string(uint256(999));
            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .credentials = {{fakeCredIdx}},
                .err = tecBAD_CREDENTIALS,
            });
        }

        // TEST 5: Preclaim - Credential Doesn't Belong to Source Account
        {
            Env env(*this, features);
            env.fund(XRP(50000), dpIssuer);
            env.close();
            MPTTester mpt(env, alice, {.holders = {bob, carol}});
            setupBasic(env, mpt);

            // Create credential for BOB (not carol)
            env(credentials::create(bob, dpIssuer, credType));
            env.close();
            env(credentials::accept(bob, dpIssuer, credType));
            env.close();

            auto const jv = credentials::ledgerEntry(env, bob, dpIssuer, credType);
            std::string const credIdx = jv[jss::result][jss::index].asString();

            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .credentials = {{credIdx}},
                .err = tecBAD_CREDENTIALS,
            });
        }

        // TEST 6: Preclaim - Credential Not Accepted
        {
            Env env(*this, features);
            env.fund(XRP(50000), dpIssuer);
            env.close();
            MPTTester mpt(env, alice, {.holders = {bob, carol}});
            setupBasic(env, mpt);

            // Create credential but DON'T accept it
            env(credentials::create(carol, dpIssuer, credType));
            env.close();

            auto const jv = credentials::ledgerEntry(env, carol, dpIssuer, credType);
            std::string const credIdx = jv[jss::result][jss::index].asString();

            mpt.send({
                .account = carol,
                .dest = bob,
                .amt = 10,
                .credentials = {{credIdx}},
                .err = tecBAD_CREDENTIALS,
            });
        }
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

        mptAlice.create({
            .flags =
                tfMPTCanTransfer | tfMPTCanLock | tfMPTCanClawback | tfMPTCanConfidentialAmount,
        });
        mptAlice.authorize({
            .account = bob,
        });
        mptAlice.pay(alice, bob, 100);
        mptAlice.authorize({
            .account = carol,
        });
        mptAlice.pay(alice, carol, 200);
        mptAlice.authorize({
            .account = dave,
        });
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
            mptAlice.convert(
                {.account = carol, .amt = 120, .holderPubKey = mptAlice.getPubKey(carol)});

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
        mptAlice.send({
            .account = carol,
            .dest = bob,
            .amt = 50,
        });

        // alice clawback all confidential balance from bob, 110 in total.
        // bob has balance in both inbox and spending. These balances should
        // become zero after clawback, which is verified in the
        // confidentialClaw function.
        mptAlice.confidentialClaw({
            .account = alice,
            .holder = bob,
            .amt = 110,
        });

        // alice clawback all confidential balance from carol, which is 70.
        // carol only has balance in spending.
        mptAlice.confidentialClaw({
            .account = alice,
            .holder = carol,
            .amt = 70,
        });

        // alice clawback all confidential balance from dave, which is 200.
        // dave only has balance in inbox.
        mptAlice.confidentialClaw({
            .account = alice,
            .holder = dave,
            .amt = 200,
        });
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
        MPTTester mptAlice(
            env,
            alice,
            {
                .holders = {bob, carol, dave},
                .auditor = auditor,
            });

        mptAlice.create({
            .flags =
                tfMPTCanTransfer | tfMPTCanLock | tfMPTCanClawback | tfMPTCanConfidentialAmount,
        });
        mptAlice.authorize({
            .account = bob,
        });
        mptAlice.pay(alice, bob, 100);
        mptAlice.authorize({
            .account = carol,
        });
        mptAlice.pay(alice, carol, 200);
        mptAlice.authorize({
            .account = dave,
        });
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
            mptAlice.convert(
                {.account = carol, .amt = 120, .holderPubKey = mptAlice.getPubKey(carol)});

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
        mptAlice.send({
            .account = carol,
            .dest = bob,
            .amt = 50,
        });

        // alice clawback all confidential balance from bob, 110 in total.
        // bob has balance in both inbox and spending. These balances should
        // become zero after clawback, which is verified in the
        // confidentialClaw function.
        mptAlice.confidentialClaw({
            .account = alice,
            .holder = bob,
            .amt = 110,
        });

        // alice clawback all confidential balance from carol, which is 70.
        // carol only has balance in spending.
        mptAlice.confidentialClaw({
            .account = alice,
            .holder = carol,
            .amt = 70,
        });

        // alice clawback all confidential balance from dave, which is 200.
        // dave only has balance in inbox.
        mptAlice.confidentialClaw({
            .account = alice,
            .holder = dave,
            .amt = 200,
        });
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
            mptAlice.authorize({
                .account = bob,
            });

            mptAlice.confidentialClaw({
                .account = alice,
                .holder = bob,
                .amt = 10,
                .proof = "123",
                .err = temDISABLED,
            });
        }

        // test malformed
        {
            // set up
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.authorize({
                .account = carol,
            });
            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(carol);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});
            mptAlice.pay(alice, bob, 100);
            mptAlice.pay(alice, carol, 50);

            // only issuer can clawback
            mptAlice.confidentialClaw({
                .account = carol,
                .holder = bob,
                .amt = 10,
                .err = temMALFORMED,
            });

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
            mptAlice.confidentialClaw({
                .account = alice,
                .holder = alice,
                .amt = 10,
                .err = temMALFORMED,
            });

            // invalid amount
            mptAlice.confidentialClaw({
                .account = alice,
                .holder = bob,
                .amt = 0,
                .err = temBAD_AMOUNT,
            });

            // invalid proof length
            mptAlice.confidentialClaw({
                .account = alice,
                .holder = bob,
                .amt = 10,
                .proof = "123",
                .err = temMALFORMED,
            });
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

            mptAlice.create({
                .flags = tfMPTCanTransfer | tfMPTCanClawback | tfMPTRequireAuth |
                    tfMPTCanConfidentialAmount,
            });
            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.authorize({
                .account = alice,
                .holder = bob,
            });
            mptAlice.authorize({
                .account = carol,
            });
            mptAlice.authorize({
                .account = alice,
                .holder = carol,
            });

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
                mptAlice.confidentialClaw({
                    .account = alice,
                    .holder = unknown,
                    .amt = 10,
                    .err = tecNO_TARGET,
                });
            }

            // dave does not hold mpt at all, no MPT object
            {
                mptAlice.confidentialClaw({
                    .account = alice,
                    .holder = dave,
                    .amt = 10,
                    .err = tecOBJECT_NOT_FOUND,
                });
            }

            // carol has no confidential balance
            {
                mptAlice.confidentialClaw({
                    .account = alice,
                    .holder = carol,
                    .amt = 10,
                    .err = tecNO_PERMISSION,
                });
            }
        }

        // lsfMPTCanClawback not set
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .flags = tfMPTCanTransfer | tfMPTCanConfidentialAmount,
            });
            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.generateKeyPair(alice);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.confidentialClaw({
                .account = alice,
                .holder = bob,
                .amt = 10,
                .err = tecNO_PERMISSION,
            });
        }

        // no issuer key
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});
            mptAlice.create({
                .flags = tfMPTCanClawback | tfMPTCanConfidentialAmount,
            });
            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.generateKeyPair(alice);

            mptAlice.confidentialClaw({
                .account = alice,
                .holder = bob,
                .amt = 10,
                .err = tecNO_PERMISSION,
            });
        }

        // issuance not found
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});
            mptAlice.create({
                .flags = tfMPTCanClawback | tfMPTCanConfidentialAmount,
            });
            mptAlice.authorize({
                .account = bob,
            });
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

            mptAlice.create({
                .flags = tfMPTCanTransfer | tfMPTCanClawback | tfMPTRequireAuth | tfMPTCanLock |
                    tfMPTCanConfidentialAmount,
            });
            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.authorize({
                .account = alice,
                .holder = bob,
            });
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
            mptAlice.set({
                .account = alice,
                .holder = bob,
                .flags = tfMPTLock,
            });

            // clawback should still work
            mptAlice.confidentialClaw({
                .account = alice,
                .holder = bob,
                .amt = 60,
            });
        }

        // lock globally
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice = setupAccounts(env, alice, bob);
            mptAlice.set({
                .account = alice,
                .flags = tfMPTLock,
            });

            // clawback should still work
            mptAlice.confidentialClaw({
                .account = alice,
                .holder = bob,
                .amt = 60,
            });
        }

        // unauthorize should not block clawback
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice = setupAccounts(env, alice, bob);

            // unauthorize bob
            mptAlice.authorize({
                .account = alice,
                .holder = bob,
                .flags = tfMPTUnauthorize,
            });
            // clawback should still work
            mptAlice.confidentialClaw({
                .account = alice,
                .holder = bob,
                .amt = 60,
            });
        }

        // insufficient funds, clawback amount exceeding confidential
        // outstanding amount
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice = setupAccounts(env, alice, bob);

            mptAlice.confidentialClaw({
                .account = alice,
                .holder = bob,
                .amt = 10000,
                .err = tecINSUFFICIENT_FUNDS,
            });
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

            mptAlice.create({
                .flags = tfMPTCanTransfer | tfMPTCanClawback | tfMPTCanConfidentialAmount,
            });

            for (auto const& [acct, amt] : {std::pair{bob, 1000}, {carol, 2000}})
            {
                mptAlice.authorize({
                    .account = acct,
                });
                mptAlice.pay(alice, acct, amt);
                mptAlice.generateKeyPair(acct);
            }

            mptAlice.generateKeyPair(alice);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            return mptAlice;
        };

        // lambda function to test a set of bad clawback amounts that should
        // return tecBAD_PROOF
        auto checkBadProofs =
            [&](MPTTester& mpt, Account const& holder, std::initializer_list<uint64_t> amts) {
                for (auto const badAmt : amts)
                {
                    mpt.confidentialClaw({
                        .account = alice,
                        .holder = holder,
                        .amt = badAmt,
                        .err = tecBAD_PROOF,
                    });
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
            mptAlice.convert(
                {.account = carol, .amt = 1000, .holderPubKey = mptAlice.getPubKey(carol)});

            // verify proof fails with invalid clawback amount
            // bob: 500 in Spending, 0 in Inbox
            checkBadProofs(
                mptAlice,
                bob,
                {
                    1,
                    10,
                    70,
                    100,
                    110,
                    200,
                    499,
                    501,
                    600,
                });

            // carol: 1000 in Inbox, 0 in Spending
            checkBadProofs(
                mptAlice,
                carol,
                {
                    1,
                    10,
                    50,
                    500,
                    777,
                    850,
                    999,
                    1001,
                    1200,
                });

            // clawback with correct amount that passes proof verification
            mptAlice.confidentialClaw({
                .account = alice,
                .holder = bob,
                .amt = 500,
            });
            mptAlice.confidentialClaw({
                .account = alice,
                .holder = carol,
                .amt = 1000,
            });
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
            mptAlice.convert(
                {.account = carol, .amt = 400, .holderPubKey = mptAlice.getPubKey(carol)});
            mptAlice.mergeInbox({
                .account = carol,
            });
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 100,
            });
            mptAlice.send({
                .account = carol,
                .dest = bob,
                .amt = 100,
            });

            // verify proof fails with invalid clawback amount
            // bob: 100 in inbox, 200 in spending
            checkBadProofs(
                mptAlice,
                bob,
                {
                    1,
                    10,
                    50,
                    100,
                    200,
                    299,
                    301,
                    400,
                });

            // proof failure for incorrect amount when clawbacking from
            // carol carol: 100 in inbox, 300 in spending
            checkBadProofs(
                mptAlice,
                carol,
                {
                    1,
                    10,
                    50,
                    100,
                    300,
                    399,
                    401,
                    501,
                });

            // clawback with correct amount that passes proof verification
            mptAlice.confidentialClaw({
                .account = alice,
                .holder = bob,
                .amt = 300,
            });
            mptAlice.confidentialClaw({
                .account = alice,
                .holder = carol,
                .amt = 400,
            });
        }
    }

    void
    testMutatePrivacy(FeatureBitset features)
    {
        testcase("mutate lsfMPTCanConfidentialAmount");
        using namespace test::jtx;

        // can not create mpt issuance with tmfMPTCannotMutateCanConfidentialAmount
        // when featureDynamicMPT is disabled
        {
            Env env{*this, features - featureDynamicMPT};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 0,
                .mutableFlags = tmfMPTCannotMutateCanConfidentialAmount,
                .err = temDISABLED,
            });
        }

        // can not create mpt issuance with tmfMPTCannotMutateCanConfidentialAmount when
        // featureConfidentialTransfer is disabled
        {
            Env env{*this, features - featureConfidentialTransfer};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 0,
                .mutableFlags = tmfMPTCannotMutateCanConfidentialAmount,
                .err = temDISABLED,
            });
        }

        // if lsmfMPTCannotMutateCanConfidentialAmount is set, can not set/clear
        // lsfMPTCanConfidentialAmount
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer,
                .mutableFlags = tmfMPTCannotMutateCanConfidentialAmount,
            });

            mptAlice.set({
                .account = alice,
                .mutableFlags = tmfMPTSetCanConfidentialAmount,
                .err = tecNO_PERMISSION,
            });

            mptAlice.set({
                .account = alice,
                .mutableFlags = tmfMPTClearCanConfidentialAmount,
                .err = tecNO_PERMISSION,
            });
        }

        // Toggle lsfMPTCanConfidentialAmount
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanConfidentialAmount,
                .mutableFlags = tmfMPTCanMutateCanLock,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            auto holderPubKeySet = false;
            auto verifyToggle = [&](TER expectedResult, uint64_t amt) {
                if (!holderPubKeySet)
                    mptAlice.convert({
                        .account = bob,
                        .amt = amt,
                        .holderPubKey = mptAlice.getPubKey(bob),
                        .err = expectedResult,
                    });
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

            // set lsfMPTCanConfidentialAmount, but no effect because lsfMPTCanConfidentialAmount
            // was already set
            mptAlice.set({
                .account = alice,
                .mutableFlags = tmfMPTSetCanConfidentialAmount,
            });
            verifyToggle(tesSUCCESS, 10);

            // clear lsfMPTCanConfidentialAmount
            mptAlice.set({
                .account = alice,
                .mutableFlags = tmfMPTClearCanConfidentialAmount,
            });
            verifyToggle(tecNO_PERMISSION, 10);

            // can clear lsfMPTCanConfidentialAmount again but has no effect
            // for privacy settings
            mptAlice.set({
                .account = alice,
                .mutableFlags = tmfMPTClearCanConfidentialAmount | tmfMPTSetCanLock,
            });
            verifyToggle(tecNO_PERMISSION, 20);

            // set lsfMPTCanConfidentialAmount again
            mptAlice.set({
                .account = alice,
                .mutableFlags = tmfMPTSetCanConfidentialAmount,
            });
            verifyToggle(tesSUCCESS, 30);
        }

        // can not mutate lsfPrivacy when there's confidential
        // outstanding amount
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            // lsmfMPTCannotMutateCanConfidentialAmount is false by default,
            // so that lsfMPTCanConfidentialAmount can be mutated
            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanConfidentialAmount,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            // bob convert 50 to confidential
            mptAlice.convert({.account = bob, .amt = 50, .holderPubKey = mptAlice.getPubKey(bob)});

            // set or clear lsfMPTCanConfidentialAmount should fail because of
            // confidential outstanding balance
            mptAlice.set({
                .account = alice,
                .mutableFlags = tmfMPTSetCanConfidentialAmount,
                .err = tecNO_PERMISSION,
            });
            mptAlice.set({
                .account = alice,
                .mutableFlags = tmfMPTClearCanConfidentialAmount,
                .err = tecNO_PERMISSION,
            });

            // bob merge inbox
            mptAlice.mergeInbox({
                .account = bob,
            });

            // bob convert back all confidential balance
            mptAlice.convertBack({
                .account = bob,
                .amt = 50,
            });

            // now clear lsfMPTCanConfidentialAmount should succeed,
            // because there's no confidential outstanding balance
            mptAlice.set({
                .account = alice,
                .mutableFlags = tmfMPTClearCanConfidentialAmount,
            });

            // bob can not convert because lsfMPTCanConfidentialAmount was cleared
            // successfully
            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .holderPubKey = mptAlice.getPubKey(bob),
                .err = tecNO_PERMISSION,
            });

            // can set lsfMPTCanConfidentialAmount again when there's no confidential
            // outstanding balance
            mptAlice.set({
                .account = alice,
                .mutableFlags = tmfMPTSetCanConfidentialAmount,
            });
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

        mptAlice.create({
            .ownerCount = 1,
            .holderCount = 0,
            .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
        });

        mptAlice.authorize({
            .account = bob,
        });
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

        auto const spendingBalance =
            mptAlice.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING);
        BEAST_EXPECT(spendingBalance.has_value());
        auto const encryptedSpendingBalance =
            mptAlice.getEncryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING);
        BEAST_EXPECT(encryptedSpendingBalance.has_value() && !encryptedSpendingBalance->empty());

        Buffer const pedersenCommitment =
            mptAlice.getPedersenCommitment(*spendingBalance, pcBlindingFactor);
        Buffer const issuerCiphertext = mptAlice.encryptAmount(alice, amt, blindingFactor);
        Buffer const bobCiphertext = mptAlice.encryptAmount(bob, amt, blindingFactor);
        auto const version = mptAlice.getMPTokenVersion(bob);

        // These tests verify that the pedersen linkage proof validation
        // correctly rejects proofs generated with incorrect parameters.
        // The pedersen linkage proof proves that the balance commitment
        // PC = balance*G + rho*H is derived from the holder's encrypted
        // spending balance.

        // Helper to combine pedersen proof and bulletproof
        auto const combineProofs = [](Buffer const& pedersenProof, Buffer const& bulletproof) {
            Buffer combinedProof(pedersenProof.size() + bulletproof.size());
            std::memcpy(combinedProof.data(), pedersenProof.data(), pedersenProof.size());
            std::memcpy(
                combinedProof.data() + pedersenProof.size(),
                bulletproof.data(),
                bulletproof.size());
            return combinedProof;
        };

        auto const holderPubKey = mptAlice.getPubKey(bob);
        BEAST_EXPECT(holderPubKey.has_value());

        // Test 1: Proof generated with wrong pedersen commitment value.
        // The proof uses PC(1, rho) but the transaction submits PC(balance, rho).
        // Verification fails because the proof doesn't match the submitted commitment.
        {
            uint256 const contextHash =
                getConvertBackContextHash(bob, mptAlice.issuanceID(), env.seq(bob), version);
            Buffer const badPedersenCommitment =
                mptAlice.getPedersenCommitment(1, pcBlindingFactor);
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

            mptAlice.convertBack({
                .account = bob,
                .amt = amt,
                .proof = proof,
                .holderEncryptedAmt = bobCiphertext,
                .issuerEncryptedAmt = issuerCiphertext,
                .blindingFactor = blindingFactor,
                .pedersenCommitment = pedersenCommitment,
                .err = tecBAD_PROOF,
            });
        }

        // Test 2: Proof generated with wrong blinding factor (rho).
        // The pedersen commitment PC = balance*G + rho*H requires the same rho
        // used in proof generation. Using a different rho breaks the linkage.
        {
            uint256 const contextHash =
                getConvertBackContextHash(bob, mptAlice.issuanceID(), env.seq(bob), version);

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

            mptAlice.convertBack({
                .account = bob,
                .amt = amt,
                .proof = proof,
                .holderEncryptedAmt = bobCiphertext,
                .issuerEncryptedAmt = issuerCiphertext,
                .blindingFactor = blindingFactor,
                .pedersenCommitment = pedersenCommitment,
                .err = tecBAD_PROOF,
            });
        }

        // Test 3: Proof generated with wrong balance value.
        // The proof claims balance=1 but the encrypted spending balance contains
        // the actual balance. Verification fails because the values don't match.
        {
            uint256 const contextHash =
                getConvertBackContextHash(bob, mptAlice.issuanceID(), env.seq(bob), version);

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

            mptAlice.convertBack({
                .account = bob,
                .amt = amt,
                .proof = proof,
                .holderEncryptedAmt = bobCiphertext,
                .issuerEncryptedAmt = issuerCiphertext,
                .blindingFactor = blindingFactor,
                .pedersenCommitment = pedersenCommitment,
                .err = tecBAD_PROOF,
            });
        }

        // Test 4: Correct proof but wrong pedersen commitment in transaction.
        // The proof is generated correctly, but the transaction submits a
        // different pedersen commitment. Verification fails because the
        // submitted commitment doesn't match what the proof was generated for.
        {
            uint256 const contextHash =
                getConvertBackContextHash(bob, mptAlice.issuanceID(), env.seq(bob), version);
            Buffer const badPedersenCommitment =
                mptAlice.getPedersenCommitment(1, pcBlindingFactor);
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
                .pedersenCommitment = badPedersenCommitment,  // wrong pedersen commitment
                .err = tecBAD_PROOF,
            });
        }

        // Test 5: Proof generated with wrong context hash.
        // The context hash binds the proof to a specific transaction (account,
        // sequence, issuanceID, amount, version). Using a different context hash
        // makes the proof invalid for this transaction, preventing replay attacks.
        {
            uint256 const contextHash =
                getConvertBackContextHash(bob, mptAlice.issuanceID(), env.seq(bob), version);
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

            mptAlice.convertBack({
                .account = bob,
                .amt = amt,
                .proof = proof,
                .holderEncryptedAmt = bobCiphertext,
                .issuerEncryptedAmt = issuerCiphertext,
                .blindingFactor = blindingFactor,
                .pedersenCommitment = pedersenCommitment,
                .err = tecBAD_PROOF,
            });
        }

        // Test 6: Correct proof to verify the test setup is valid.
        // All parameters are correct, so the transaction should succeed.
        {
            uint256 const contextHash =
                getConvertBackContextHash(bob, mptAlice.issuanceID(), env.seq(bob), version);

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

        mptAlice.create({
            .ownerCount = 1,
            .holderCount = 0,
            .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
        });

        mptAlice.authorize({
            .account = bob,
        });
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

        auto const spendingBalance =
            mptAlice.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING);
        BEAST_EXPECT(spendingBalance.has_value());
        auto const encryptedSpendingBalance =
            mptAlice.getEncryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING);
        BEAST_EXPECT(encryptedSpendingBalance.has_value() && !encryptedSpendingBalance->empty());

        Buffer const pedersenCommitment =
            mptAlice.getPedersenCommitment(*spendingBalance, pcBlindingFactor);
        Buffer const issuerCiphertext = mptAlice.encryptAmount(alice, amt, blindingFactor);
        Buffer const bobCiphertext = mptAlice.encryptAmount(bob, amt, blindingFactor);
        auto const version = mptAlice.getMPTokenVersion(bob);

        // These tests verify that the bulletproof (range proof) validation
        // correctly rejects proofs generated with incorrect parameters.
        // The bulletproof proves that the remaining balance (balance - amount)
        // is non-negative, i.e., in the range [0, 2^64-1]. This prevents
        // overdrafts where a user tries to convert back more than they have.

        // Helper to combine pedersen proof and bulletproof
        auto const combineProofs = [](Buffer const& pedersenProof, Buffer const& bulletproof) {
            Buffer combinedProof(pedersenProof.size() + bulletproof.size());
            std::memcpy(combinedProof.data(), pedersenProof.data(), pedersenProof.size());
            std::memcpy(
                combinedProof.data() + pedersenProof.size(),
                bulletproof.data(),
                bulletproof.size());
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
                getConvertBackContextHash(bob, mptAlice.issuanceID(), env.seq(bob), version);

            Buffer const bulletproof = mptAlice.getBulletproof(
                {1},  // wrong remaining balance
                {pcBlindingFactor},
                contextHash);

            Buffer const proof = combineProofs(getPedersenProof(contextHash), bulletproof);

            mptAlice.convertBack({
                .account = bob,
                .amt = amt,
                .proof = proof,
                .holderEncryptedAmt = bobCiphertext,
                .issuerEncryptedAmt = issuerCiphertext,
                .blindingFactor = blindingFactor,
                .pedersenCommitment = pedersenCommitment,
                .err = tecBAD_PROOF,
            });
        }

        // Test 2: Bulletproof generated with wrong blinding factor.
        // The bulletproof must use the same blinding factor (rho) as the pedersen
        // commitment PC = (balance - amount)*G + rho*H. Using a different rho
        // creates a commitment mismatch and verification fails.
        {
            uint256 const contextHash =
                getConvertBackContextHash(bob, mptAlice.issuanceID(), env.seq(bob), version);

            Buffer const bulletproof = mptAlice.getBulletproof(
                {*spendingBalance - amt},
                {generateBlindingFactor()},  // wrong blinding factor
                contextHash);

            Buffer const proof = combineProofs(getPedersenProof(contextHash), bulletproof);

            mptAlice.convertBack({
                .account = bob,
                .amt = amt,
                .proof = proof,
                .holderEncryptedAmt = bobCiphertext,
                .issuerEncryptedAmt = issuerCiphertext,
                .blindingFactor = blindingFactor,
                .pedersenCommitment = pedersenCommitment,
                .err = tecBAD_PROOF,
            });
        }

        // Test 3: Bulletproof generated with wrong context hash.
        // The context hash binds the proof to a specific transaction (account,
        // sequence, issuanceID, amount, version). Using a different context hash
        // makes the proof invalid for this transaction, preventing replay attacks.
        {
            uint256 const contextHash =
                getConvertBackContextHash(bob, mptAlice.issuanceID(), env.seq(bob), version);

            uint256 const badContextHash{1};
            Buffer const bulletproof = mptAlice.getBulletproof(
                {*spendingBalance - amt},
                {pcBlindingFactor},
                badContextHash);  // wrong context hash

            Buffer const proof = combineProofs(getPedersenProof(contextHash), bulletproof);

            mptAlice.convertBack({
                .account = bob,
                .amt = amt,
                .proof = proof,
                .holderEncryptedAmt = bobCiphertext,
                .issuerEncryptedAmt = issuerCiphertext,
                .blindingFactor = blindingFactor,
                .pedersenCommitment = pedersenCommitment,
                .err = tecBAD_PROOF,
            });
        }

        // Test 4: Correct proof to verify the test setup is valid.
        // All parameters are correct, so the transaction should succeed.
        {
            uint256 const contextHash =
                getConvertBackContextHash(bob, mptAlice.issuanceID(), env.seq(bob), version);

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

    // This test verifies that proofs are non-replayable by simulating replays
    // with an outdated ledger version or an old sequence number.
    // It confirms that the validator detects the resulting ContextID mismatch
    // and rejects the transaction with tecBAD_PROOF.
    void
    testProofContextBinding(FeatureBitset features)
    {
        testcase("Proof context binding (Sequence and Version)");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        MPTTester mptAlice(env, alice, {.holders = {bob}});

        mptAlice.create({
            .ownerCount = 1,
            .holderCount = 0,
            .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
        });

        mptAlice.authorize({
            .account = bob,
        });
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

        uint64_t const amt = 10;
        Buffer const blindingFactor = generateBlindingFactor();
        Buffer const pcBlindingFactor = generateBlindingFactor();

        auto const spendingBalance =
            mptAlice.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING);
        BEAST_EXPECT(
            spendingBalance.has_value() && *spendingBalance == 40);  // because bob encrypted 40
        auto const encryptedSpendingBalance =
            mptAlice.getEncryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING);
        BEAST_EXPECT(encryptedSpendingBalance.has_value() && !encryptedSpendingBalance->empty());

        Buffer const pedersenCommitment =
            mptAlice.getPedersenCommitment(*spendingBalance, pcBlindingFactor);
        Buffer const issuerCiphertext = mptAlice.encryptAmount(alice, amt, blindingFactor);
        Buffer const bobCiphertext = mptAlice.encryptAmount(bob, amt, blindingFactor);

        auto const currentVersion = mptAlice.getMPTokenVersion(bob);

        // Invalid Version Binding
        // Simulates replaying a full transaction after the ledger's version
        // has updated. We simulate this by attempting to use a proof built
        // using an older version but with the current valid sequence.
        {
            uint32_t const seqA = env.seq(bob);
            uint32_t const oldVersion = currentVersion - 1;
            uint256 const badContextHash =
                getConvertBackContextHash(bob, mptAlice.issuanceID(), seqA, oldVersion);

            Buffer const proof = mptAlice.getConvertBackProof(
                bob,
                amt,
                badContextHash,
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
                .err = tecBAD_PROOF,
            });
        }

        // Invalid Sequence Binding
        // Simulates submitting a new transaction (with a new, valid signature
        // and sequence) but reusing a ZKP from a previous sequence number.
        {
            // Fetch updated sequence, as the tecBAD_PROOF above consumed one
            uint32_t const seqB = env.seq(bob);
            uint32_t const oldSeq = seqB - 1;
            uint256 const badContextHash =
                getConvertBackContextHash(bob, mptAlice.issuanceID(), oldSeq, currentVersion);

            Buffer const proof = mptAlice.getConvertBackProof(
                bob,
                amt,
                badContextHash,
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
                .err = tecBAD_PROOF,
            });
        }

        // Verify Correct Proof Passes
        // Ensure the test setup was correct and functions when no replay is attempted.
        {
            // Fetch updated sequence once more
            uint32_t const seqC = env.seq(bob);
            uint256 const goodContextHash =
                getConvertBackContextHash(bob, mptAlice.issuanceID(), seqC, currentVersion);

            Buffer const proof = mptAlice.getConvertBackProof(
                bob,
                amt,
                goodContextHash,
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

    // This test simulates a valid proof π extracted from a transaction
    // for amount m1 is reused in a new transaction for a different
    // amount m2 with different ciphertexts. It confirms the context hash
    // recomputation fails due to the ciphertext binding mismatch, resulting
    // in tecBAD_PROOF.
    void
    testProofCiphertextBinding(FeatureBitset features)
    {
        testcase("Proof ciphertext binding");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        MPTTester mptAlice(env, alice, {.holders = {bob}});

        mptAlice.create({
            .ownerCount = 1,
            .holderCount = 0,
            .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
        });

        mptAlice.authorize({
            .account = bob,
        });
        mptAlice.pay(alice, bob, 100);

        mptAlice.generateKeyPair(alice);
        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        mptAlice.generateKeyPair(bob);
        mptAlice.convert({
            .account = bob,
            .amt = 50,
            .holderPubKey = mptAlice.getPubKey(bob),
        });

        mptAlice.mergeInbox({
            .account = bob,
        });

        auto const spendingBalance =
            mptAlice.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING);
        auto const encryptedSpendingBalance =
            mptAlice.getEncryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING);
        auto const version = mptAlice.getMPTokenVersion(bob);
        Buffer const pcBlindingFactor = generateBlindingFactor();
        Buffer const pedersenCommitment =
            mptAlice.getPedersenCommitment(*spendingBalance, pcBlindingFactor);

        // Generate a valid proof pi for Amount m1 = 10
        uint64_t const amtA = 10;
        uint32_t const currentSeq = env.seq(bob);
        uint256 const contextHashA =
            getConvertBackContextHash(bob, mptAlice.issuanceID(), currentSeq, version);

        Buffer const proofA = mptAlice.getConvertBackProof(
            bob,
            amtA,
            contextHashA,
            {
                .pedersenCommitment = pedersenCommitment,
                .amt = *spendingBalance,
                .encryptedAmt = *encryptedSpendingBalance,
                .blindingFactor = pcBlindingFactor,
            });

        // Construct Transaction B with Amount m2 = 20 and attach Proof pi
        uint64_t const amtB = 20;
        Buffer const blindingFactorB = generateBlindingFactor();
        Buffer const bobCiphertextB = mptAlice.encryptAmount(bob, amtB, blindingFactorB);
        Buffer const issuerCiphertextB = mptAlice.encryptAmount(alice, amtB, blindingFactorB);

        // We attempt to verify the proof pi (for amt 10) against the new ciphertexts (for amt 20).
        mptAlice.convertBack(
            {.account = bob,
             .amt = amtB,
             .proof = proofA,  // Extracted/Reused proof from Transaction A
             .holderEncryptedAmt = bobCiphertextB,
             .issuerEncryptedAmt = issuerCiphertextB,
             .blindingFactor = blindingFactorB,
             .pedersenCommitment = pedersenCommitment,
             .err = tecBAD_PROOF});  // Expected failure
    }

    // This test simulates a valid proof π and ciphertext are
    // tied to version v, but are reused after an inbox merge has incremented
    // the CBS version to v+1. It confirms the validator rejects the transaction
    // before acceptance due to the ContextID mismatch.
    void
    testProofVersionMismatch(FeatureBitset features)
    {
        testcase("Proof version mismatch");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        MPTTester mptAlice(env, alice, {.holders = {bob}});

        mptAlice.create({
            .ownerCount = 1,
            .holderCount = 0,
            .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
        });

        mptAlice.authorize({
            .account = bob,
        });
        mptAlice.pay(alice, bob, 1000);

        mptAlice.generateKeyPair(alice);
        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        mptAlice.generateKeyPair(bob);

        // Initial state: Version v
        // Convert and merge to establish a spending balance and initial version
        mptAlice.convert({
            .account = bob,
            .amt = 100,
            .holderPubKey = mptAlice.getPubKey(bob),
        });
        mptAlice.mergeInbox({
            .account = bob,
        });

        auto const versionV = mptAlice.getMPTokenVersion(bob);
        auto const spendingBalanceV =
            mptAlice.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING);
        auto const encryptedSpendingBalanceV =
            mptAlice.getEncryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING);

        // Parameters for the intended ConvertBack transaction
        uint64_t const amt = 10;
        Buffer const blindingFactor = generateBlindingFactor();
        Buffer const pcBlindingFactor = generateBlindingFactor();
        Buffer const pedersenCommitment =
            mptAlice.getPedersenCommitment(*spendingBalanceV, pcBlindingFactor);
        Buffer const issuerCiphertext = mptAlice.encryptAmount(alice, amt, blindingFactor);
        Buffer const bobCiphertext = mptAlice.encryptAmount(bob, amt, blindingFactor);

        // State Change: Increment version to v+1
        // Converting more funds and merging increments the sfConfidentialBalanceVersion
        mptAlice.convert({
            .account = bob,
            .amt = 50,
        });
        mptAlice.mergeInbox({
            .account = bob,
        });

        BEAST_EXPECT(mptAlice.getMPTokenVersion(bob) > versionV);

        // Attack: Attempt to reuse proof tied to Version v at ledger Version v+1
        uint32_t const currentSeq = env.seq(bob);
        // Proof is explicitly generated using the outdated Version v
        uint256 const oldContextHash =
            getConvertBackContextHash(bob, mptAlice.issuanceID(), currentSeq, versionV);

        Buffer const oldProof = mptAlice.getConvertBackProof(
            bob,
            amt,
            oldContextHash,
            {
                .pedersenCommitment = pedersenCommitment,
                .amt = *spendingBalanceV,
                .encryptedAmt = *encryptedSpendingBalanceV,
                .blindingFactor = pcBlindingFactor,
            });

        // Submit and verify failure
        mptAlice.convertBack(
            {.account = bob,
             .amt = amt,
             .proof = oldProof,
             .holderEncryptedAmt = bobCiphertext,
             .issuerEncryptedAmt = issuerCiphertext,
             .blindingFactor = blindingFactor,
             .pedersenCommitment = pedersenCommitment,
             .err = tecBAD_PROOF});  // Fails because TransactionContextID differs
    }

    /* This test simulates an attack where the holder ciphertext is modified
     * via homomorphic addition (adding Encrypted_amt(1)) while leaving the issuer
     * ciphertext unchanged. It confirms that the validator detects the
     * mismatch between the re-computed ciphertexts and the submitted ones,
     * resulting in tecBAD_PROOF.   */
    void
    testHomomorphicCiphertextModification(FeatureBitset features)
    {
        testcase("Homomorphic ciphertext modification");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        MPTTester mptAlice(env, alice, {.holders = {bob}});

        mptAlice.create(
            {.ownerCount = 1,
             .holderCount = 0,
             .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount});

        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 100);

        mptAlice.generateKeyPair(alice);
        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        mptAlice.generateKeyPair(bob);

        // Bob converts 50 to confidential balance
        mptAlice.convert({
            .account = bob,
            .amt = 50,
            .holderPubKey = mptAlice.getPubKey(bob),
        });
        mptAlice.mergeInbox({.account = bob});

        // Prepare valid parameters for a ConvertBack of 10
        uint64_t const amt = 10;
        Buffer const bf = generateBlindingFactor();

        auto const holderCipherText = mptAlice.encryptAmount(bob, amt, bf);
        auto const issuerCipherText = mptAlice.encryptAmount(alice, amt, bf);

        // Generate a "Delta" ciphertext (Encrypting 1)
        // We use Bob's key because we are tampering with Bob's (Holder's) field
        Buffer const deltaBf = generateBlindingFactor();
        auto const deltaCipherText = mptAlice.encryptAmount(bob, 1, deltaBf);

        // Homomorphically add Delta to HolderCipherText: Tampered = Enc(10) + Enc(1) = Enc(11)
        auto tamperedOpt = homomorphicAdd(holderCipherText, deltaCipherText);
        BEAST_EXPECT(tamperedOpt.has_value());
        Buffer tamperedHolderCipherText = std::move(*tamperedOpt);

        // Generate a valid proof for the ORIGINAL amount (10)
        auto const spendingBal =
            mptAlice.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING);
        auto const spendingBalEnc =
            mptAlice.getEncryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING);
        Buffer const pcBf = generateBlindingFactor();
        auto const pedersenCommitment = mptAlice.getPedersenCommitment(*spendingBal, pcBf);

        auto const currentVersion = mptAlice.getMPTokenVersion(bob);
        // Uses the new signature: Account, IssuanceID, Sequence, Version
        uint256 const contextHash =
            getConvertBackContextHash(bob, mptAlice.issuanceID(), env.seq(bob), currentVersion);

        Buffer const proof = mptAlice.getConvertBackProof(
            bob,
            amt,
            contextHash,
            {
                .pedersenCommitment = pedersenCommitment,
                .amt = *spendingBal,
                .encryptedAmt = *spendingBalEnc,
                .blindingFactor = pcBf,
            });

        // Submit transaction with Divergent Ciphertexts
        // Holder Ciphertext encrypts 11. Issuer Ciphertext encrypts 10.
        // The consistency check (re-encryption of `amt` with `bf`) will match Issuer but FAIL for
        // Holder.
        mptAlice.convertBack(
            {.account = bob,
             .amt = amt,
             .proof = proof,
             .holderEncryptedAmt = tamperedHolderCipherText,  // Tampered (11)
             .issuerEncryptedAmt = issuerCipherText,          // Original (10)
             .blindingFactor = bf,
             .pedersenCommitment = pedersenCommitment,
             .err = tecBAD_PROOF});
    }

    /* This test verifies that rippled correctly rejects attempts to
     * overflow the maximum allowable token amount via homomorphic manipulation.
     * It simulates an attack where an individual takes a valid ciphertext encrypting
     * the maximum amount (maxMPTokenAmount) and homomorphically adds an encryption of
     * 1 to it, producing a ciphertext for MAX+1. The test confirms that the Bulletproof
     * range proof or inner-product constraints detect this overflow and invalidate the
     * transaction, preserving the supply invariant. */
    void
    testSendHomomorphicOverflow(FeatureBitset features)
    {
        testcase("Send: homomorphic overflow attack via Enc(MAX) + Enc(1)");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

        mptAlice.create(
            {.ownerCount = 1,
             .flags = tfMPTCanLock | tfMPTCanConfidentialAmount | tfMPTCanTransfer});
        mptAlice.authorize({.account = bob});
        mptAlice.authorize({.account = carol});

        mptAlice.pay(alice, bob, 100);
        mptAlice.pay(alice, carol, 50);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);

        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        mptAlice.convert({.account = bob, .amt = 100, .holderPubKey = mptAlice.getPubKey(bob)});
        mptAlice.mergeInbox({.account = bob});

        mptAlice.convert({.account = carol, .amt = 50, .holderPubKey = mptAlice.getPubKey(carol)});
        mptAlice.mergeInbox({.account = carol});

        // Bob sends 10 to carol.  The send amount (10) and Bob's remaining balance
        // (90) are both within [0, maxMPTokenAmount].  Range proof passes.
        mptAlice.send({.account = bob, .dest = carol, .amt = 10});

        // Bob's spending balance is 90 after the baseline send.
        auto const bobSpendingBefore =
            mptAlice.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING);
        BEAST_EXPECT(bobSpendingBefore == 90);

        // Construct Enc(maxMPTokenAmount) with Bob's public key.
        Buffer const bf1 = generateBlindingFactor();
        Buffer const encMax = mptAlice.encryptAmount(bob, maxMPTokenAmount, bf1);

        // Construct Enc(1) with a separate blinding factor.
        Buffer const bf2 = generateBlindingFactor();
        Buffer const encOne = mptAlice.encryptAmount(bob, 1, bf2);

        // Homomorphically add to produce CB_S_holder' = Enc(MAX) + Enc(1)
        auto overflowedOpt = homomorphicAdd(encMax, encOne);
        BEAST_EXPECT(overflowedOpt.has_value());
        Buffer overflowedCt = std::move(*overflowedOpt);

        // Submit the send transaction with the tampered ciphertext.
        // Setting amt = maxMPTokenAmount + 1 drives proof generation for the
        // overflowed value.  The bulletproof range check [0, maxMPTokenAmount]
        // rejects MAX+1; the validator must return tecBAD_PROOF.
        mptAlice.send({
            .account = bob,
            .dest = carol,
            .amt = maxMPTokenAmount + 1,
            .senderEncryptedAmt = overflowedCt,
            .err = tecBAD_PROOF,
        });

        auto const bobSpendingAfter =
            mptAlice.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING);
        BEAST_EXPECT(bobSpendingBefore == bobSpendingAfter);
    }

    /* This test ensures that the system prevents underflow attacks where a user
     * attempts to create a negative balance through homomorphic subtraction. It
     * simulates a scenario where an attacker takes a ciphertext encrypting zero
     * and subtracts an encryption of 1, resulting in a value of -1.
     * The test asserts that the range proof verification fails because the resulting
     * value falls outside the valid non-negative range [0, maxMPTokenAmount],
     * causing the validator to reject the transaction with tecBAD_PROOF. */
    void
    testConvertBackHomomorphicUnderflow(FeatureBitset features)
    {
        testcase("ConvertBack: homomorphic underflow attack via Enc(0) - Enc(1)");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        MPTTester mptAlice(env, alice, {.holders = {bob}});

        mptAlice.create(
            {.ownerCount = 1,
             .holderCount = 0,
             .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount});

        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 10);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);

        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        mptAlice.convert({.account = bob, .amt = 10, .holderPubKey = mptAlice.getPubKey(bob)});
        mptAlice.mergeInbox({.account = bob});

        // Converting back 1 from 10 leaves remaining balance = 9 (non-negative).
        // Range proof [0, maxMPTokenAmount] passes.
        mptAlice.convertBack({.account = bob, .amt = 1});

        // Bob's spending balance is now 9; public balance is 1.
        auto const bobSpendingBefore =
            mptAlice.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING);
        BEAST_EXPECT(bobSpendingBefore == 9);
        auto const bobPublicBefore = mptAlice.getBalance(bob);
        BEAST_EXPECT(bobPublicBefore == 1);

        // Construct Enc(0) — the zero encrypted balance using Bob's key.
        Buffer const bf1 = generateBlindingFactor();
        Buffer const encZero = mptAlice.encryptAmount(bob, 0, bf1);

        // Construct Enc(1) with a separate blinding factor.
        Buffer const bf2 = generateBlindingFactor();
        Buffer const encOne = mptAlice.encryptAmount(bob, 1, bf2);

        // Homomorphically subtract to produce CB_S_holder' = Enc(0) − Enc(1)
        // = Enc(−1), which lies below [0, maxMPTokenAmount].
        auto underflowedOpt = homomorphicSubtract(encZero, encOne);
        BEAST_EXPECT(underflowedOpt.has_value());
        Buffer underflowedCt = std::move(*underflowedOpt);

        // The underflowed value as uint64_t: 0 - 1 wraps to 0xFFFFFFFFFFFFFFFF.
        // Generate a real proof using this wrapped value. The validator must still reject it
        // because 0xFFFFFFFFFFFFFFFE (remaining balance) is outside [0, maxMPTokenAmount].
        constexpr std::uint64_t underflowedAmt =
            static_cast<std::uint64_t>(0) - static_cast<std::uint64_t>(1);

        Buffer const pcBf = generateBlindingFactor();
        Buffer const pedersenCommitment = mptAlice.getPedersenCommitment(underflowedAmt, pcBf);

        auto const currentVersion = mptAlice.getMPTokenVersion(bob);
        uint256 const contextHash =
            getConvertBackContextHash(bob, mptAlice.issuanceID(), env.seq(bob), currentVersion);

        Buffer const proof = mptAlice.getConvertBackProof(
            bob,
            1,
            contextHash,
            {
                .pedersenCommitment = pedersenCommitment,
                .amt = underflowedAmt,
                .encryptedAmt = underflowedCt,
                .blindingFactor = pcBf,
            });

        mptAlice.convertBack({
            .account = bob,
            .amt = 1,
            .proof = proof,
            .holderEncryptedAmt = underflowedCt,
            .pedersenCommitment = pedersenCommitment,
            .err = tecBAD_PROOF,
        });

        // Supply invariant: both public and confidential balances must be unchanged
        // after the rejected attack.
        BEAST_EXPECT(mptAlice.getBalance(bob) == bobPublicBefore);
        auto const bobSpendingAfter =
            mptAlice.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING);
        BEAST_EXPECT(bobSpendingBefore == bobSpendingAfter);
    }

    /* This test ensures that when sending confidential tokens, the encrypted
     * amounts are securely locked to the correct accounts' official public keys.
     *
     * Attack scenario — Encrypting the issuer's copy with the wrong key:
     * A sender correctly encrypts the hidden transfer amount for themselves
     * and the receiver. However, they intentionally encrypt the issuer's
     * copy of the data using the wrong public key (for example, using the
     * receiver's key instead of the official issuer's key). */
    void
    testSendWrongIssuerPublicKey(FeatureBitset features)
    {
        testcase("Send: issuer ciphertext encrypted under wrong public key");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

        mptAlice.create(
            {.ownerCount = 1,
             .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount});
        mptAlice.authorize({.account = bob});
        mptAlice.authorize({.account = carol});
        mptAlice.pay(alice, bob, 100);
        mptAlice.pay(alice, carol, 50);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);

        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        mptAlice.convert({.account = bob, .amt = 100, .holderPubKey = mptAlice.getPubKey(bob)});
        mptAlice.mergeInbox({.account = bob});

        mptAlice.convert({.account = carol, .amt = 50, .holderPubKey = mptAlice.getPubKey(carol)});
        mptAlice.mergeInbox({.account = carol});

        auto const bobSpendingBefore =
            mptAlice.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING);
        BEAST_EXPECT(bobSpendingBefore == 100);

        // issuer ciphertext encrypted under carol's holder key
        // (should be under alice's registered issuer key).
        {
            Buffer const bf = generateBlindingFactor();
            Buffer const wrongIssuerCt = mptAlice.encryptAmount(carol, 10, bf);

            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .issuerEncryptedAmt = wrongIssuerCt,
                .err = tecBAD_PROOF,
            });
        }

        // issuer ciphertext encrypted under bob's holder key
        // (the sender's own key — still not the registered issuer key).
        {
            Buffer const bf = generateBlindingFactor();
            Buffer const wrongIssuerCt = mptAlice.encryptAmount(bob, 10, bf);

            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .issuerEncryptedAmt = wrongIssuerCt,
                .err = tecBAD_PROOF,
            });
        }

        // all balances unchanged
        BEAST_EXPECT(
            mptAlice.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING) ==
            bobSpendingBefore);
        BEAST_EXPECT(mptAlice.getDecryptedBalance(carol, MPTTester::HOLDER_ENCRYPTED_INBOX) == 0);
    }

    // Exercises every Confidential Transfer transaction type (MPTokenIssuanceSet,
    // Convert, MergeInbox, Send, ConvertBack) using tickets instead of regular account
    // sequence numbers.
    void
    testWithTickets(FeatureBitset features)
    {
        testcase("Confidential transfer with tickets");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
        });
        mptAlice.authorize({.account = bob});
        mptAlice.authorize({.account = carol});
        mptAlice.pay(alice, bob, 100);
        mptAlice.pay(alice, carol, 100);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);

        // MPTokenIssuanceSet with ticket, registers alice's issuer key.
        {
            std::uint32_t const ticketSeq = env.seq(alice) + 1;
            env(ticket::create(alice, 1));
            mptAlice.set({.issuerPubKey = mptAlice.getPubKey(alice), .ticketSeq = ticketSeq});
        }

        // ConfidentialMPTConvert with ticket, first convert registers bob's key.
        {
            std::uint32_t const ticketSeq = env.seq(bob) + 1;
            env(ticket::create(bob, 1));
            mptAlice.convert(
                {.account = bob,
                 .amt = 50,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .ticketSeq = ticketSeq});
            env.require(mptbalance(mptAlice, bob, 50));
        }

        // ConfidentialMPTConvert with ticket
        {
            std::uint32_t const ticketSeq = env.seq(bob) + 1;
            env(ticket::create(bob, 1));
            mptAlice.convert({.account = bob, .amt = 20, .ticketSeq = ticketSeq});
            env.require(mptbalance(mptAlice, bob, 30));
        }

        // ConfidentialMPTMergeInbox with ticket.
        {
            std::uint32_t const ticketSeq = env.seq(bob) + 1;
            env(ticket::create(bob, 1));
            mptAlice.mergeInbox({.account = bob, .ticketSeq = ticketSeq});
        }

        mptAlice.convert({.account = carol, .amt = 50, .holderPubKey = mptAlice.getPubKey(carol)});
        mptAlice.mergeInbox({.account = carol});

        // ConfidentialMPTSend with ticket.
        {
            std::uint32_t const ticketSeq = env.seq(bob) + 1;
            env(ticket::create(bob, 1));
            mptAlice.send({.account = bob, .dest = carol, .amt = 10, .ticketSeq = ticketSeq});
        }

        // Merge carol's inbox so her spending balance includes the received send.
        mptAlice.mergeInbox({.account = carol});

        // ConfidentialMPTConvertBack with ticket.
        // The convertBack proof context hash must use the ticket sequence.
        {
            std::uint32_t const ticketSeq = env.seq(carol) + 1;
            env(ticket::create(carol, 1));
            mptAlice.convertBack({.account = carol, .amt = 10, .ticketSeq = ticketSeq});
            // carol converted 50, received 10 from bob, then converted back 10 → public 60
            env.require(mptbalance(mptAlice, carol, 60));
        }
    }

    // Verifies that cryptographic proofs in Convert transactions are bound to
    // the ticket sequence rather than the account sequence.
    // A proof built with the ticket sequence passes.
    void
    testConvertTicketProofBinding(FeatureBitset features)
    {
        testcase("Convert proof binds to ticket sequence");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        MPTTester mptAlice(env, alice, {.holders = {bob}});

        mptAlice.create({
            .ownerCount = 1,
            .holderCount = 0,
            .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
        });
        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 100);

        mptAlice.generateKeyPair(alice);
        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});
        mptAlice.generateKeyPair(bob);

        uint64_t const amt = 30;
        Buffer const bf = generateBlindingFactor();
        Buffer const holderCt = mptAlice.encryptAmount(bob, amt, bf);
        Buffer const issuerCt = mptAlice.encryptAmount(alice, amt, bf);

        std::uint32_t const ticketSeq1 = env.seq(bob) + 1;
        env(ticket::create(bob, 1));

        // Invalid: Schnorr proof built with the account seq (env.seq(bob)) rather
        // than the ticket seq (ticketSeq1).
        {
            BEAST_EXPECT(env.seq(bob) != ticketSeq1);
            uint256 const badCtxHash =
                getConvertContextHash(bob, mptAlice.issuanceID(), env.seq(bob));
            auto const badProof = mptAlice.getSchnorrProof(bob, badCtxHash);
            BEAST_EXPECT(badProof.has_value());

            mptAlice.convert({
                .account = bob,
                .amt = amt,
                .proof = strHex(*badProof),
                .holderPubKey = mptAlice.getPubKey(bob),
                .holderEncryptedAmt = holderCt,
                .issuerEncryptedAmt = issuerCt,
                .blindingFactor = bf,
                .ticketSeq = ticketSeq1,
                .err = tecBAD_PROOF,
            });
        }

        std::uint32_t const ticketSeq2 = env.seq(bob) + 1;
        env(ticket::create(bob, 1));

        // Valid: proof auto-generated by convert() using ticketSeq2; context hashes match.
        mptAlice.convert({
            .account = bob,
            .amt = amt,
            .holderPubKey = mptAlice.getPubKey(bob),
            .holderEncryptedAmt = holderCt,
            .issuerEncryptedAmt = issuerCt,
            .blindingFactor = bf,
            .ticketSeq = ticketSeq2,
        });
        env.require(mptbalance(mptAlice, bob, 70));
    }

    // Exercises ticket-specific error codes for confidential transfer transactions:
    // terPRE_TICKET when the ticket doesn't exist yet, and tefNO_TICKET when
    // the ticket has already been consumed or was never created.
    void
    testTicketErrors(FeatureBitset features)
    {
        testcase("Confidential transfer ticket errors");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        MPTTester mptAlice(env, alice, {.holders = {bob}});

        mptAlice.create({
            .ownerCount = 1,
            .holderCount = 0,
            .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
        });
        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 100);

        mptAlice.generateKeyPair(alice);
        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});
        mptAlice.generateKeyPair(bob);

        // Give bob an inbox balance so MergeInbox has something to merge.
        mptAlice.convert({.account = bob, .amt = 10, .holderPubKey = mptAlice.getPubKey(bob)});

        // Use MergeInbox as the confidential transfer transaction under test
        // so that ticket errors are isolated from cryptographic verification.

        // terPRE_TICKET: ticket sequence is far in the future and hasn't been created.
        mptAlice.mergeInbox(
            {.account = bob, .ticketSeq = env.seq(bob) + 100, .err = terPRE_TICKET});

        // Create one ticket and use it successfully.
        std::uint32_t const ticketSeq = env.seq(bob) + 1;
        env(ticket::create(bob, 1));
        mptAlice.mergeInbox({.account = bob, .ticketSeq = ticketSeq});

        // tefNO_TICKET: attempt to reuse the same (already-consumed) ticket.
        mptAlice.mergeInbox({.account = bob, .ticketSeq = ticketSeq, .err = tefNO_TICKET});

        // tefNO_TICKET: ticket sequence is in the past but was never created.
        mptAlice.mergeInbox({.account = bob, .ticketSeq = 1, .err = tefNO_TICKET});
    }

    // Basic tests of confidential transfer through delegation. Verifies that a delegated account
    // with the appropriate permissions can execute confidential transfer transactions
    // on behalf of the delegator.
    void
    testConfidentialDelegation(FeatureBitset features)
    {
        testcase("Confidential transfers through delegation");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const dave{"dave"};

        MPTTester mptAlice(env, alice, {.holders = {bob, carol}});
        env.fund(XRP(10000), dave);
        env.close();

        mptAlice.create({
            .ownerCount = 1,
            .flags =
                tfMPTCanTransfer | tfMPTCanLock | tfMPTCanClawback | tfMPTCanConfidentialAmount,
        });
        mptAlice.authorize({.account = bob});
        mptAlice.authorize({.account = carol});
        mptAlice.pay(alice, bob, 200);
        mptAlice.pay(alice, carol, 100);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);
        mptAlice.set({.issuerPubKey = mptAlice.getPubKey(alice)});

        // Bob delegates Convert, MergeInbox to dave.
        env(delegate::set(bob, dave, {"ConfidentialMPTConvert", "ConfidentialMPTMergeInbox"}));
        env.close();

        // Carol has no permission from bob to convert on his behalf.
        mptAlice.convert({
            .account = bob,
            .amt = 10,
            .holderPubKey = mptAlice.getPubKey(bob),
            .delegate = carol,
            .err = terNO_DELEGATE_PERMISSION,
        });

        // Dave executes Convert on behalf of bob, registering bob's key.
        mptAlice.convert({
            .account = bob,
            .amt = 100,
            .holderPubKey = mptAlice.getPubKey(bob),
            .delegate = dave,
        });
        env.require(mptbalance(mptAlice, bob, 100));

        // Dave executes Convert again on behalf of bob (no key registration).
        mptAlice.convert({.account = bob, .amt = 50, .delegate = dave});

        // Dave executes MergeInbox on behalf of bob.
        mptAlice.mergeInbox({.account = bob, .delegate = dave});

        // Carol converts and merge inbox.
        mptAlice.convert({
            .account = carol,
            .amt = 100,
            .holderPubKey = mptAlice.getPubKey(carol),
        });
        mptAlice.mergeInbox({.account = carol});

        // Dave does not have permission to send on behalf of bob.
        mptAlice.send(
            {.account = bob,
             .dest = carol,
             .amt = 10,
             .delegate = dave,
             .err = terNO_DELEGATE_PERMISSION});

        // Bob delegates ConfidentialMPTSend to dave.
        env(delegate::set(
            bob,
            dave,
            {"ConfidentialMPTConvert", "ConfidentialMPTMergeInbox", "ConfidentialMPTSend"}));
        env.close();

        // Dave executes Send on behalf of bob.
        mptAlice.send({.account = bob, .dest = carol, .amt = 10, .delegate = dave});
        mptAlice.mergeInbox({.account = carol});

        // Dave does not have permission to convert back on behalf of bob.
        mptAlice.convertBack(
            {.account = bob, .amt = 10, .delegate = dave, .err = terNO_DELEGATE_PERMISSION});

        // Bob delegates ConfidentialMPTConvertBack to dave.
        env(delegate::set(
            bob,
            dave,
            {"ConfidentialMPTConvert",
             "ConfidentialMPTMergeInbox",
             "ConfidentialMPTSend",
             "ConfidentialMPTConvertBack"}));
        env.close();

        // Dave executes ConvertBack on behalf of bob.
        mptAlice.convertBack({.account = bob, .amt = 10, .delegate = dave});

        // Dave does not have permission to clawback on behalf of alice.
        mptAlice.confidentialClaw(
            {.holder = bob, .amt = 130, .delegate = dave, .err = terNO_DELEGATE_PERMISSION});

        // Alice delegates ConfidentialMPTClawback to dave.
        env(delegate::set(alice, dave, {"ConfidentialMPTClawback"}));
        env.close();

        // Dave executes Clawback on behalf of alice.
        mptAlice.confidentialClaw({.holder = bob, .amt = 130, .delegate = dave});
    }

    // Verifies that revoking delegation prevents further delegated operations.
    void
    testDelegationRevocation(FeatureBitset features)
    {
        testcase("Confidential delegation revocation");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};

        MPTTester mptAlice(env, alice, {.holders = {bob}});
        env.fund(XRP(10000), carol);
        env.close();

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanConfidentialAmount,
        });
        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 100);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.set({.issuerPubKey = mptAlice.getPubKey(alice)});

        // Creating the Delegate SLE consumes one owner reserve slot for bob.
        auto const bobOwnersBefore = ownerCount(env, bob);
        env(delegate::set(bob, carol, {"ConfidentialMPTConvert", "ConfidentialMPTMergeInbox"}));
        env.close();
        env.require(owners(bob, bobOwnersBefore + 1));

        // Carol converts and merge inbox on behalf of bob.
        mptAlice.convert({
            .account = bob,
            .amt = 50,
            .holderPubKey = mptAlice.getPubKey(bob),
            .delegate = carol,
        });
        mptAlice.mergeInbox({.account = bob, .delegate = carol});

        // Bob revokes all permissions, deletes the Delegate SLE, releasing the reserve.
        env(delegate::set(bob, carol, std::vector<std::string>{}));
        env.close();
        env.require(owners(bob, bobOwnersBefore));

        // Carol can no longer convert on behalf of bob.
        mptAlice.convert({
            .account = bob,
            .amt = 30,
            .delegate = carol,
            .err = terNO_DELEGATE_PERMISSION,
        });

        // Bob can still convert by himself.
        mptAlice.convert({.account = bob, .amt = 30});
    }

    // Verifies that a delegated confidential transfer works correctly when an
    // auditor is configured on the issuance.
    void
    testDelegationWithAuditor(FeatureBitset features)
    {
        testcase("Confidential delegation with auditor");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const dave{"dave"};
        Account const auditor{"auditor"};

        MPTTester mptAlice(env, alice, {.holders = {bob, carol}, .auditor = auditor});
        env.fund(XRP(10000), dave);
        env.close();

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanConfidentialAmount,
        });
        mptAlice.authorize({.account = bob});
        mptAlice.authorize({.account = carol});
        mptAlice.pay(alice, bob, 100);
        mptAlice.pay(alice, carol, 100);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);
        mptAlice.generateKeyPair(auditor);
        mptAlice.set(
            {.issuerPubKey = mptAlice.getPubKey(alice),
             .auditorPubKey = mptAlice.getPubKey(auditor)});

        // Bob delegates Convert and Send permissions to dave.
        env(delegate::set(bob, dave, {"ConfidentialMPTSend", "ConfidentialMPTConvert"}));
        env.close();

        // Dave converts on behalf of bob.
        mptAlice.convert(
            {.account = bob, .amt = 50, .holderPubKey = mptAlice.getPubKey(bob), .delegate = dave});
        mptAlice.mergeInbox({.account = bob});

        mptAlice.convert({
            .account = carol,
            .amt = 50,
            .holderPubKey = mptAlice.getPubKey(carol),
        });
        mptAlice.mergeInbox({.account = carol});

        // Dave sends on behalf of bob.
        mptAlice.send({.account = bob, .dest = carol, .amt = 20, .delegate = dave});
        mptAlice.send({.account = bob, .dest = carol, .amt = 10, .delegate = dave});

        // Bob delegates ConvertBack and Send permissions to auditor.
        env(delegate::set(bob, auditor, {"ConfidentialMPTSend", "ConfidentialMPTConvertBack"}));
        env.close();

        // auditor can send and convert back on behalf of bob as well.
        mptAlice.send({.account = bob, .dest = carol, .amt = 10, .delegate = auditor});
        mptAlice.convertBack({.account = bob, .amt = 10, .delegate = auditor});
    }

    // Verifies that a non-issuer delegating clawback to a third party does not
    // allow that party to execute clawback, since clawback is issuer-only.
    void
    testDelegationClawbackIssuerOnly(FeatureBitset features)
    {
        testcase("Confidential clawback delegation requires issuer");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const dave{"dave"};

        MPTTester mptAlice(env, alice, {.holders = {bob, carol}});
        env.fund(XRP(10000), dave);
        env.close();

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanClawback | tfMPTCanConfidentialAmount,
        });
        mptAlice.authorize({.account = bob});
        mptAlice.authorize({.account = carol});
        mptAlice.pay(alice, bob, 100);
        mptAlice.pay(alice, carol, 100);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);
        mptAlice.set({.issuerPubKey = mptAlice.getPubKey(alice)});

        mptAlice.convert({
            .account = bob,
            .amt = 50,
            .holderPubKey = mptAlice.getPubKey(bob),
        });
        mptAlice.mergeInbox({.account = bob});

        mptAlice.convert({
            .account = carol,
            .amt = 100,
            .holderPubKey = mptAlice.getPubKey(carol),
        });
        mptAlice.mergeInbox({.account = carol});

        // Bob delegates Clawback permission to dave.
        env(delegate::set(bob, dave, {"ConfidentialMPTClawback"}));
        env.close();

        // Dave attempts clawback on behalf of bob targetting bob, but since bob is not the issuer,
        // the transaction should be rejected.
        {
            Json::Value jv;
            jv[jss::Account] = bob.human();
            jv[jss::TransactionType] = jss::ConfidentialMPTClawback;
            jv[sfMPTokenIssuanceID] = to_string(mptAlice.issuanceID());
            jv[sfHolder] = bob.human();
            jv[sfMPTAmount.jsonName] = "50";
            jv[sfZKProof.jsonName] = std::string(ecEqualityProofLength * 2, '0');
            env(jv, delegate::as(dave), ter(temMALFORMED));
        }

        // Dave attempts clawback on behalf of bob targeting carol, but since bob is not the issuer,
        // the transaction should be rejected.
        {
            Json::Value jv;
            jv[jss::Account] = bob.human();
            jv[jss::TransactionType] = jss::ConfidentialMPTClawback;
            jv[sfMPTokenIssuanceID] = to_string(mptAlice.issuanceID());
            jv[sfHolder] = carol.human();
            jv[sfMPTAmount.jsonName] = "100";
            jv[sfZKProof.jsonName] = std::string(ecEqualityProofLength * 2, '0');
            env(jv, delegate::as(dave), ter(temMALFORMED));
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

        testSet(features);
        testSetPreflight(features);
        testSetPreclaim(features);

        // ConfidentialMPTSend
        testSend(features);
        testSendPreflight(features);
        testSendPreclaim(features);
        testSendRangeProof(features);
        // testSendZeroAmount(features);
        testSendDepositPreauth(features);
        testSendCredentialValidation(features);
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

        // Homomorphic operation tests
        testSendHomomorphicOverflow(features);
        testHomomorphicCiphertextModification(features);
        testConvertBackHomomorphicUnderflow(features);

        testSendWrongIssuerPublicKey(features);

        // Replay Tests
        testMutatePrivacy(features);
        testProofContextBinding(features);
        testProofCiphertextBinding(features);
        testProofVersionMismatch(features);

        // Ticket Tests
        testWithTickets(features);
        testConvertTicketProofBinding(features);
        testTicketErrors(features);

        // Delegation Tests
        testConfidentialDelegation(features);
        testDelegationRevocation(features);
        testDelegationWithAuditor(features);
        testDelegationClawbackIssuerOnly(features);
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
