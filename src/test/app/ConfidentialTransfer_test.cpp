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
    getTrivialSendProofHex()
    {
        Buffer buf(ecSendProofLength);
        std::memset(buf.data(), 0, ecSendProofLength);

        for (std::size_t i = 0; i < ecSendProofLength; i += ecGamalEncryptedLength)
        {
            buf.data()[i] = ecCompressedPrefixEvenY;
            if (i + ecGamalEncryptedLength - 1 < ecSendProofLength)
                buf.data()[i + ecGamalEncryptedLength - 1] = 0x01;
        }

        return strHex(buf);
    }

    // Helper struct to encapsulate common setup for ZKP integration tests.
    struct ZkpTestSetup
    {
        // Constants
        uint64_t sendAmount;
        size_t nRecipients;
        uint32_t version;

        // Blinding factors
        Buffer blindingFactor;
        Buffer amountBlindingFactor;
        Buffer balanceBlindingFactor;

        // Encrypted amounts
        Buffer senderAmt;
        Buffer destAmt;
        Buffer issuerAmt;
        std::optional<Buffer> auditorAmt;

        // Commitments
        Buffer amountCommitment;

        // Long-lived pub key buffers (to avoid dangling Slice)
        Buffer senderPubKey;
        Buffer destPubKey;
        Buffer issuerPubKey;
        std::optional<Buffer> auditorPubKey;

        // Balance data
        uint64_t prevSpending;
        Buffer prevEncryptedSpending;

        // Balance commitment (declared after prevSpending for init order)
        Buffer balanceCommitment;

        // Recipients vector
        std::vector<ConfidentialRecipient> recipients;

        // Constructor that performs all common setup
        ZkpTestSetup(
            test::jtx::MPTTester& mpt,
            test::jtx::Account const& sender,
            test::jtx::Account const& dest,
            test::jtx::Account const& issuer,
            uint64_t amount,
            std::optional<std::reference_wrapper<test::jtx::Account const>> auditor = std::nullopt)
            : sendAmount(amount)
            , nRecipients(auditor ? 4 : 3)
            , version(mpt.getMPTokenVersion(sender))
            , blindingFactor(generateBlindingFactor())
            , amountBlindingFactor(generateBlindingFactor())
            , balanceBlindingFactor(generateBlindingFactor())
            , senderAmt(mpt.encryptAmount(sender, amount, blindingFactor))
            , destAmt(mpt.encryptAmount(dest, amount, blindingFactor))
            , issuerAmt(mpt.encryptAmount(issuer, amount, blindingFactor))
            , auditorAmt(
                  auditor ? std::optional<Buffer>(
                                mpt.encryptAmount(auditor->get(), amount, blindingFactor))
                          : std::nullopt)
            , amountCommitment(mpt.getPedersenCommitment(amount, amountBlindingFactor))
            , senderPubKey(*mpt.getPubKey(sender))
            , destPubKey(*mpt.getPubKey(dest))
            , issuerPubKey(*mpt.getPubKey(issuer))
            , auditorPubKey(
                  auditor ? std::optional<Buffer>(*mpt.getPubKey(auditor->get())) : std::nullopt)
            , prevSpending(
                  *mpt.getDecryptedBalance(sender, test::jtx::MPTTester::HOLDER_ENCRYPTED_SPENDING))
            , prevEncryptedSpending(
                  *mpt.getEncryptedBalance(sender, test::jtx::MPTTester::HOLDER_ENCRYPTED_SPENDING))
            , balanceCommitment(mpt.getPedersenCommitment(prevSpending, balanceBlindingFactor))
        {
            recipients.push_back({Slice(senderPubKey), senderAmt});
            recipients.push_back({Slice(destPubKey), destAmt});
            recipients.push_back({Slice(issuerPubKey), issuerAmt});
            if (auditor)
                recipients.push_back({Slice(*auditorPubKey), *auditorAmt});
        }

        // Generate proof with current account sequence
        std::optional<Buffer>
        generateProof(
            test::jtx::MPTTester& mpt,
            test::jtx::Env& env,
            test::jtx::Account const& sender,
            test::jtx::Account const& dest) const
        {
            auto const ctxHash = getSendContextHash(
                sender.id(), mpt.issuanceID(), env.seq(sender), dest.id(), version);

            return mpt.getConfidentialSendProof(
                sender,
                sendAmount,
                recipients,
                blindingFactor,
                nRecipients,
                ctxHash,
                {.pedersenCommitment = amountCommitment,
                 .amt = sendAmount,
                 .encryptedAmt = senderAmt,
                 .blindingFactor = amountBlindingFactor},
                {.pedersenCommitment = balanceCommitment,
                 .amt = prevSpending,
                 .encryptedAmt = prevEncryptedSpending,
                 .blindingFactor = balanceBlindingFactor});
        }
    };

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
                .proof = getTrivialSendProofHex(),
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
                .proof = getTrivialSendProofHex(),
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
                .proof = getTrivialSendProofHex(),
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
                .proof = getTrivialSendProofHex(),
                .amountCommitment = makeZeroBuffer(100),
                .balanceCommitment = getTrivialCommitment(),
                .err = temMALFORMED,
            });

            // invalid balance Pedersen commitment length
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(),
                .amountCommitment = getTrivialCommitment(),
                .balanceCommitment = makeZeroBuffer(100),
                .err = temMALFORMED,
            });

            // amount Pedersen commitment has correct length but invalid EC point data
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(),
                .amountCommitment = makeZeroBuffer(ecPedersenCommitmentLength),
                .balanceCommitment = getTrivialCommitment(),
                .err = temMALFORMED,
            });

            // balance Pedersen commitment has correct length but invalid EC point data
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(),
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
                .proof = getTrivialSendProofHex(),
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
                .proof = getTrivialSendProofHex(),
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
            jv[sfZKProof] = getTrivialSendProofHex();

            env(jv, ter(tecOBJECT_NOT_FOUND));
        }

        // destination does not exist
        {
            Account const unknown("unknown");
            mptAlice.send({
                .account = bob,
                .dest = unknown,
                .amt = 10,
                .proof = getTrivialSendProofHex(),
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
                .proof = getTrivialSendProofHex(),
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
                .proof = getTrivialSendProofHex(),
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
                .proof = getTrivialSendProofHex(),
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
                .proof = getTrivialSendProofHex(),
                .err = tecBAD_PROOF,
            });
        }

        // No Auditor key set, but auditor encrypted amt provided
        {
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(),
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
                .proof = getTrivialSendProofHex(),
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
            .proof = getTrivialSendProofHex(),
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
            .proof = getTrivialSendProofHex(),
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
            PrettyAsset const asset = mptt.issuanceID();
            mptt.authorize({.account = owner});
            mptt.authorize({.account = depositor});
            env(pay(issuer, depositor, asset(1000)));
            env.close();

            test::jtx::Vault const vault{env};
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
            tooManyCredentials.reserve(9);
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
            std::string const dummyProof(ecClawbackProofLength * 2, '0');
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

        // These tests verify that the compact ConvertBack proof validation
        // correctly rejects proofs generated with incorrect parameters.
        // The compact proof simultaneously verifies balance ownership,
        // commitment linkage, and that remaining balance is non-negative.

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
            uint256 const badContextHash{1};

            Buffer const proof = mptAlice.getConvertBackProof(
                bob,
                amt,
                badContextHash,  // wrong context hash
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

        // These tests verify that the compact ConvertBack proof (sigma + bulletproof)
        // correctly rejects proofs generated with incorrect parameters.
        // The compact proof simultaneously verifies balance ownership, commitment
        // linkage, and that the remaining balance is non-negative.

        // Test 1: Proof generated with wrong balance value.
        // The sigma proof claims balance=1 but the spending balance contains the
        // actual balance. The compact proof's balance-linkage check fails.
        {
            uint256 const contextHash =
                getConvertBackContextHash(bob, mptAlice.issuanceID(), env.seq(bob), version);

            Buffer const proof = mptAlice.getConvertBackProof(
                bob,
                amt,
                contextHash,
                {
                    .pedersenCommitment = pedersenCommitment,
                    .amt = 1,  // wrong balance (actual balance is ~40)
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
        // The compact sigma proof must use the same blinding factor (rho) as the
        // Pedersen commitment PC = balance*G + rho*H. Using a different rho
        // creates an inconsistency the verifier detects.
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

        // Test 3: Proof generated with wrong context hash.
        // The context hash binds the proof to a specific transaction (account,
        // sequence, issuanceID, amount, version). Using a different context hash
        // makes the proof invalid for this transaction, preventing replay attacks.
        {
            uint256 const badContextHash{1};
            Buffer const proof = mptAlice.getConvertBackProof(
                bob,
                amt,
                badContextHash,  // wrong context hash
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

    /* This test verifies that xrpld correctly rejects attempts to
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

    // Confidential sends carry encrypted amounts and a zero-knowledge proof.
    // Both are built from elliptic-curve math, so every coordinate in the
    // transaction must be a real point on the secp256k1 curve. These three
    // variants confirm the validator rejects garbage coordinates at the right
    // stage before any expensive cryptographic verification runs.
    void
    testSendInvalidCurvePoints(FeatureBitset features)
    {
        testcase("Send: off-curve EC points");
        using namespace test::jtx;

        // Variant A: garbage coordinate in ciphertext / commitment fields
        // getBadCiphertext() looks structurally valid (correct length, right
        // prefix byte 0x02) but its x-coordinate is 0xFF...FF, which does not
        // lie on secp256k1. Preflight must reject before any ledger access.
        {
            Env env{*this, features};
            Account const alice("alice"), bob("bob"), carol("carol");
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
            mptAlice.convert({.account = bob, .amt = 60, .holderPubKey = mptAlice.getPubKey(bob)});
            mptAlice.convert(
                {.account = carol, .amt = 30, .holderPubKey = mptAlice.getPubKey(carol)});

            // sender's encrypted amount has an invalid coordinate
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(),
                .senderEncryptedAmt = getBadCiphertext(),
                .amountCommitment = getTrivialCommitment(),
                .balanceCommitment = getTrivialCommitment(),
                .err = temBAD_CIPHERTEXT,
            });

            // recipient's encrypted amount has an invalid coordinate
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(),
                .destEncryptedAmt = getBadCiphertext(),
                .amountCommitment = getTrivialCommitment(),
                .balanceCommitment = getTrivialCommitment(),
                .err = temBAD_CIPHERTEXT,
            });

            // issuer's encrypted amount has an invalid coordinate
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(),
                .issuerEncryptedAmt = getBadCiphertext(),
                .amountCommitment = getTrivialCommitment(),
                .balanceCommitment = getTrivialCommitment(),
                .err = temBAD_CIPHERTEXT,
            });

            // The amount and balance commitments are single curve coordinates
            // used to tie the proof to the transfer amount and sender balance.
            // A commitment with a valid-looking prefix but an impossible
            // x-coordinate must also be rejected.
            Buffer badCommitment(ecPedersenCommitmentLength);
            std::memset(badCommitment.data(), 0xFF, ecPedersenCommitmentLength);
            badCommitment.data()[0] = ecCompressedPrefixEvenY;

            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(),
                .amountCommitment = badCommitment,
                .balanceCommitment = getTrivialCommitment(),
                .err = temMALFORMED,
            });

            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(),
                .amountCommitment = getTrivialCommitment(),
                .balanceCommitment = badCommitment,
                .err = temMALFORMED,
            });
        }

        // Variant B: garbage coordinates inside the ZKP proof blob
        // The proof blob has the right total byte length (so it passes the
        // length check at preflight), but every embedded coordinate is
        // 0xFF...FF — impossible on secp256k1. The proof verifier must detect
        // this and return tecBAD_PROOF without crashing.
        {
            Env env{*this, features};
            Account const alice("alice"), bob("bob"), carol("carol");
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
            mptAlice.convert({.account = bob, .amt = 60, .holderPubKey = mptAlice.getPubKey(bob)});
            mptAlice.mergeInbox({.account = bob});
            mptAlice.convert(
                {.account = carol, .amt = 30, .holderPubKey = mptAlice.getPubKey(carol)});
            mptAlice.mergeInbox({.account = carol});

            Buffer badProof(ecSendProofLength);
            std::memset(badProof.data(), 0xFF, ecSendProofLength);
            badProof.data()[0] = ecCompressedPrefixEvenY;

            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = strHex(badProof),
                .err = tecBAD_PROOF,
            });
        }

        // Variant C: only one of the two ciphertext coordinates is bad
        // Each encrypted amount is two coordinates back-to-back: C1 then C2.
        // Both must be valid. These tests corrupt only one at a time to
        // confirm both are checked independently.
        {
            Env env{*this, features};
            Account const alice("alice"), bob("bob"), carol("carol");
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
            mptAlice.convert({.account = bob, .amt = 60, .holderPubKey = mptAlice.getPubKey(bob)});
            mptAlice.convert(
                {.account = carol, .amt = 30, .holderPubKey = mptAlice.getPubKey(carol)});

            // getTrivialCiphertext() has both C1 and C2 as valid (but trivial)
            // curve coordinates.  We replace one half at a time with 0xFF...FF.
            auto const& tc = getTrivialCiphertext();

            // C1 = bad (0xFF...FF), C2 = valid trivial point
            Buffer badC1goodC2(ecGamalEncryptedTotalLength);
            std::memset(badC1goodC2.data(), 0xFF, ecGamalEncryptedTotalLength);
            badC1goodC2.data()[0] = ecCompressedPrefixEvenY;
            std::memcpy(
                badC1goodC2.data() + ecGamalEncryptedLength,
                tc.data() + ecGamalEncryptedLength,
                ecGamalEncryptedLength);

            // C1 = valid trivial point, C2 = bad (0xFF...FF)
            Buffer goodC1badC2(ecGamalEncryptedTotalLength);
            std::memset(goodC1badC2.data(), 0xFF, ecGamalEncryptedTotalLength);
            std::memcpy(goodC1badC2.data(), tc.data(), ecGamalEncryptedLength);
            goodC1badC2.data()[ecGamalEncryptedLength] = ecCompressedPrefixEvenY;

            // sender's encrypted amount — bad C1
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(),
                .senderEncryptedAmt = badC1goodC2,
                .amountCommitment = getTrivialCommitment(),
                .balanceCommitment = getTrivialCommitment(),
                .err = temBAD_CIPHERTEXT,
            });

            // sender's encrypted amount — bad C2
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(),
                .senderEncryptedAmt = goodC1badC2,
                .amountCommitment = getTrivialCommitment(),
                .balanceCommitment = getTrivialCommitment(),
                .err = temBAD_CIPHERTEXT,
            });

            // recipient's encrypted amount — bad C1
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(),
                .destEncryptedAmt = badC1goodC2,
                .amountCommitment = getTrivialCommitment(),
                .balanceCommitment = getTrivialCommitment(),
                .err = temBAD_CIPHERTEXT,
            });

            // recipient's encrypted amount — bad C2
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(),
                .destEncryptedAmt = goodC1badC2,
                .amountCommitment = getTrivialCommitment(),
                .balanceCommitment = getTrivialCommitment(),
                .err = temBAD_CIPHERTEXT,
            });
        }
    }

    // Reject points from the wrong elliptic curve (wrong-group injection).
    //
    // An attacker might submit coordinates that come from a completely
    // different elliptic curve, for example, the one used in TLS
    // certificates (NIST P-256). If those coordinates happen to also be
    // valid points on secp256k1 (which is possible since both curves use
    // 256-bit fields), the format check at preflight will pass. However,
    // the zero-knowledge proof is built specifically for secp256k1: the
    // math inside the proof only holds for the right curve, so any
    // transaction carrying cross-curve data will still be rejected at
    // proof verification (tecBAD_PROOF).
    void
    testSendWrongGroupPointInjection(FeatureBitset features)
    {
        testcase("Send: wrong-group point injection rejected");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice"), bob("bob"), carol("carol");
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
        mptAlice.convert({.account = bob, .amt = 60, .holderPubKey = mptAlice.getPubKey(bob)});
        mptAlice.mergeInbox({.account = bob});
        mptAlice.convert({.account = carol, .amt = 30, .holderPubKey = mptAlice.getPubKey(carol)});
        mptAlice.mergeInbox({.account = carol});

        // The x-coordinate of the NIST P-256 generator point — a real,
        // well-known value from a different elliptic curve (used in TLS
        // and certificates).  This x-coordinate is also a valid secp256k1
        // point, so it passes preflight.  Rejection happens at proof
        // verification because the ZKP is secp256k1-specific.
        //
        //   P-256 generator x:
        //     6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296
        static constexpr std::uint8_t kP256GeneratorX[32] = {
            0x6B, 0x17, 0xD1, 0xF2, 0xE1, 0x2C, 0x42, 0x47, 0xF8, 0xBC, 0xE6,
            0xE5, 0x63, 0xA4, 0x40, 0xF2, 0x77, 0x03, 0x7D, 0x81, 0x2D, 0xEB,
            0x33, 0xA0, 0xF4, 0xA1, 0x39, 0x45, 0xD8, 0x98, 0xC2, 0x96,
        };

        // A 66-byte encrypted amount using the P-256 x-coordinate for both halves.
        Buffer wrongGroupCt(ecGamalEncryptedTotalLength);
        wrongGroupCt.data()[0] = ecCompressedPrefixEvenY;
        std::memcpy(wrongGroupCt.data() + 1, kP256GeneratorX, 32);
        wrongGroupCt.data()[ecGamalEncryptedLength] = ecCompressedPrefixEvenY;
        std::memcpy(wrongGroupCt.data() + ecGamalEncryptedLength + 1, kP256GeneratorX, 32);

        // A 33-byte commitment using the same wrong-curve x-coordinate.
        Buffer wrongGroupCommitment(ecPedersenCommitmentLength);
        wrongGroupCommitment.data()[0] = ecCompressedPrefixEvenY;
        std::memcpy(wrongGroupCommitment.data() + 1, kP256GeneratorX, 32);

        // sender's encrypted amount uses a coordinate from the wrong curve
        mptAlice.send({
            .account = bob,
            .dest = carol,
            .amt = 10,
            .proof = getTrivialSendProofHex(),
            .senderEncryptedAmt = wrongGroupCt,
            .amountCommitment = getTrivialCommitment(),
            .balanceCommitment = getTrivialCommitment(),
            .err = tecBAD_PROOF,
        });

        // recipient's encrypted amount uses a coordinate from the wrong curve
        mptAlice.send({
            .account = bob,
            .dest = carol,
            .amt = 10,
            .proof = getTrivialSendProofHex(),
            .destEncryptedAmt = wrongGroupCt,
            .amountCommitment = getTrivialCommitment(),
            .balanceCommitment = getTrivialCommitment(),
            .err = tecBAD_PROOF,
        });

        // issuer's encrypted amount uses a coordinate from the wrong curve
        mptAlice.send({
            .account = bob,
            .dest = carol,
            .amt = 10,
            .proof = getTrivialSendProofHex(),
            .issuerEncryptedAmt = wrongGroupCt,
            .amountCommitment = getTrivialCommitment(),
            .balanceCommitment = getTrivialCommitment(),
            .err = tecBAD_PROOF,
        });

        // amount commitment uses a coordinate from the wrong curve
        mptAlice.send({
            .account = bob,
            .dest = carol,
            .amt = 10,
            .proof = getTrivialSendProofHex(),
            .amountCommitment = wrongGroupCommitment,
            .balanceCommitment = getTrivialCommitment(),
            .err = tecBAD_PROOF,
        });

        // balance commitment uses a coordinate from the wrong curve
        mptAlice.send({
            .account = bob,
            .dest = carol,
            .amt = 10,
            .proof = getTrivialSendProofHex(),
            .amountCommitment = getTrivialCommitment(),
            .balanceCommitment = wrongGroupCommitment,
            .err = tecBAD_PROOF,
        });
    }

    // Reject an all-zero "null" public key.
    //
    // Every account in a confidential transfer needs a real public key —
    // a specific point on the secp256k1 curve derived from a secret number
    // only that account knows. An all-zero key (33 bytes of 0x00) is not
    // a real key. It has no secret behind it, and encrypting data to it
    // would not actually hide anything. The validator must reject it at
    // preflight so no account can ever register a broken key.
    void
    testIdentityElementRejection(FeatureBitset features)
    {
        testcase("Send: all-zero public key rejected");
        using namespace test::jtx;

        // 33 zero bytes — not a real public key; no valid secret maps to this.
        Buffer const nullKey = makeZeroBuffer(ecPubKeyLength);

        // Recipient (holder) tries to register an all-zero key.
        // Must be rejected so no account ends up with an unprotected balance.
        {
            Env env{*this, features};
            Account const alice("alice"), bob("bob"), carol("carol");
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

            // recipient (carol) tries to register an all-zero key
            mptAlice.convert({
                .account = carol,
                .amt = 10,
                .holderPubKey = nullKey,
                .err = temMALFORMED,
            });

            // sender (bob) tries to register an all-zero key
            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .holderPubKey = nullKey,
                .err = temMALFORMED,
            });
        }

        // Issuer tries to register an all-zero key.
        // The issuer's key is used to encrypt the issuer's copy of every
        // transfer amount.
        {
            Env env{*this, features};
            Account const alice("alice"), bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});
            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
            });
            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);
            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);

            mptAlice.set({
                .account = alice,
                .issuerPubKey = nullKey,
                .err = temMALFORMED,
            });
        }
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

    // Set up an MPT environment suitable for batch testing.
    // alice is issuer; bob has 'bobAmt' in confidential spending; carol has
    // 'carolAmt' in confidential spending; dave is initialised with pubkey but
    // zero spending/inbox.
    void
    setupBatchEnv(
        test::jtx::MPTTester& mpt,
        test::jtx::Account const& alice,
        test::jtx::Account const& bob,
        test::jtx::Account const& carol,
        test::jtx::Account const& dave,
        std::uint64_t bobAmt,
        std::uint64_t carolAmt)
    {
        using namespace test::jtx;
        mpt.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanConfidentialAmount,
        });
        mpt.authorize({.account = bob});
        mpt.authorize({.account = carol});
        mpt.authorize({.account = dave});

        if (bobAmt > 0)
            mpt.pay(alice, bob, bobAmt);
        if (carolAmt > 0)
            mpt.pay(alice, carol, carolAmt);

        mpt.generateKeyPair(alice);
        mpt.generateKeyPair(bob);
        mpt.generateKeyPair(carol);
        mpt.generateKeyPair(dave);

        mpt.set({.account = alice, .issuerPubKey = mpt.getPubKey(alice)});

        if (bobAmt > 0)
        {
            mpt.convert({.account = bob, .amt = bobAmt, .holderPubKey = mpt.getPubKey(bob)});
            mpt.mergeInbox({.account = bob});
        }
        else
        {
            mpt.convert({.account = bob, .amt = 0, .holderPubKey = mpt.getPubKey(bob)});
        }

        if (carolAmt > 0)
        {
            mpt.convert({.account = carol, .amt = carolAmt, .holderPubKey = mpt.getPubKey(carol)});
            mpt.mergeInbox({.account = carol});
        }
        else
        {
            mpt.convert({.account = carol, .amt = 0, .holderPubKey = mpt.getPubKey(carol)});
        }

        // dave: register pubkey only (0 spending/inbox)
        mpt.convert({.account = dave, .amt = 0, .holderPubKey = mpt.getPubKey(dave)});
    }

    // Bob sends 100 MPT to Carol. Carol Merge Inbox. Carol sends 50 MPT to Dave.
    // Inner 3rd txn (Carol sends to Dave) fails because the proof is built with
    // when Carols's spending balance is 0. (before she received funds from Bob)
    //
    // Also tests Bob sending to two recipients (Carol and Dave) in a single
    // batch. Even though Bob has enough balance for both, the second send's
    // balance-linkage proof becomes incorrect once inner 1 updates Bob's encrypted
    // spending, so fails
    void
    testBatchConfidentialSend(FeatureBitset features)
    {
        testcase("Batch confidential send - merge inbox dependency");
        using namespace test::jtx;

        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const dave("dave");

            MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
            // bob = A (100 spending), carol = B (0), dave = C (0)
            setupBatchEnv(mpt, alice, bob, carol, dave, 100, 0);

            // Build the batch:
            //   Batch Txn 1 bob -> carol 100 : valid proof, bob spending=100
            //   Batch Txn 2 carol -> mergeInbox : valid JV
            //   Batch Txn 3 carol->dave 50  : Invalid
            auto const bobSeq = env.seq(bob);
            auto const carolSeq = env.seq(carol);
            // 3 signers, Bob, Carol, Dave
            auto const batchFee = batch::calcBatchFee(env, 1, 3);

            auto const jv1 = mpt.sendJV({.account = bob, .dest = carol, .amt = 100}, bobSeq + 1);
            auto const jv2 = mpt.mergeInboxJV({.account = carol});
            auto const jv3 = mpt.sendJV({.account = carol, .dest = dave, .amt = 50}, carolSeq + 1);

            env(batch::outer(bob, bobSeq, batchFee, tfAllOrNothing),
                batch::inner(jv1, bobSeq + 1),
                batch::inner(jv2, carolSeq),
                batch::inner(jv3, carolSeq + 1),
                batch::sig(carol),
                ter(tesSUCCESS));
            env.close();

            // AllOrNothing: inner 3 fails
            // bob's spending must remain 100; carol's inbox must remain 0.
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 100);
            BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::HOLDER_ENCRYPTED_INBOX) == 0);
        }

        // Bob sends to two recipients (Carol and Dave) in one batch.
        // Bob has 150, enough for both sends individually.  However, batch txn 1
        // changes Bob's encrypted spending on the ledger; batch txn 2 was built
        // against the old enc(150) so its balance-linkage proof is stale.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const dave("dave");

            MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
            setupBatchEnv(mpt, alice, bob, carol, dave, 150, 0);

            // tfAllOrNothing — rejects the whole batch as 2nd txn proof is incorrect
            {
                auto const bobSeq = env.seq(bob);
                auto const batchFee = batch::calcBatchFee(env, 0, 2);

                auto const jv1 = mpt.sendJV({.account = bob, .dest = carol, .amt = 50}, bobSeq + 1);
                auto const jv2 = mpt.sendJV({.account = bob, .dest = dave, .amt = 60}, bobSeq + 2);

                env(batch::outer(bob, bobSeq, batchFee, tfAllOrNothing),
                    batch::inner(jv1, bobSeq + 1),
                    batch::inner(jv2, bobSeq + 2),
                    ter(tesSUCCESS));
                env.close();

                // Nothing applied: bob stays 150, carol and dave inbox stay 0.
                BEAST_EXPECT(
                    mpt.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 150);
                BEAST_EXPECT(
                    mpt.getDecryptedBalance(carol, MPTTester::HOLDER_ENCRYPTED_INBOX) == 0);
                BEAST_EXPECT(mpt.getDecryptedBalance(dave, MPTTester::HOLDER_ENCRYPTED_INBOX) == 0);
            }

            // If we change batch mode to be tfIndependent — txn 1 applies, inner 2 fails.
            {
                auto const bobSeq = env.seq(bob);
                auto const batchFee = batch::calcBatchFee(env, 0, 2);

                auto const jv1 = mpt.sendJV({.account = bob, .dest = carol, .amt = 50}, bobSeq + 1);
                auto const jv2 = mpt.sendJV({.account = bob, .dest = dave, .amt = 60}, bobSeq + 2);

                env(batch::outer(bob, bobSeq, batchFee, tfIndependent),
                    batch::inner(jv1, bobSeq + 1),
                    batch::inner(jv2, bobSeq + 2),
                    ter(tesSUCCESS));
                env.close();

                // bob 150→100, carol inbox 0→50
                BEAST_EXPECT(
                    mpt.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 100);
                BEAST_EXPECT(
                    mpt.getDecryptedBalance(carol, MPTTester::HOLDER_ENCRYPTED_INBOX) == 50);
                // dave gets nothing
                BEAST_EXPECT(mpt.getDecryptedBalance(dave, MPTTester::HOLDER_ENCRYPTED_INBOX) == 0);
            }
        }

        // Now, Bob sends Confidential MPT to 2 accounts in one batch.
        // However this time, the second txn proof is calculated using the
        // correct encrypted(spending) proof, so it should pass.
        {
            // bob has exactly enough for both sends.
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const dave("dave");

            MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
            setupBatchEnv(mpt, alice, bob, carol, dave, 200, 0);

            {
                auto const bobSeq = env.seq(bob);
                auto const batchFee = batch::calcBatchFee(env, 0, 2);

                // jv1 is built against the current ledger state (spending=200).
                auto const jv1 =
                    mpt.sendJV({.account = bob, .dest = carol, .amt = 100}, bobSeq + 1);

                // Compute post-jv1 state without touching the ledger.
                auto const chain1 = mpt.chainAfterSend(bob, 100, jv1);

                // jv2 proof is built against predicted spending=100, version=N+1.
                auto const jv2 =
                    mpt.sendJV({.account = bob, .dest = dave, .amt = 100}, bobSeq + 2, chain1);

                env(batch::outer(bob, bobSeq, batchFee, tfAllOrNothing),
                    batch::inner(jv1, bobSeq + 1),
                    batch::inner(jv2, bobSeq + 2),
                    ter(tesSUCCESS));
                env.close();

                // Both txns applied: bob 200→0, carol inbox=100, dave inbox=100.
                BEAST_EXPECT(
                    mpt.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 0);
                BEAST_EXPECT(
                    mpt.getDecryptedBalance(carol, MPTTester::HOLDER_ENCRYPTED_INBOX) == 100);
                BEAST_EXPECT(
                    mpt.getDecryptedBalance(dave, MPTTester::HOLDER_ENCRYPTED_INBOX) == 100);
            }

            // Now Bob has 150, but triees to send two 100 in one batch.
            // This fails because Bob doesn't have enough MPT balance.
            {
                Env env2{*this, features};
                Account const alice2("alice");
                Account const bob2("bob");
                Account const carol2("carol");
                Account const dave2("dave");

                MPTTester mpt2(env2, alice2, {.holders = {bob2, carol2, dave2}});
                setupBatchEnv(mpt2, alice2, bob2, carol2, dave2, 150, 0);

                auto const bobSeq = env2.seq(bob2);
                auto const batchFee = batch::calcBatchFee(env2, 0, 2);

                auto const jv1 =
                    mpt2.sendJV({.account = bob2, .dest = carol2, .amt = 100}, bobSeq + 1);
                auto const chain1 = mpt2.chainAfterSend(bob2, 100, jv1);

                auto const jv2 =
                    mpt2.sendJV({.account = bob2, .dest = dave2, .amt = 100}, bobSeq + 2, chain1);

                env2(
                    batch::outer(bob2, bobSeq, batchFee, tfAllOrNothing),
                    batch::inner(jv1, bobSeq + 1),
                    batch::inner(jv2, bobSeq + 2),
                    ter(tesSUCCESS));
                env2.close();

                // AllOrNothing: inner 2 fails → nothing applied.
                BEAST_EXPECT(
                    mpt2.getDecryptedBalance(bob2, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 150);
                BEAST_EXPECT(
                    mpt2.getDecryptedBalance(carol2, MPTTester::HOLDER_ENCRYPTED_INBOX) == 0);
                BEAST_EXPECT(
                    mpt2.getDecryptedBalance(dave2, MPTTester::HOLDER_ENCRYPTED_INBOX) == 0);
            }
        }
    }

    void
    testBatchAllOrNothing(FeatureBitset features)
    {
        testcase("Batch confidential MPT - all or nothing");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const dave("dave");

        MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
        // bob=100 spending, carol=60 spending, dave=0
        setupBatchEnv(mpt, alice, bob, carol, dave, 100, 60);

        // bob sends dave 10, carol sends dave 5, independent, both valid.
        {
            auto const bobSeq = env.seq(bob);
            auto const carolSeq = env.seq(carol);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);

            auto const jv1 = mpt.sendJV({.account = bob, .dest = dave, .amt = 10}, bobSeq + 1);
            auto const jv2 = mpt.sendJV({.account = carol, .dest = dave, .amt = 5}, carolSeq);

            env(batch::outer(bob, bobSeq, batchFee, tfAllOrNothing),
                batch::inner(jv1, bobSeq + 1),
                batch::inner(jv2, carolSeq),
                batch::sig(carol),
                ter(tesSUCCESS));
            env.close();

            // Both txn applied: bob's balance 100→90, carol 60→55, dave inbox 0→15
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 90);
            BEAST_EXPECT(
                mpt.getDecryptedBalance(carol, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 55);
            BEAST_EXPECT(mpt.getDecryptedBalance(dave, MPTTester::HOLDER_ENCRYPTED_INBOX) == 15);
        }
    }

    void
    testBatchOnlyOne(FeatureBitset features)
    {
        testcase("Batch confidential MPT - only one");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const dave("dave");

        MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
        // bob=100 spending, carol=60 spending, dave=0
        setupBatchEnv(mpt, alice, bob, carol, dave, 100, 60);

        // bob sends dave 200 (invalid), carol sends dave 300 (invalid)
        {
            auto const bobSeq = env.seq(bob);
            auto const carolSeq = env.seq(carol);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);

            // Both proofs fail range check (amount > balance)
            auto const jv1 = mpt.sendJV({.account = bob, .dest = dave, .amt = 200}, bobSeq + 1);
            auto const jv2 = mpt.sendJV({.account = carol, .dest = dave, .amt = 300}, carolSeq);

            env(batch::outer(bob, bobSeq, batchFee, tfOnlyOne),
                batch::inner(jv1, bobSeq + 1),
                batch::inner(jv2, carolSeq),
                batch::sig(carol),
                ter(tesSUCCESS));
            env.close();

            // No success found → nothing applied; balances unchanged
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 100);
            BEAST_EXPECT(
                mpt.getDecryptedBalance(carol, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 60);
            BEAST_EXPECT(mpt.getDecryptedBalance(dave, MPTTester::HOLDER_ENCRYPTED_INBOX) == 0);
        }

        // bob sends dave 200 (invalid), carol sends dave 5 (valid)
        {
            auto const bobSeq = env.seq(bob);
            auto const carolSeq = env.seq(carol);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);

            auto jv1 = mpt.sendJV({.account = bob, .dest = dave, .amt = 200}, bobSeq + 1);
            auto jv2 = mpt.sendJV({.account = carol, .dest = dave, .amt = 5}, carolSeq);

            env(batch::outer(bob, bobSeq, batchFee, tfOnlyOne),
                batch::inner(jv1, bobSeq + 1),
                batch::inner(jv2, carolSeq),
                batch::sig(carol),
                ter(tesSUCCESS));
            env.close();

            // Only carol's send applied: carol 60→55, dave inbox 0→5, bob unchanged
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 100);
            BEAST_EXPECT(
                mpt.getDecryptedBalance(carol, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 55);
            BEAST_EXPECT(mpt.getDecryptedBalance(dave, MPTTester::HOLDER_ENCRYPTED_INBOX) == 5);
        }
    }

    void
    testBatchUntilFailure(FeatureBitset features)
    {
        testcase("Batch confidential MPT - until failure");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const dave("dave");

        MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
        // bob=100 spending, carol=60 spending, dave=0
        setupBatchEnv(mpt, alice, bob, carol, dave, 100, 60);

        // first fails → none applied
        // Bob sends Dave 200 (invalid — stops immediately)
        {
            auto const bobSeq = env.seq(bob);
            auto const carolSeq = env.seq(carol);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);

            auto const jv1 = mpt.sendJV({.account = bob, .dest = dave, .amt = 200}, bobSeq + 1);
            auto const jv2 = mpt.sendJV({.account = carol, .dest = dave, .amt = 5}, carolSeq);

            env(batch::outer(bob, bobSeq, batchFee, tfUntilFailure),
                batch::inner(jv1, bobSeq + 1),
                batch::inner(jv2, carolSeq),
                batch::sig(carol),
                ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 100);
            BEAST_EXPECT(
                mpt.getDecryptedBalance(carol, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 60);
        }

        // Bob sends dave 10, Carol sends dave 5 — both valid and independent
        {
            auto const bobSeq = env.seq(bob);
            auto const carolSeq = env.seq(carol);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);

            auto const jv1 = mpt.sendJV({.account = bob, .dest = dave, .amt = 10}, bobSeq + 1);
            auto const jv2 = mpt.sendJV({.account = carol, .dest = dave, .amt = 5}, carolSeq);

            env(batch::outer(bob, bobSeq, batchFee, tfUntilFailure),
                batch::inner(jv1, bobSeq + 1),
                batch::inner(jv2, carolSeq),
                batch::sig(carol),
                ter(tesSUCCESS));
            env.close();

            // Both applied: bob 100→90, carol 60→55, dave inbox 0→15
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 90);
            BEAST_EXPECT(
                mpt.getDecryptedBalance(carol, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 55);
            BEAST_EXPECT(mpt.getDecryptedBalance(dave, MPTTester::HOLDER_ENCRYPTED_INBOX) == 15);
        }
    }

    void
    testBatchIndependent(FeatureBitset features)
    {
        testcase("Batch confidential MPT - independent");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const dave("dave");

        MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
        // bob=100 spending, carol=60 spending, dave=0
        setupBatchEnv(mpt, alice, bob, carol, dave, 100, 60);

        // Bob sends dave 10 (valid), Carol sends dave 300
        // (invalid), Carol sends Dave 5 (valid). Carol's
        // balance is still 60 because the preceding send failed).
        {
            auto const bobSeq = env.seq(bob);
            auto const carolSeq = env.seq(carol);
            auto const batchFee = batch::calcBatchFee(env, 1, 3);

            auto const jv1 = mpt.sendJV({.account = bob, .dest = dave, .amt = 10}, bobSeq + 1);

            // Carol trying to send dave 300 but own balance only 60
            auto const jv2 = mpt.sendJV({.account = carol, .dest = dave, .amt = 300}, carolSeq);
            auto const jv3 = mpt.sendJV({.account = carol, .dest = dave, .amt = 5}, carolSeq + 1);

            env(batch::outer(bob, bobSeq, batchFee, tfIndependent),
                batch::inner(jv1, bobSeq + 1),
                batch::inner(jv2, carolSeq),
                batch::inner(jv3, carolSeq + 1),
                batch::sig(carol),
                ter(tesSUCCESS));
            env.close();

            // inner 1 (bob→dave 10) applied: bob 100→90
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 90);
            // inner 2 failed (carol not changed), inner 3 applied: carol 60→55
            BEAST_EXPECT(
                mpt.getDecryptedBalance(carol, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 55);
            // dave inbox: 10 (from bob) + 5 (from carol inner 3) = 15
            BEAST_EXPECT(mpt.getDecryptedBalance(dave, MPTTester::HOLDER_ENCRYPTED_INBOX) == 15);
        }
    }

    // Tests batching ConfidentialMPTConvert and a ConfidentialMPTConvertBack
    // in the same batch transaction. Because Convert only modifies the inbox
    // (never the spending balance or the version counter), a ConvertBack proof
    // built against the pre-batch spending balance is still valid when both
    // appear in the same batch.
    void
    testBatchConfidentialConvertAndConvertBack(FeatureBitset features)
    {
        testcase("Batch confidential convert and convertBack");
        using namespace test::jtx;

        // convert + convertBack in one AllOrNothing batch, both valid.
        //
        // Bob has regular=50, spending=100.
        // jv1: convert 50 regular → inbox  (Schnorr proof; does NOT touch spending/version)
        // jv2: convertBack 30 spending → regular  (proof against spending=100, version=V)
        //
        // Since jv1 leaves spending and version unchanged, jv2's proof is still
        // valid when it executes, so both inner txns succeed.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const dave("dave");

            MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
            // bob: spending=100, regular=0 after setupBatchEnv;
            // pay 50 more to give bob regular MPT to convert in the batch.
            setupBatchEnv(mpt, alice, bob, carol, dave, 100, 0);
            mpt.pay(alice, bob, 50);

            auto const bobSeq = env.seq(bob);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);

            // jv1: convert 50 regular MPT into confidential inbox
            auto const jv1 = mpt.convertJV({.account = bob, .amt = 50}, bobSeq + 1);
            // jv2: convert 30 spending back to regular MPT
            auto const jv2 = mpt.convertBackJV({.account = bob, .amt = 30}, bobSeq + 2);

            env(batch::outer(bob, bobSeq, batchFee, tfAllOrNothing),
                batch::inner(jv1, bobSeq + 1),
                batch::inner(jv2, bobSeq + 2),
                ter(tesSUCCESS));
            env.close();

            //   regular (mptAmount): 50 (pre) - 50 (convert) + 30 (convertBack) = 30
            //   spending balance: 100 - 30 = 70
            //   inbox:    0   + 50 (from convert) = 50
            env.require(mptbalance(mpt, bob, 30));
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 70);
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_INBOX) == 50);
        }

        // convert + mergeInbox + convertBack, stale convertBack proof.
        //
        // jv1: convert 50 regular → inbox
        // jv2: mergeInbox (inbox 50 → spending, version V → V+1)
        // jv3: convertBack 30 (proof built against spending=100, version=V)
        //
        // After jv2 applies, spending=150 and version=V+1, so jv3's
        // proof is stale.  AllOrNothing rejects the whole batch.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const dave("dave");

            MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
            setupBatchEnv(mpt, alice, bob, carol, dave, 100, 0);
            mpt.pay(alice, bob, 50);

            auto const bobSeq = env.seq(bob);
            auto const batchFee = batch::calcBatchFee(env, 0, 3);

            auto const jv1 = mpt.convertJV({.account = bob, .amt = 50}, bobSeq + 1);
            auto const jv2 = mpt.mergeInboxJV({.account = bob});
            // jv3 proof is built against spending=100, version=V (pre-batch)
            auto const jv3 = mpt.convertBackJV({.account = bob, .amt = 30}, bobSeq + 3);

            env(batch::outer(bob, bobSeq, batchFee, tfAllOrNothing),
                batch::inner(jv1, bobSeq + 1),
                batch::inner(jv2, bobSeq + 2),
                batch::inner(jv3, bobSeq + 3),
                ter(tesSUCCESS));
            env.close();

            // jv3 fails so nothing is applied.
            env.require(mptbalance(mpt, bob, 50));
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 100);
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_INBOX) == 0);
        }
    }

    // Tests a batch containing all four confidential MPT operations, Send,
    // Convert, ConvertBack, and MergeInbox in a single AllOrNothing batch.
    void
    testBatchConfidentialMixTransactions(FeatureBitset features)
    {
        testcase("Batch confidential mixed operations");
        using namespace test::jtx;

        // send(bob→carol) + convert(carol) + convertBack(dave)
        //  + mergeInbox(carol) in one AllOrNothing batch.
        //
        // Setup:
        //   bob:   spending=100, regular=0
        //   carol: spending=0,   regular=50
        //   dave:  spending=50,  regular=0
        //
        // After the batch:
        //   bob   spending: 100 -> 70  (sent 30 to carol)
        //   carol inbox:    0+30(send)+50(convert)=80 -> merged -> spending=80, inbox=0
        //   dave  spending: 50 -> 30; regular: 0 -> 20
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const dave("dave");

            MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
            // bob: spending=100. carol: key registered, spending=0.
            // dave: key registered, spending=0 initially.
            setupBatchEnv(mpt, alice, bob, carol, dave, 100, 0);
            // Give carol 50 regular MPT to convert in the batch.
            mpt.pay(alice, carol, 50);
            // Give dave 50 regular MPT then convert to confidential spending.
            mpt.pay(alice, dave, 50);
            mpt.convert({.account = dave, .amt = 50});
            mpt.mergeInbox({.account = dave});

            auto const bobSeq = env.seq(bob);
            auto const carolSeq = env.seq(carol);
            auto const daveSeq = env.seq(dave);
            // 2 extra signers (carol, dave), 4 inner txns
            auto const batchFee = batch::calcBatchFee(env, 2, 4);

            // jv1: bob sends 30 to carol
            auto const jv1 = mpt.sendJV({.account = bob, .dest = carol, .amt = 30}, bobSeq + 1);
            // jv2: carol converts her 50 regular MPT to confidential
            auto const jv2 = mpt.convertJV({.account = carol, .amt = 50}, carolSeq);
            // jv3: dave converts 20 spending back to regular MPT
            auto const jv3 = mpt.convertBackJV({.account = dave, .amt = 20}, daveSeq);
            // jv4: carol merges inbox into spending
            //   (inbox = 30 from jv1 + 50 from jv2 = 80 at execution time)
            auto const jv4 = mpt.mergeInboxJV({.account = carol});

            env(batch::outer(bob, bobSeq, batchFee, tfAllOrNothing),
                batch::inner(jv1, bobSeq + 1),
                batch::inner(jv2, carolSeq),
                batch::inner(jv3, daveSeq),
                batch::inner(jv4, carolSeq + 1),
                batch::sig(carol, dave),
                ter(tesSUCCESS));
            env.close();

            // All four applied:
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 70);
            // carol's inbox was merged: spending=80, inbox=0
            BEAST_EXPECT(
                mpt.getDecryptedBalance(carol, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 80);
            BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::HOLDER_ENCRYPTED_INBOX) == 0);
            // dave: spending=30, regular=20
            BEAST_EXPECT(mpt.getDecryptedBalance(dave, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 30);
            env.require(mptbalance(mpt, dave, 20));
        }

        // bob send + bob convertBack in one AllOrNothing batch.
        //
        // The Send applies first and increments Bob's version counter.
        // The ConvertBack proof was built against the pre-Send (spending=100,
        // version=V), so batch txn is rejected.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const dave("dave");

            MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
            setupBatchEnv(mpt, alice, bob, carol, dave, 100, 0);

            auto const bobSeq = env.seq(bob);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);

            // jv1: bob sends 30 to carol (spending 100->70, version V->V+1)
            auto const jv1 = mpt.sendJV({.account = bob, .dest = carol, .amt = 30}, bobSeq + 1);
            // jv2: bob convertBack 40 , proof built against spending=100, version=V
            auto const jv2 = mpt.convertBackJV({.account = bob, .amt = 40}, bobSeq + 2);

            env(batch::outer(bob, bobSeq, batchFee, tfAllOrNothing),
                batch::inner(jv1, bobSeq + 1),
                batch::inner(jv2, bobSeq + 2),
                ter(tesSUCCESS));
            env.close();

            // AllOrNothing: jv2 fails (stale proof) → nothing applied.
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 100);
            BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::HOLDER_ENCRYPTED_INBOX) == 0);
        }
    }

    // Verifies that batch transactions work correctly when tickets are used instead
    // of sequence numbers
    void
    testBatchWithTickets(FeatureBitset features)
    {
        testcase("Batch confidential MPT with tickets");
        using namespace test::jtx;

        // outer batch uses a ticket.
        // The inner send proofs are still bound to regular account sequences.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const dave("dave");

            MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
            setupBatchEnv(mpt, alice, bob, carol, dave, 100, 0);

            // Bob creates one ticket to use for the outer batch.
            std::uint32_t const outerTicketSeq = env.seq(bob) + 1;
            env(ticket::create(bob, 1));
            env.close();

            auto const bobSeq = env.seq(bob);
            // 0 extra signers: all inner txns are from bob;
            auto const batchFee = batch::calcBatchFee(env, 0, 2);

            // When the outer uses a ticket (seq=0), inner txns start from bobSeq, bobSeq+1.
            // jv2 must use chain state predicted after jv1 since both sends are from bob.
            auto const jv1 = mpt.sendJV({.account = bob, .dest = carol, .amt = 40}, bobSeq);
            auto const chain1 = mpt.chainAfterSend(bob, 40, jv1);
            auto const jv2 =
                mpt.sendJV({.account = bob, .dest = dave, .amt = 20}, bobSeq + 1, chain1);

            env(batch::outer(bob, 0, batchFee, tfAllOrNothing),
                batch::inner(jv1, bobSeq),
                batch::inner(jv2, bobSeq + 1),
                ticket::use(outerTicketSeq),
                ter(tesSUCCESS));
            env.close();

            // Both sends applied: bob 100→40, carol inbox=40, dave inbox=20.
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 40);
            BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::HOLDER_ENCRYPTED_INBOX) == 40);
            BEAST_EXPECT(mpt.getDecryptedBalance(dave, MPTTester::HOLDER_ENCRYPTED_INBOX) == 20);
        }

        // inner transactions each consume their own ticket.
        // The send proof context hash must be bound to the ticket sequence, not the
        // account sequence. sendJV receives the ticket seq as its `seq` parameter.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const dave("dave");

            MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
            setupBatchEnv(mpt, alice, bob, carol, dave, 100, 0);

            // Bob creates two tickets for the two inner sends.
            std::uint32_t const ticketSeq1 = env.seq(bob) + 1;
            std::uint32_t const ticketSeq2 = env.seq(bob) + 2;
            env(ticket::create(bob, 2));
            env.close();

            auto const bobSeq = env.seq(bob);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);

            // jv1: proof bound to ticketSeq1.
            auto const jv1 = mpt.sendJV({.account = bob, .dest = carol, .amt = 40}, ticketSeq1);
            // jv2: proof bound to ticketSeq2, spending state predicted after jv1.
            auto const chain1 = mpt.chainAfterSend(bob, 40, jv1);
            auto const jv2 =
                mpt.sendJV({.account = bob, .dest = dave, .amt = 30}, ticketSeq2, chain1);

            env(batch::outer(bob, bobSeq, batchFee, tfAllOrNothing),
                batch::inner(jv1, 0, ticketSeq1),
                batch::inner(jv2, 0, ticketSeq2),
                ter(tesSUCCESS));
            env.close();

            // Both sends applied: bob 100→30, carol inbox=40, dave inbox=30.
            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 30);
            BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::HOLDER_ENCRYPTED_INBOX) == 40);
            BEAST_EXPECT(mpt.getDecryptedBalance(dave, MPTTester::HOLDER_ENCRYPTED_INBOX) == 30);
        }

        // inner send uses wrong sequence (account seq instead of ticket seq)
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            Account const dave("dave");

            MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
            setupBatchEnv(mpt, alice, bob, carol, dave, 100, 0);

            std::uint32_t const ticketSeq = env.seq(bob) + 1;
            env(ticket::create(bob, 1));
            env.close();

            auto const bobSeq = env.seq(bob);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);

            // Proof intentionally built with account seq (bobSeq+1) instead of ticketSeq.
            auto const badJV = mpt.sendJV({.account = bob, .dest = carol, .amt = 40}, bobSeq + 1);
            auto const jv2 = mpt.mergeInboxJV({.account = bob});

            env(batch::outer(bob, bobSeq, batchFee, tfAllOrNothing),
                batch::inner(badJV, 0, ticketSeq),
                batch::inner(jv2, bobSeq + 1),
                ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(mpt.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING) == 100);
            BEAST_EXPECT(mpt.getDecryptedBalance(carol, MPTTester::HOLDER_ENCRYPTED_INBOX) == 0);
        }
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
            jv[sfZKProof.jsonName] = std::string(ecClawbackProofLength * 2, '0');
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
            jv[sfZKProof.jsonName] = std::string(ecClawbackProofLength * 2, '0');
            env(jv, delegate::as(dave), ter(temMALFORMED));
        }
    }

    // Test invalid scenarios for delegation with tickets.
    void
    testInvalidDelegationWithTickets(FeatureBitset features)
    {
        testcase("Invalid cases for delegation with tickets");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        MPTTester mptAlice(env, alice, {.holders = {bob}});
        env.fund(XRP(10000), carol);
        env.close();

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanConfidentialAmount | tfMPTCanClawback,
        });
        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 200);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.set({.issuerPubKey = mptAlice.getPubKey(alice)});

        // Bob grants carol permissions.
        env(delegate::set(bob, carol, {"ConfidentialMPTConvert"}));
        env.close();

        uint64_t const amt = 10;
        auto const bf = generateBlindingFactor();
        auto const holderCt = mptAlice.encryptAmount(bob, amt, bf);
        auto const issuerCt = mptAlice.encryptAmount(alice, amt, bf);

        // Invalid: proof built with wrong ticket sequence (ticketSeq + 1).
        {
            auto const ticketSeq = env.seq(bob) + 1;
            env(ticket::create(bob, 1));

            auto const badCtxHash =
                getConvertContextHash(bob, mptAlice.issuanceID(), ticketSeq + 1);
            auto const badProof = mptAlice.getSchnorrProof(bob, badCtxHash);
            BEAST_EXPECT(badProof.has_value());

            mptAlice.convert(
                {.account = bob,
                 .amt = amt,
                 .proof = strHex(*badProof),
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .holderEncryptedAmt = holderCt,
                 .issuerEncryptedAmt = issuerCt,
                 .blindingFactor = bf,
                 .delegate = carol,
                 .ticketSeq = ticketSeq,
                 .err = tecBAD_PROOF});
        }

        // Invalid: proof built with account sequence instead of ticket sequence.
        {
            auto const ticketSeq = env.seq(bob) + 1;
            env(ticket::create(bob, 1));
            auto const badCtxHash = getConvertContextHash(bob, mptAlice.issuanceID(), env.seq(bob));
            auto const badProof = mptAlice.getSchnorrProof(bob, badCtxHash);
            BEAST_EXPECT(badProof.has_value());

            mptAlice.convert(
                {.account = bob,
                 .amt = amt,
                 .proof = strHex(*badProof),
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .holderEncryptedAmt = holderCt,
                 .issuerEncryptedAmt = issuerCt,
                 .blindingFactor = bf,
                 .delegate = carol,
                 .ticketSeq = ticketSeq,
                 .err = tecBAD_PROOF});
        }

        // Invalid: ticket sequence is far in the future and hasn't been created yet.
        {
            mptAlice.convert({
                .account = bob,
                .amt = amt,
                .holderPubKey = mptAlice.getPubKey(bob),
                .holderEncryptedAmt = holderCt,
                .issuerEncryptedAmt = issuerCt,
                .blindingFactor = bf,
                .delegate = carol,
                .ticketSeq = env.seq(bob) + 100,
                .err = terPRE_TICKET,
            });
        }

        // Invalid: ticket sequence is in the past but was never created.
        {
            mptAlice.convert({
                .account = bob,
                .amt = amt,
                .holderPubKey = mptAlice.getPubKey(bob),
                .holderEncryptedAmt = holderCt,
                .issuerEncryptedAmt = issuerCt,
                .blindingFactor = bf,
                .delegate = carol,
                .ticketSeq = 1,
                .err = tefNO_TICKET,
            });
        }

        // Invalid: the delegated account, carol, creates a ticket and uses it.
        {
            auto const carolTicketSeq = env.seq(carol) + 1;
            env(ticket::create(carol, 1));

            mptAlice.convert(
                {.account = bob,
                 .amt = amt,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .holderEncryptedAmt = holderCt,
                 .issuerEncryptedAmt = issuerCt,
                 .blindingFactor = bf,
                 .delegate = carol,
                 .ticketSeq = carolTicketSeq,
                 .err = tefNO_TICKET});
        }

        // Invalid: proof bound to a ticket sequence but submitted without a ticket,
        // using account sequence.
        {
            auto const ticketSeq = env.seq(bob) + 1;
            env(ticket::create(bob, 1));

            // Build proof using ticketSeq.
            auto const ctxHashForTicket =
                getConvertContextHash(bob, mptAlice.issuanceID(), ticketSeq);
            auto const proof = mptAlice.getSchnorrProof(bob, ctxHashForTicket);
            BEAST_EXPECT(proof.has_value());

            // Submit without ticket.
            mptAlice.convert(
                {.account = bob,
                 .amt = amt,
                 .proof = strHex(*proof),
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .holderEncryptedAmt = holderCt,
                 .issuerEncryptedAmt = issuerCt,
                 .blindingFactor = bf,
                 .delegate = carol,
                 .err = tecBAD_PROOF});
        }
    }

    // Verifies that delegation works correctly when the delegating account uses
    // tickets instead of regular sequence numbers. The proof must bind to the
    // ticket sequence, not the account sequence.
    void
    testDelegationWithTickets(FeatureBitset features)
    {
        testcase("Confidential delegation with tickets");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const dave("dave");
        MPTTester mptAlice(env, alice, {.holders = {bob, carol}});
        env.fund(XRP(10000), dave);
        env.close();

        mptAlice.create({
            .ownerCount = 1,
            .flags = tfMPTCanTransfer | tfMPTCanConfidentialAmount | tfMPTCanClawback,
        });
        mptAlice.authorize({.account = bob});
        mptAlice.authorize({.account = carol});
        mptAlice.pay(alice, bob, 200);
        mptAlice.pay(alice, carol, 100);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);
        mptAlice.set({.issuerPubKey = mptAlice.getPubKey(alice)});

        // Bob grants dave permissions.
        env(delegate::set(
            bob,
            dave,
            {"ConfidentialMPTConvert",
             "ConfidentialMPTMergeInbox",
             "ConfidentialMPTSend",
             "ConfidentialMPTConvertBack"}));
        // Alice grants dave permission to clawback on her behalf.
        env(delegate::set(alice, dave, {"ConfidentialMPTClawback"}));
        env.close();

        // Dave executes Convert on behalf of bob using ticket.
        auto ticketSeq = env.seq(bob) + 1;
        env(ticket::create(bob, 1));
        BEAST_EXPECT(env.seq(bob) != ticketSeq);
        mptAlice.convert({
            .account = bob,
            .amt = 100,
            .holderPubKey = mptAlice.getPubKey(bob),
            .delegate = dave,
            .ticketSeq = ticketSeq,
        });
        env.require(mptbalance(mptAlice, bob, 100));

        // MergeInbox using ticket with delegation.
        ticketSeq = env.seq(bob) + 1;
        env(ticket::create(bob, 1));
        BEAST_EXPECT(env.seq(bob) != ticketSeq);
        mptAlice.mergeInbox({.account = bob, .delegate = dave, .ticketSeq = ticketSeq});

        // Carol converts and merges inbox to receive from bob.
        mptAlice.convert({
            .account = carol,
            .amt = 50,
            .holderPubKey = mptAlice.getPubKey(carol),
        });
        mptAlice.mergeInbox({.account = carol});

        // Send using ticket with delegation.
        ticketSeq = env.seq(bob) + 1;
        env(ticket::create(bob, 1));
        BEAST_EXPECT(env.seq(bob) != ticketSeq);
        mptAlice.send({
            .account = bob,
            .dest = carol,
            .amt = 20,
            .delegate = dave,
            .ticketSeq = ticketSeq,
        });

        // ConvertBack using ticket with delegation.
        ticketSeq = env.seq(bob) + 1;
        env(ticket::create(bob, 1));
        BEAST_EXPECT(env.seq(bob) != ticketSeq);
        mptAlice.convertBack({
            .account = bob,
            .amt = 10,
            .delegate = dave,
            .ticketSeq = ticketSeq,
        });

        // Clawback using ticket with delegation.
        ticketSeq = env.seq(alice) + 1;
        env(ticket::create(alice, 1));
        BEAST_EXPECT(env.seq(alice) != ticketSeq);
        mptAlice.confidentialClaw({
            .holder = bob,
            .amt = 70,
            .delegate = dave,
            .ticketSeq = ticketSeq,
        });
    }

    void
    testForgedEqualityProof(FeatureBitset features)
    {
        testcase("test Forged Equality Proof");

        // Test that modifying a ciphertext after proof generation causes
        // verification to fail. The Fiat-Shamir challenge binds ciphertexts
        // to the proof, so any modification invalidates the proof.

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

        mptAlice.pay(alice, bob, 1000);
        mptAlice.pay(alice, carol, 1000);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);

        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        // Setup: Bob and Carol convert some tokens to confidential mode
        mptAlice.convert({.account = bob, .amt = 100, .holderPubKey = mptAlice.getPubKey(bob)});
        mptAlice.mergeInbox({.account = bob});

        mptAlice.convert({.account = carol, .amt = 50, .holderPubKey = mptAlice.getPubKey(carol)});
        mptAlice.mergeInbox({.account = carol});

        // Use struct for common setup
        ZkpTestSetup setup(mptAlice, bob, carol, alice, 10);

        // Forge destination ciphertext (Enc(20) instead of Enc(10))
        {
            auto const proof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            Buffer const forgedBlindingFactor = generateBlindingFactor();
            auto const forgedDestAmt = mptAlice.encryptAmount(carol, 20, forgedBlindingFactor);

            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = setup.sendAmount,
                 .proof = strHex(*proof),
                 .senderEncryptedAmt = setup.senderAmt,
                 .destEncryptedAmt = forgedDestAmt,
                 .issuerEncryptedAmt = setup.issuerAmt,
                 .amountCommitment = setup.amountCommitment,
                 .balanceCommitment = setup.balanceCommitment,
                 .err = tecBAD_PROOF});
        }

        // Forge sender's ciphertext (Enc(5) instead of Enc(10))
        {
            auto const proof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            Buffer const forgedBlindingFactor = generateBlindingFactor();
            auto const forgedSenderAmt = mptAlice.encryptAmount(bob, 5, forgedBlindingFactor);

            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = setup.sendAmount,
                 .proof = strHex(*proof),
                 .senderEncryptedAmt = forgedSenderAmt,
                 .destEncryptedAmt = setup.destAmt,
                 .issuerEncryptedAmt = setup.issuerAmt,
                 .amountCommitment = setup.amountCommitment,
                 .balanceCommitment = setup.balanceCommitment,
                 .err = tecBAD_PROOF});
        }

        // Forge issuer's ciphertext (Enc(100) instead of Enc(10))
        {
            auto const proof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            Buffer const forgedBlindingFactor = generateBlindingFactor();
            auto const forgedIssuerAmt = mptAlice.encryptAmount(alice, 100, forgedBlindingFactor);

            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = setup.sendAmount,
                 .proof = strHex(*proof),
                 .senderEncryptedAmt = setup.senderAmt,
                 .destEncryptedAmt = setup.destAmt,
                 .issuerEncryptedAmt = forgedIssuerAmt,
                 .amountCommitment = setup.amountCommitment,
                 .balanceCommitment = setup.balanceCommitment,
                 .err = tecBAD_PROOF});
        }
    }

    void
    testForgedRangeProof(FeatureBitset features)
    {
        testcase("test Forged Range Proof");

        // Verify that replacing a valid bulletproof with one claiming
        // remaining_balance = uint64_max fails because the forged proof
        // doesn't match the actual Pedersen commitments.

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

        mptAlice.pay(alice, bob, 1000);
        mptAlice.pay(alice, carol, 1000);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);

        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        // Setup: Bob and Carol convert some tokens to confidential mode
        mptAlice.convert({.account = bob, .amt = 100, .holderPubKey = mptAlice.getPubKey(bob)});
        mptAlice.mergeInbox({.account = bob});

        mptAlice.convert({.account = carol, .amt = 50, .holderPubKey = mptAlice.getPubKey(carol)});
        mptAlice.mergeInbox({.account = carol});

        uint64_t const sendAmount = 10;
        uint64_t const forgedAmount = 0xFFFFFFFFFFFFFFFF;  // uint64_max

        Buffer const blindingFactor = generateBlindingFactor();

        // Step 1: Generate valid ciphertexts encrypting sendAmount
        auto const senderAmt = mptAlice.encryptAmount(bob, sendAmount, blindingFactor);
        auto const destAmt = mptAlice.encryptAmount(carol, sendAmount, blindingFactor);
        auto const issuerAmt = mptAlice.encryptAmount(alice, sendAmount, blindingFactor);

        // Generate Pedersen commitments for the actual amounts
        auto const amountBlindingFactor = generateBlindingFactor();
        auto const amountCommitment =
            mptAlice.getPedersenCommitment(sendAmount, amountBlindingFactor);

        auto const prevSpending =
            mptAlice.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING);
        BEAST_EXPECT(prevSpending.has_value());
        auto const balanceBlindingFactor = generateBlindingFactor();
        auto const balanceCommitment =
            mptAlice.getPedersenCommitment(*prevSpending, balanceBlindingFactor);

        auto const version = mptAlice.getMPTokenVersion(bob);
        auto const ctxHash =
            getSendContextHash(bob.id(), mptAlice.issuanceID(), env.seq(bob), carol.id(), version);

        size_t const nRecipients = 3;
        Buffer const bobPubKey = *mptAlice.getPubKey(bob);
        Buffer const carolPubKey = *mptAlice.getPubKey(carol);
        Buffer const alicePubKey = *mptAlice.getPubKey(alice);
        std::vector<ConfidentialRecipient> recipients;
        recipients.push_back({Slice(bobPubKey), senderAmt});
        recipients.push_back({Slice(carolPubKey), destAmt});
        recipients.push_back({Slice(alicePubKey), issuerAmt});

        auto const prevEncryptedSpending =
            mptAlice.getEncryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING);
        BEAST_EXPECT(prevEncryptedSpending.has_value());

        // Generate a completely valid proof for the actual sendAmount
        auto const validProof = mptAlice.getConfidentialSendProof(
            bob,
            sendAmount,
            recipients,
            blindingFactor,
            nRecipients,
            ctxHash,
            {.pedersenCommitment = amountCommitment,
             .amt = sendAmount,
             .encryptedAmt = senderAmt,
             .blindingFactor = amountBlindingFactor},
            {.pedersenCommitment = balanceCommitment,
             .amt = *prevSpending,
             .encryptedAmt = *prevEncryptedSpending,
             .blindingFactor = balanceBlindingFactor});

        if (!BEAST_EXPECT(validProof.has_value()))
            return;

        // Step 2: Generate a FORGED bulletproof that claims:
        // - sendAmount is correct (10)
        // - remaining balance is uint64_max (forged value)
        // This simulates an attacker trying to inflate their balance
        auto const forgedBulletproof = mptAlice.getBulletproof(
            {sendAmount, forgedAmount}, {amountBlindingFactor, balanceBlindingFactor}, ctxHash);

        // Step 3: Replace the bulletproof portion in the valid proof
        // Proof structure: [equality proof | amount linkage | balance linkage | bulletproof]
        size_t const equalityProofSize = secp256k1_mpt_prove_same_plaintext_multi_size(nRecipients);
        size_t const bulletproofOffset =
            equalityProofSize + ecPedersenProofLength + ecPedersenProofLength;

        Buffer forgedProof(validProof->size());
        // Copy equality proof and linkage proofs from valid proof
        std::memcpy(forgedProof.data(), validProof->data(), bulletproofOffset);
        // Replace bulletproof with forged one
        std::memcpy(
            forgedProof.data() + bulletproofOffset,
            forgedBulletproof.data(),
            ecDoubleBulletproofLength);

        // Step 4: Submit transaction with forged bulletproof
        // Verification fails because the forged bulletproof doesn't match
        // the balance commitment (which commits to the real balance, not uint64_max)
        mptAlice.send(
            {.account = bob,
             .dest = carol,
             .amt = sendAmount,
             .proof = strHex(forgedProof),
             .senderEncryptedAmt = senderAmt,
             .destEncryptedAmt = destAmt,
             .issuerEncryptedAmt = issuerAmt,
             .amountCommitment = amountCommitment,
             .balanceCommitment = balanceCommitment,
             .err = tecBAD_PROOF});
    }

    void
    testNegativeValueMalleability(FeatureBitset features)
    {
        testcase("test Negative Value Malleability");

        // Verify that claiming a wrapped negative balance (e.g., -10 as uint64)
        // fails because the verifier derives the commitment independently and
        // the forged bulletproof won't match.

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

        mptAlice.pay(alice, bob, 1000);
        mptAlice.pay(alice, carol, 1000);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);

        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        // Bob converts exactly 10 tokens — the minimum needed to execute
        // a send of 10, leaving an honest remaining balance of 0.
        mptAlice.convert({.account = bob, .amt = 10, .holderPubKey = mptAlice.getPubKey(bob)});
        mptAlice.mergeInbox({.account = bob});

        mptAlice.convert({.account = carol, .amt = 50, .holderPubKey = mptAlice.getPubKey(carol)});
        mptAlice.mergeInbox({.account = carol});

        // -10 reinterpreted as uint64_t via two's complement wraparound.
        // Represents the "remaining balance" an attacker would claim when
        // overdrafting 10 tokens from a zero balance (or 20 from a balance of 10).
        uint64_t const negativeRemaining = static_cast<uint64_t>(-10);  // 0xFFFFFFFFFFFFFFF6

        // Use struct for common setup
        ZkpTestSetup setup(mptAlice, bob, carol, alice, 10);

        auto const ctxHash = getSendContextHash(
            bob.id(), mptAlice.issuanceID(), env.seq(bob), carol.id(), setup.version);

        // Generate a fully valid proof for the legitimate send (sendAmount == balance,
        // honest remaining == 0). The equality and linkage proofs in this proof are
        // valid and will be reused; only the bulletproof will be replaced.
        auto const validProof = setup.generateProof(mptAlice, env, bob, carol);
        if (!BEAST_EXPECT(validProof.has_value()))
            return;

        // Forge a bulletproof claiming:
        //   sendAmount  (10)                 is in [0, 2^64-1]  ← true
        //   remaining   (0xFFFFFFFFFFFFFFF6) is in [0, 2^64-1]  ← technically true,
        //                                                           but represents -10
        //
        // The proof is internally consistent (both values are valid uint64s), yet
        // the verifier independently derives:
        //   PC_remaining = PC_balance - 10*G = PC(10) - 10*G = PC(0)
        //
        // PC(0) != PC(0xFFFFFFFFFFFFFFF6), so the bulletproof is rejected.
        auto const forgedBulletproof = mptAlice.getBulletproof(
            {setup.sendAmount, negativeRemaining},
            {setup.amountBlindingFactor, setup.balanceBlindingFactor},
            ctxHash);

        // Proof layout: [equality proof | amount linkage | balance linkage | bulletproof]
        size_t const equalityProofSize =
            secp256k1_mpt_prove_same_plaintext_multi_size(setup.nRecipients);
        size_t const bulletproofOffset =
            equalityProofSize + ecPedersenProofLength + ecPedersenProofLength;

        Buffer forgedProof(validProof->size());
        std::memcpy(forgedProof.data(), validProof->data(), bulletproofOffset);
        std::memcpy(
            forgedProof.data() + bulletproofOffset,
            forgedBulletproof.data(),
            ecDoubleBulletproofLength);

        // Submit the tampered proof. The system must reject it: the bulletproof
        // proves a wrapped negative remaining balance, not the honest value (0).
        // Acceptance would allow an attacker to spend tokens while fraudulently
        // claiming a non-zero (effectively infinite) remaining balance.
        mptAlice.send(
            {.account = bob,
             .dest = carol,
             .amt = setup.sendAmount,
             .proof = strHex(forgedProof),
             .senderEncryptedAmt = setup.senderAmt,
             .destEncryptedAmt = setup.destAmt,
             .issuerEncryptedAmt = setup.issuerAmt,
             .amountCommitment = setup.amountCommitment,
             .balanceCommitment = setup.balanceCommitment,
             .err = tecBAD_PROOF});
    }

    void
    testFiatShamirBinding(FeatureBitset features)
    {
        testcase("test Fiat-Shamir Binding");

        // Verify that modifying any transcript-bound input (ciphertexts,
        // commitments, context) after proof generation invalidates the proof.

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

        mptAlice.pay(alice, bob, 1000);
        mptAlice.pay(alice, carol, 1000);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);

        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        mptAlice.convert({.account = bob, .amt = 100, .holderPubKey = mptAlice.getPubKey(bob)});
        mptAlice.mergeInbox({.account = bob});

        mptAlice.convert({.account = carol, .amt = 50, .holderPubKey = mptAlice.getPubKey(carol)});
        mptAlice.mergeInbox({.account = carol});

        // Use struct for common setup
        ZkpTestSetup setup(mptAlice, bob, carol, alice, 10);

        // Variant A: Modify transcript input (commitment) after proof generation
        // -----------------------------------------------------------------
        // Attack: Generate valid proof, then swap the amount commitment
        // with a forged one committing to a different value.
        //
        // Why it fails: The Pedersen linkage proof binds the commitment to
        // the proof. When the verifier recomputes the challenge using the
        // forged commitment, it gets a different challenge than what the
        // prover used, causing the response scalars to fail verification.
        {
            auto const proof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            auto const forgedBlindingFactor = generateBlindingFactor();
            auto const forgedCommitment =
                mptAlice.getPedersenCommitment(setup.sendAmount + 5, forgedBlindingFactor);

            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = setup.sendAmount,
                 .proof = strHex(*proof),
                 .senderEncryptedAmt = setup.senderAmt,
                 .destEncryptedAmt = setup.destAmt,
                 .issuerEncryptedAmt = setup.issuerAmt,
                 .amountCommitment = forgedCommitment,
                 .balanceCommitment = setup.balanceCommitment,
                 .err = tecBAD_PROOF});
        }

        // Variant B: Proof replay under different TransactionContextID
        // -----------------------------------------------------------------
        // Attack: Generate valid proof at sequence N, then attempt to
        // replay the same proof after the sequence has changed to N+1.
        //
        // Why it fails: The context hash (account, sequence, issuance ID,
        // destination) is included in the Fiat-Shamir challenge. When the
        // verifier computes the challenge with the new sequence, it differs
        // from the challenge the prover used, invalidating the proof.
        // This prevents replay attacks across different transactions.
        {
            auto const proof =
                setup.generateProof(mptAlice, env, bob, carol);  // Generated at sequence N
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            // Submit a different transaction to increment Bob's sequence
            mptAlice.pay(bob, carol, 1);  // Sequence N -> N+1
            env.close();

            // Attempt to replay proof (bound to sequence N) at sequence N+1
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = setup.sendAmount,
                 .proof = strHex(*proof),
                 .senderEncryptedAmt = setup.senderAmt,
                 .destEncryptedAmt = setup.destAmt,
                 .issuerEncryptedAmt = setup.issuerAmt,
                 .amountCommitment = setup.amountCommitment,
                 .balanceCommitment = setup.balanceCommitment,
                 .err = tecBAD_PROOF});
        }

        // Variant C: Tamper with challenge-dependent response scalars
        // -----------------------------------------------------------------
        // Attack: Generate valid proof, then flip bits in the middle of
        // the proof data, corrupting the response scalars (s-values).
        //
        // Why it fails: In a Schnorr-style proof, the response scalar is
        // computed as s = r + c * secret, where c is the challenge. Even
        // if the challenge matches, corrupted s-values won't satisfy the
        // verification equation: s*G == R + c*PubKey. The verifier detects
        // this inconsistency and rejects the proof.
        {
            auto const proof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            Buffer tamperedProof(proof->size());
            std::memcpy(tamperedProof.data(), proof->data(), proof->size());
            size_t const tamperOffset = tamperedProof.size() / 2;
            tamperedProof.data()[tamperOffset] ^= 0xFF;

            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = setup.sendAmount,
                 .proof = strHex(tamperedProof),
                 .senderEncryptedAmt = setup.senderAmt,
                 .destEncryptedAmt = setup.destAmt,
                 .issuerEncryptedAmt = setup.issuerAmt,
                 .amountCommitment = setup.amountCommitment,
                 .balanceCommitment = setup.balanceCommitment,
                 .err = tecBAD_PROOF});
        }
    }

    void
    testProofComponentReuse(FeatureBitset features)
    {
        testcase("test Proof Component Reuse");

        // Verify that extracting proof components from one transaction and
        // reusing them in a different context fails verification.

        using namespace test::jtx;
        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const dan("dan");
        MPTTester mptAlice(env, alice, {.holders = {bob, carol, dan}});

        mptAlice.create(
            {.ownerCount = 1,
             .flags = tfMPTCanLock | tfMPTCanConfidentialAmount | tfMPTCanTransfer});
        mptAlice.authorize({.account = bob});
        mptAlice.authorize({.account = carol});
        mptAlice.authorize({.account = dan});

        mptAlice.pay(alice, bob, 1000);
        mptAlice.pay(alice, carol, 1000);
        mptAlice.pay(alice, dan, 1000);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);
        mptAlice.generateKeyPair(dan);

        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        mptAlice.convert({.account = bob, .amt = 100, .holderPubKey = mptAlice.getPubKey(bob)});
        mptAlice.mergeInbox({.account = bob});

        mptAlice.convert({.account = carol, .amt = 50, .holderPubKey = mptAlice.getPubKey(carol)});
        mptAlice.mergeInbox({.account = carol});

        mptAlice.convert({.account = dan, .amt = 50, .holderPubKey = mptAlice.getPubKey(dan)});
        mptAlice.mergeInbox({.account = dan});

        uint64_t const sendAmount = 10;

        // Variant A: Replay proof to same destination (sequence changed)
        // -----------------------------------------------------------------
        // Attack: Extract proof from a successful Bob->Carol transaction,
        // then replay it to the same destination (Carol) in a new transaction.
        //
        // Why it fails: The context hash includes the account sequence.
        // After the first transaction, Bob's sequence increments from N to N+1.
        // The proof was bound to sequence N, but verifier computes challenge
        // with sequence N+1, causing a mismatch.
        {
            // Fresh setup for Variant A
            ZkpTestSetup setup(mptAlice, bob, carol, alice, sendAmount);

            auto const proof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            // Submit successful transaction Bob -> Carol (sequence N)
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = setup.sendAmount,
                 .proof = strHex(*proof),
                 .senderEncryptedAmt = setup.senderAmt,
                 .destEncryptedAmt = setup.destAmt,
                 .issuerEncryptedAmt = setup.issuerAmt,
                 .amountCommitment = setup.amountCommitment,
                 .balanceCommitment = setup.balanceCommitment});
            mptAlice.mergeInbox({.account = carol});

            // Attempt to replay exact same proof to same destination (sequence N+1)
            // Uses identical ciphertexts and commitments - only sequence differs
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = setup.sendAmount,
                 .proof = strHex(*proof),
                 .senderEncryptedAmt = setup.senderAmt,
                 .destEncryptedAmt = setup.destAmt,
                 .issuerEncryptedAmt = setup.issuerAmt,
                 .amountCommitment = setup.amountCommitment,
                 .balanceCommitment = setup.balanceCommitment,
                 .err = tecBAD_PROOF});
        }

        // Variant B: Replay proof to different destination
        // -----------------------------------------------------------------
        // Attack: Extract proof from a successful Bob->Carol transaction,
        // then attempt to reuse it for Bob->Dan (different destination).
        //
        // Why it fails: The context hash includes the destination account.
        // When verifier recomputes challenge with Dan's address instead of
        // Carol's, it gets a different challenge than what the prover used.
        {
            // Fresh setup for Variant B (balance changed after Variant A)
            ZkpTestSetup setup(mptAlice, bob, carol, alice, sendAmount);

            auto const proof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            // Submit successful transaction Bob -> Carol
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = setup.sendAmount,
                 .proof = strHex(*proof),
                 .senderEncryptedAmt = setup.senderAmt,
                 .destEncryptedAmt = setup.destAmt,
                 .issuerEncryptedAmt = setup.issuerAmt,
                 .amountCommitment = setup.amountCommitment,
                 .balanceCommitment = setup.balanceCommitment});
            mptAlice.mergeInbox({.account = carol});

            // Generate fresh ciphertexts for Dan (different destination)
            auto const destAmtDan = mptAlice.encryptAmount(dan, sendAmount, setup.blindingFactor);
            auto const issuerAmtDan =
                mptAlice.encryptAmount(alice, sendAmount, setup.blindingFactor);

            // Attempt to reuse proof extracted from Bob->Carol for Bob->Dan
            mptAlice.send(
                {.account = bob,
                 .dest = dan,
                 .amt = setup.sendAmount,
                 .proof = strHex(*proof),
                 .senderEncryptedAmt = setup.senderAmt,
                 .destEncryptedAmt = destAmtDan,
                 .issuerEncryptedAmt = issuerAmtDan,
                 .amountCommitment = setup.amountCommitment,
                 .balanceCommitment = setup.balanceCommitment,
                 .err = tecBAD_PROOF});
        }
    }

    void
    testSpecialWitnessValues(FeatureBitset features)
    {
        testcase("test Special Witness Values");

        // This test verifies that the ZKP verification correctly rejects
        // proofs containing degenerate or edge-case values that an attacker
        // might use to forge proofs.

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

        mptAlice.pay(alice, bob, 1000);
        mptAlice.pay(alice, carol, 1000);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);

        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        mptAlice.convert({.account = bob, .amt = 100, .holderPubKey = mptAlice.getPubKey(bob)});
        mptAlice.mergeInbox({.account = bob});

        mptAlice.convert({.account = carol, .amt = 50, .holderPubKey = mptAlice.getPubKey(carol)});
        mptAlice.mergeInbox({.account = carol});

        // Setup common test parameters
        ZkpTestSetup setup(mptAlice, bob, carol, alice, 10);

        // Variant A: Zero-valued response scalars
        // -----------------------------------------------------------------
        // Attack: Construct a proof where response scalars are set to zero.
        // This attempts to trivially satisfy verification equations.
        //
        // Why it fails: The Fiat-Shamir challenge is derived from the
        // transcript including commitments. Zero responses cannot satisfy
        // c * x + r = 0 for non-zero challenge c and secret x.
        {
            auto const proof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            // Create a forged proof with scalar responses set to zero
            Buffer forgedProof = *proof;

            // Equality proof structure: (1 + 2*n) points followed by (1 + n) scalars
            size_t const pointsSize = (1 + 2 * setup.nRecipients) * compressedECPointLength;
            size_t const scalarsSize = (1 + setup.nRecipients) * ecBlindingFactorLength;
            std::memset(forgedProof.data() + pointsSize, 0, scalarsSize);

            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = setup.sendAmount,
                 .proof = strHex(forgedProof),
                 .senderEncryptedAmt = setup.senderAmt,
                 .destEncryptedAmt = setup.destAmt,
                 .issuerEncryptedAmt = setup.issuerAmt,
                 .amountCommitment = setup.amountCommitment,
                 .balanceCommitment = setup.balanceCommitment,
                 .err = tecBAD_PROOF});
        }

        // Variant B: Identity element (point at infinity) in ciphertext
        // -----------------------------------------------------------------
        // Attack: Replace a ciphertext component with the identity element.
        // The identity element has no valid compressed representation on
        // secp256k1 (it cannot be encoded as 02/03 + x-coordinate).
        //
        // Why it fails: secp256k1_ec_pubkey_parse rejects invalid encodings.
        // The all-zeros encoding is not a valid compressed point.
        {
            auto const proof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            // Create invalid ciphertext with identity-like encoding
            // All zeros is not valid on secp256k1 curve
            Buffer invalidCiphertext(ecGamalEncryptedTotalLength);
            std::memset(invalidCiphertext.data(), 0, ecGamalEncryptedTotalLength);

            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = setup.sendAmount,
                 .proof = strHex(*proof),
                 .senderEncryptedAmt = invalidCiphertext,
                 .destEncryptedAmt = setup.destAmt,
                 .issuerEncryptedAmt = setup.issuerAmt,
                 .amountCommitment = setup.amountCommitment,
                 .balanceCommitment = setup.balanceCommitment,
                 .err = temBAD_CIPHERTEXT});
        }

        // Variant B2: Identity element in commitment
        // -----------------------------------------------------------------
        {
            auto const proof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            // Create invalid commitment with identity-like encoding
            Buffer invalidCommitment(ecPedersenCommitmentLength);
            std::memset(invalidCommitment.data(), 0, ecPedersenCommitmentLength);

            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = setup.sendAmount,
                 .proof = strHex(*proof),
                 .senderEncryptedAmt = setup.senderAmt,
                 .destEncryptedAmt = setup.destAmt,
                 .issuerEncryptedAmt = setup.issuerAmt,
                 .amountCommitment = invalidCommitment,
                 .balanceCommitment = setup.balanceCommitment,
                 .err = temMALFORMED});
        }

        // Variant C: Boundary scalar (curve order)
        // -----------------------------------------------------------------
        // Attack: Use scalar values equal to the curve order n.
        // On secp256k1, scalars are reduced mod n, so n ≡ 0 (mod n).
        //
        // Why it fails: Setting scalar = curve_order effectively sets it to 0,
        // which fails the verification equations as in Variant A.
        {
            auto const proof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            Buffer forgedProof = *proof;

            // secp256k1 curve order n (big-endian)
            static constexpr unsigned char curveOrder[32] = {
                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  //
                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,  //
                0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48, 0xA0, 0x3B,  //
                0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x41   //
            };

            // Equality proof structure: (1 + 2*n) points followed by (1 + n) scalars
            size_t const pointsSize = (1 + 2 * setup.nRecipients) * compressedECPointLength;
            std::memcpy(forgedProof.data() + pointsSize, curveOrder, 32);

            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = setup.sendAmount,
                 .proof = strHex(forgedProof),
                 .senderEncryptedAmt = setup.senderAmt,
                 .destEncryptedAmt = setup.destAmt,
                 .issuerEncryptedAmt = setup.issuerAmt,
                 .amountCommitment = setup.amountCommitment,
                 .balanceCommitment = setup.balanceCommitment,
                 .err = tecBAD_PROOF});
        }

        // Variant C2: Overflow scalar (curve order + 1)
        // -----------------------------------------------------------------
        // Attack: Use scalar values greater than the curve order.
        //
        // Why it fails: Values > n are either rejected or reduced mod n,
        // resulting in small values that don't satisfy verification.
        {
            auto const proof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            Buffer forgedProof = *proof;

            // curve_order + 1 (big-endian)
            static constexpr unsigned char overflowScalar[32] = {
                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  //
                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,  //
                0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48, 0xA0, 0x3B,  //
                0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x42   //
            };

            // Equality proof structure: (1 + 2*n) points followed by (1 + n) scalars
            size_t const pointsSize = (1 + 2 * setup.nRecipients) * compressedECPointLength;
            std::memcpy(forgedProof.data() + pointsSize, overflowScalar, 32);

            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = setup.sendAmount,
                 .proof = strHex(forgedProof),
                 .senderEncryptedAmt = setup.senderAmt,
                 .destEncryptedAmt = setup.destAmt,
                 .issuerEncryptedAmt = setup.issuerAmt,
                 .amountCommitment = setup.amountCommitment,
                 .balanceCommitment = setup.balanceCommitment,
                 .err = tecBAD_PROOF});
        }
    }

    void
    testCrossStatementProofSubstitution(FeatureBitset features)
    {
        testcase("test Cross-Statement Proof Substitution");

        // This test verifies that proofs generated for one protocol component
        // cannot be used in place of another, and that proofs bound to
        // different public parameters are rejected.

        using namespace test::jtx;
        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

        mptAlice.create(
            {.ownerCount = 1,
             .flags =
                 tfMPTCanLock | tfMPTCanConfidentialAmount | tfMPTCanTransfer | tfMPTCanClawback});
        mptAlice.authorize({.account = bob});
        mptAlice.authorize({.account = carol});

        mptAlice.pay(alice, bob, 1000);
        mptAlice.pay(alice, carol, 1000);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);

        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        mptAlice.convert({.account = bob, .amt = 100, .holderPubKey = mptAlice.getPubKey(bob)});
        mptAlice.mergeInbox({.account = bob});

        mptAlice.convert({.account = carol, .amt = 50, .holderPubKey = mptAlice.getPubKey(carol)});
        mptAlice.mergeInbox({.account = carol});

        uint64_t const sendAmount = 10;

        // Variant A: Swap proof type (cross-statement substitution)
        // -----------------------------------------------------------------
        // Attack: Generate a valid convertBack proof (Pedersen linkage +
        // single bulletproof) and attempt to use it as the ZK proof in a
        // ConfidentialMPTSend transaction.
        //
        // Expected: The send proof has a different structure
        // (equality + 2×pedersen + double bulletproof). Even if sized to
        // match, the domain-separated Fiat-Shamir transcript differs,
        // so verification equations fail.
        {
            ZkpTestSetup setup(mptAlice, bob, carol, alice, sendAmount);

            // Generate a valid convertBack proof for bob
            auto const spendingBalance =
                mptAlice.getDecryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING);
            BEAST_EXPECT(spendingBalance.has_value());
            auto const encryptedSpending =
                mptAlice.getEncryptedBalance(bob, MPTTester::HOLDER_ENCRYPTED_SPENDING);
            BEAST_EXPECT(encryptedSpending.has_value());

            Buffer const pcBlindingFactor = generateBlindingFactor();
            Buffer const pedersenCommitment =
                mptAlice.getPedersenCommitment(*spendingBalance, pcBlindingFactor);

            auto const version = mptAlice.getMPTokenVersion(bob);
            uint256 const convertBackCtxHash =
                getConvertBackContextHash(bob.id(), mptAlice.issuanceID(), env.seq(bob), version);

            Buffer const convertBackProof = mptAlice.getConvertBackProof(
                bob,
                sendAmount,
                convertBackCtxHash,
                {.pedersenCommitment = pedersenCommitment,
                 .amt = *spendingBalance,
                 .encryptedAmt = *encryptedSpending,
                 .blindingFactor = pcBlindingFactor});

            // Resize the convertBack proof to match the expected send proof
            // size so it passes preflight's size check and reaches the actual
            // ZK verification in doApply.
            auto const expectedSendSize = getEqualityProofSize(setup.nRecipients) +
                2 * ecPedersenProofLength + ecDoubleBulletproofLength;
            Buffer resizedProof(expectedSendSize);
            auto const copyLen = std::min(convertBackProof.size(), expectedSendSize);
            std::memcpy(resizedProof.data(), convertBackProof.data(), copyLen);
            // Zero-pad the rest (if convertBack proof is shorter)
            if (copyLen < expectedSendSize)
                std::memset(resizedProof.data() + copyLen, 0, expectedSendSize - copyLen);

            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = setup.sendAmount,
                 .proof = strHex(resizedProof),
                 .senderEncryptedAmt = setup.senderAmt,
                 .destEncryptedAmt = setup.destAmt,
                 .issuerEncryptedAmt = setup.issuerAmt,
                 .amountCommitment = setup.amountCommitment,
                 .balanceCommitment = setup.balanceCommitment,
                 .err = tecBAD_PROOF});
        }

        // Variant B: Valid proof bound to wrong public parameters
        // -----------------------------------------------------------------
        // Attack: Generate a valid send proof using a wrong context hash
        // (computed with a different issuanceID). The proof is
        // mathematically valid for the wrong statement, but when the
        // verifier recomputes the Fiat-Shamir challenge using the correct
        // issuanceID, the challenge differs and verification fails.
        {
            ZkpTestSetup setup(mptAlice, bob, carol, alice, sendAmount);

            // Compute context hash with a fabricated (wrong) issuanceID
            uint192 const fakeIssuanceID{1};
            auto const wrongCtxHash = getSendContextHash(
                bob.id(), fakeIssuanceID, env.seq(bob), carol.id(), setup.version);

            // Generate a proof that is valid for the wrong issuanceID
            auto const wrongProof = mptAlice.getConfidentialSendProof(
                bob,
                sendAmount,
                setup.recipients,
                setup.blindingFactor,
                setup.nRecipients,
                wrongCtxHash,
                {.pedersenCommitment = setup.amountCommitment,
                 .amt = sendAmount,
                 .encryptedAmt = setup.senderAmt,
                 .blindingFactor = setup.amountBlindingFactor},
                {.pedersenCommitment = setup.balanceCommitment,
                 .amt = setup.prevSpending,
                 .encryptedAmt = setup.prevEncryptedSpending,
                 .blindingFactor = setup.balanceBlindingFactor});

            if (!BEAST_EXPECT(wrongProof.has_value()))
                return;

            // Submit with the correct issuanceID — verifier recomputes
            // the challenge using the real issuanceID, which differs from
            // the one baked into the proof.
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = setup.sendAmount,
                 .proof = strHex(*wrongProof),
                 .senderEncryptedAmt = setup.senderAmt,
                 .destEncryptedAmt = setup.destAmt,
                 .issuerEncryptedAmt = setup.issuerAmt,
                 .amountCommitment = setup.amountCommitment,
                 .balanceCommitment = setup.balanceCommitment,
                 .err = tecBAD_PROOF});
        }
    }

    void
    testCiphertextMalleability(FeatureBitset features)
    {
        testcase("test Ciphertext Malleability");

        // This test verifies that multiplying an ElGamal ciphertext by a
        // scalar (creating Enc(k*m) from Enc(m)) is detected and rejected.
        // An attacker tries to inflate the received amount without detection.
        //
        // ElGamal ciphertext = (C1, C2) where C2 = m*G + r*PK, C1 = r*G
        // Multiplying both components by k gives (k*C1, k*C2) which decrypts
        // to k*m. The ZKP binds the ciphertext to the proven value, so
        // modifying the ciphertext breaks the proof.

        using namespace test::jtx;
        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

        mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanConfidentialAmount});
        mptAlice.authorize({.account = bob});
        mptAlice.authorize({.account = carol});

        mptAlice.pay(alice, bob, 1000);
        mptAlice.pay(alice, carol, 1000);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);

        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        mptAlice.convert({.account = bob, .amt = 100, .holderPubKey = mptAlice.getPubKey(bob)});
        mptAlice.mergeInbox({.account = bob});

        mptAlice.convert({.account = carol, .amt = 50, .holderPubKey = mptAlice.getPubKey(carol)});
        mptAlice.mergeInbox({.account = carol});

        uint64_t const sendAmount = 10;

        // Variant A: Post-signature tampering
        // -----------------------------------------------------------------
        // Create a valid ConfidentialMPTSend, sign it, then modify the
        // ciphertext field while preserving the original signature.
        // Expected: Signature verification fails, transaction rejected.
        {
            // Build the valid send JV and sign it
            auto const seq = env.seq(bob);
            auto jv = mptAlice.sendJV({.account = bob, .dest = carol, .amt = sendAmount}, seq);
            auto jtx = env.jt(jv);
            BEAST_EXPECT(jtx.stx);

            // Serialize the signed transaction
            Serializer s;
            jtx.stx->add(s);

            // Deserialize into a mutable STObject so we can change
            // a specific field while keeping the original signature
            SerialIter sit(s.slice());
            STObject obj(sit, sfTransaction);

            // Replace the destination ciphertext with a valid encryption
            // of an inflated amount (2x). This is a real EC point, not
            // random bytes, so it passes format validation.
            Buffer const bf = generateBlindingFactor();
            auto const inflatedCiphertext = mptAlice.encryptAmount(carol, sendAmount * 2, bf);
            obj.setFieldVL(sfDestinationEncryptedAmount, inflatedCiphertext);

            // Re-serialize — the signature is still the original one
            // but the signed data has changed
            Serializer tampered;
            obj.add(tampered);

            // Submit the tampered blob — signature no longer matches.
            // The RPC layer rejects it before it reaches the transaction
            // engine, returning an "invalidTransaction" error.
            auto const jr = env.rpc("submit", strHex(tampered.slice()));
            BEAST_EXPECT(jr[jss::result][jss::error] == "invalidTransaction");
        }

        // Variant B: Re-signed malleation
        // -----------------------------------------------------------------
        // Generate a valid send proof, then modify the destination
        // ciphertext (simulating scalar multiplication of the ElGamal
        // ciphertext: (C1, C2) → (k*C1, k*C2)), and re-sign.
        // Expected: Signature passes, ZKP verification fails because the
        // ciphertext no longer matches the committed value in the proof.
        {
            ZkpTestSetup setup(mptAlice, bob, carol, alice, sendAmount);

            auto const ctxHash = getSendContextHash(
                bob.id(), mptAlice.issuanceID(), env.seq(bob), carol.id(), setup.version);

            auto const validProof = mptAlice.getConfidentialSendProof(
                bob,
                sendAmount,
                setup.recipients,
                setup.blindingFactor,
                setup.nRecipients,
                ctxHash,
                {.pedersenCommitment = setup.amountCommitment,
                 .amt = sendAmount,
                 .encryptedAmt = setup.senderAmt,
                 .blindingFactor = setup.amountBlindingFactor},
                {.pedersenCommitment = setup.balanceCommitment,
                 .amt = setup.prevSpending,
                 .encryptedAmt = setup.prevEncryptedSpending,
                 .blindingFactor = setup.balanceBlindingFactor});

            if (!BEAST_EXPECT(validProof.has_value()))
                return;

            // Create a valid ciphertext encrypting a different amount
            // (2x the real amount) to carol's key. This simulates an
            // attacker replacing the destination ciphertext with one
            // that decrypts to an inflated value.
            auto const inflatedDestAmt =
                mptAlice.encryptAmount(carol, sendAmount * 2, setup.blindingFactor);

            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = setup.sendAmount,
                 .proof = strHex(*validProof),
                 .senderEncryptedAmt = setup.senderAmt,
                 .destEncryptedAmt = inflatedDestAmt,
                 .issuerEncryptedAmt = setup.issuerAmt,
                 .amountCommitment = setup.amountCommitment,
                 .balanceCommitment = setup.balanceCommitment,
                 .err = tecBAD_PROOF});
        }
    }

    void
    testCiphertextCombination(FeatureBitset features)
    {
        testcase("test Ciphertext Combination");

        // This test verifies that adding two ElGamal ciphertexts
        // Enc(m1) + Enc(m2) = Enc(m1+m2) to combine balances is detected
        // and rejected. The ZKP's equality proof with proof-of-knowledge
        // and shared randomness binding prevents this attack.

        using namespace test::jtx;
        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

        mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer | tfMPTCanConfidentialAmount});
        mptAlice.authorize({.account = bob});
        mptAlice.authorize({.account = carol});

        mptAlice.pay(alice, bob, 1000);
        mptAlice.pay(alice, carol, 1000);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(bob);
        mptAlice.generateKeyPair(carol);

        mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

        mptAlice.convert({.account = bob, .amt = 200, .holderPubKey = mptAlice.getPubKey(bob)});
        mptAlice.mergeInbox({.account = bob});

        mptAlice.convert({.account = carol, .amt = 100, .holderPubKey = mptAlice.getPubKey(carol)});
        mptAlice.mergeInbox({.account = carol});

        uint64_t const m1 = 10;
        uint64_t const m2 = 5;

        // Variant A: Post-signature ciphertext combination
        // -----------------------------------------------------------------
        // Build a valid send for m1, sign it, then replace the destination
        // ciphertext with Enc(m1) + Enc(m2) while preserving the stale
        // signature. Submit the raw blob.
        // Expected: Signature verification fails → "invalidTransaction"
        {
            auto const seq = env.seq(bob);
            auto jv = mptAlice.sendJV({.account = bob, .dest = carol, .amt = m1}, seq);
            auto jtx = env.jt(jv);
            BEAST_EXPECT(jtx.stx);

            // Serialize the signed transaction
            Serializer s;
            jtx.stx->add(s);

            // Deserialize into a mutable STObject
            SerialIter sit(s.slice());
            STObject obj(sit, sfTransaction);

            // Get the original destination ciphertext Enc(m1)
            auto const origDestCt = obj.getFieldVL(sfDestinationEncryptedAmount);

            // Create Enc(m2) for carol and homomorphically add
            Buffer const bf2 = generateBlindingFactor();
            auto const encM2 = mptAlice.encryptAmount(carol, m2, bf2);
            auto const combined = homomorphicAdd(
                Slice(origDestCt.data(), origDestCt.size()), Slice(encM2.data(), encM2.size()));
            BEAST_EXPECT(combined.has_value());

            // Replace with combined ciphertext Enc(m1+m2)
            obj.setFieldVL(sfDestinationEncryptedAmount, *combined);

            // Re-serialize with stale signature
            Serializer tampered;
            obj.add(tampered);

            // Signature no longer matches
            auto const jr = env.rpc("submit", strHex(tampered.slice()));
            BEAST_EXPECT(jr[jss::result][jss::error] == "invalidTransaction");
        }

        // Variant B: Re-signed ciphertext combination
        // -----------------------------------------------------------------
        // Generate a valid proof for amount m1, then replace the destination
        // ciphertext with Enc(m1) + Enc(m2) = Enc(m1+m2), re-sign.
        // Expected: Signature passes, ZKP fails due to:
        //   - ElGamal–Pedersen linkage mismatch
        //   - equality/shared-r proof failure
        {
            ZkpTestSetup setup(mptAlice, bob, carol, alice, m1);

            auto const validProof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(validProof.has_value()))
                return;

            // Create Enc(m2) for carol and add to the original dest ciphertext
            Buffer const bf2 = generateBlindingFactor();
            auto const encM2 = mptAlice.encryptAmount(carol, m2, bf2);
            auto const combinedDest = homomorphicAdd(setup.destAmt, encM2);
            BEAST_EXPECT(combinedDest.has_value());

            // Submit with combined ciphertext — proof was for Enc(m1) only
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = setup.sendAmount,
                 .proof = strHex(*validProof),
                 .senderEncryptedAmt = setup.senderAmt,
                 .destEncryptedAmt = combinedDest,
                 .issuerEncryptedAmt = setup.issuerAmt,
                 .amountCommitment = setup.amountCommitment,
                 .balanceCommitment = setup.balanceCommitment,
                 .err = tecBAD_PROOF});
        }

        // Variant C: Cross-transaction ciphertext reuse
        // -----------------------------------------------------------------
        // Execute a valid send of m1 from bob→carol. Extract the destination
        // ciphertext from that transaction. Build a new transaction that
        // uses the extracted ciphertext combined with a fresh Enc(m2).
        // Expected: ZKP fails because context hash binding (sequence,
        // CBS.version) differs between the old and new transactions.
        {
            // First, execute a valid send to advance bob's state
            mptAlice.send({.account = bob, .dest = carol, .amt = m1});

            // The old destination ciphertext was Enc(m1) to carol's key.
            // Recreate it deterministically for the attack.
            Buffer const oldBf = generateBlindingFactor();
            auto const oldEncM1 = mptAlice.encryptAmount(carol, m1, oldBf);

            // Now build a new send for m2, but combine ciphertexts
            ZkpTestSetup setup2(mptAlice, bob, carol, alice, m2);

            auto const proof2 = setup2.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(proof2.has_value()))
                return;

            // Combine: oldEnc(m1) + newEnc(m2) = Enc(m1+m2)
            auto const crossCombined = homomorphicAdd(oldEncM1, setup2.destAmt);
            BEAST_EXPECT(crossCombined.has_value());

            // Submit — proof was generated for m2 with current context,
            // but destination ciphertext is Enc(m1+m2)
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = setup2.sendAmount,
                 .proof = strHex(*proof2),
                 .senderEncryptedAmt = setup2.senderAmt,
                 .destEncryptedAmt = crossCombined,
                 .issuerEncryptedAmt = setup2.issuerAmt,
                 .amountCommitment = setup2.amountCommitment,
                 .balanceCommitment = setup2.balanceCommitment,
                 .err = tecBAD_PROOF});
        }
    }

    void
    testWithFeats(FeatureBitset features)
    {
        // // ConfidentialMPTConvert
        // testConvert(features);
        // testConvertPreflight(features);
        // testConvertPreclaim(features);
        // testConvertWithAuditor(features);

        // // ConfidentialMPTMergeInbox
        // testMergeInbox(features);
        // testMergeInboxPreflight(features);
        // testMergeInboxPreclaim(features);

        // testSet(features);
        // testSetPreflight(features);
        // testSetPreclaim(features);

        // // ConfidentialMPTSend
        // testSend(features);
        // testSendPreflight(features);
        // testSendPreclaim(features);
        // testSendRangeProof(features);
        // // testSendZeroAmount(features);
        // testSendDepositPreauth(features);
        // testSendCredentialValidation(features);
        // testSendWithAuditor(features);

        // // ConfidentialMPTClawback
        // testClawback(features);
        // testClawbackPreflight(features);
        // testClawbackPreclaim(features);
        // testClawbackProof(features);
        // testClawbackWithAuditor(features);

        // testDelete(features);

        // // ConfidentialMPTConvertBack
        // testConvertBack(features);
        // testConvertBackPreflight(features);
        // testConvertBackPreclaim(features);
        // testConvertBackWithAuditor(features);
        // testConvertBackPedersenProof(features);
        // testConvertBackBulletproof(features);

        // // Homomorphic operation tests
        // testSendHomomorphicOverflow(features);
        // testHomomorphicCiphertextModification(features);
        // testConvertBackHomomorphicUnderflow(features);

        // // invalid curve points
        // testSendInvalidCurvePoints(features);
        // testSendWrongGroupPointInjection(features);
        // testIdentityElementRejection(features);
        // testSendWrongIssuerPublicKey(features);

        // // Replay Tests
        // testMutatePrivacy(features);

        // // Zero knowledge proof tests
        // testForgedEqualityProof(features);
        // testForgedRangeProof(features);
        // testNegativeValueMalleability(features);
        // testFiatShamirBinding(features);
        // testProofComponentReuse(features);
        // testSpecialWitnessValues(features);
        // testCrossStatementProofSubstitution(features);

        // // Replay tests
        // testProofContextBinding(features);
        // testProofCiphertextBinding(features);
        // testProofVersionMismatch(features);

        // // Ticket Tests
        // testWithTickets(features);
        // testConvertTicketProofBinding(features);
        // testTicketErrors(features);

        // // Batch Tests
        // testBatchConfidentialSend(features);
        // testBatchConfidentialConvertAndConvertBack(features);
        // testBatchConfidentialMixTransactions(features);
        // testBatchAllOrNothing(features);
        // testBatchOnlyOne(features);
        // testBatchUntilFailure(features);
        // testBatchIndependent(features);
        // testBatchWithTickets(features);

        // // Delegation Tests
        // testConfidentialDelegation(features);
        // testDelegationRevocation(features);
        // testDelegationWithAuditor(features);
        // testDelegationClawbackIssuerOnly(features);

        // // Delegation with Tickets Tests
        // testInvalidDelegationWithTickets(features);
        // testDelegationWithTickets(features);

        // Ciphertext malleability tests
        testCiphertextMalleability(features);
        testCiphertextCombination(features);
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
