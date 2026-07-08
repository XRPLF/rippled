#include <test/jtx/Account.h>
#include <test/jtx/ConfidentialTransfer.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/apply.h>

#include <openssl/evp.h>
#include <utility/mpt_utility.h>

#include <secp256k1.h>
#include <secp256k1_mpt.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace xrpl {

class ConfidentialTransfer_test : public ConfidentialTransferTestBase
{
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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

        // Edge case: kMaxMpTokenAmount
        // Using raw JSON to avoid automatic decryption checks in MPTTester
        // which don't work for very large amounts (brute-force decryption is slow)
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, kMaxMpTokenAmount);

            mptAlice.generateKeyPair(alice);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            // First convert with amt=0 to register public key (uses MPTTester)
            mptAlice.convert({
                .account = bob,
                .amt = 0,
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            // Second convert with kMaxMpTokenAmount using raw JSON
            Buffer const blindingFactor = generateBlindingFactor();
            auto const holderCiphertext =
                mptAlice.encryptAmount(bob, kMaxMpTokenAmount, blindingFactor);
            auto const issuerCiphertext =
                mptAlice.encryptAmount(alice, kMaxMpTokenAmount, blindingFactor);

            json::Value jv;
            jv[jss::Account] = bob.human();
            jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
            jv[sfMPTokenIssuanceID] = to_string(mptAlice.issuanceID());
            jv[sfMPTAmount.jsonName] = std::to_string(kMaxMpTokenAmount);
            jv[sfHolderEncryptedAmount.jsonName] = strHex(holderCiphertext);
            jv[sfIssuerEncryptedAmount.jsonName] = strHex(issuerCiphertext);
            jv[sfBlindingFactor.jsonName] = strHex(blindingFactor);

            env(jv, Ter(tesSUCCESS));

            // Verify the public balance was reduced
            env.require(MptBalance(mptAlice, bob, 0));
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
            .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
        });

        mptAlice.authorize({
            .account = bob,
        });
        mptAlice.pay(alice, bob, 100);

        mptAlice.generateKeyPair(alice);
        mptAlice.generateKeyPair(auditor);

        mptAlice.set({
            .account = alice,
            .issuerPubKey = mptAlice.getPubKey(alice),
            .auditorPubKey = mptAlice.getPubKey(auditor),
        });

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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
                .auditorEncryptedAmt = gMakeZeroBuffer(10),
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
                .amt = kMaxMpTokenAmount + 1,
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
                .holderPubKey = gMakeZeroBuffer(kEcPubKeyLength),
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
    testConvertInvalidProofContextBinding(FeatureBitset features)
    {
        testcase("Convert proof context binding");
        using namespace test::jtx;

        auto runBadProof = [&](auto makeContextHash) {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
            });
            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(carol);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            auto const proof =
                mptAlice.getSchnorrProof(bob, makeContextHash(env, mptAlice, alice, bob, carol));
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            mptAlice.convert({
                .account = bob,
                .amt = 10,
                .proof = strHex(requireOptional(proof, "Missing proof")),
                .holderPubKey = mptAlice.getPubKey(bob),
                .err = tecBAD_PROOF,
            });
        };

        // Wrong account in the proof context.
        runBadProof([&](Env& env,
                        MPTTester const& mpt,
                        Account const&,
                        Account const& bob,
                        Account const& carol) {
            return getConvertContextHash(carol.id(), mpt.issuanceID(), env.seq(bob));
        });

        // Wrong issuance ID in the proof context.
        runBadProof([&](Env& env,
                        MPTTester const&,
                        Account const& alice,
                        Account const& bob,
                        Account const&) {
            return getConvertContextHash(
                bob.id(), makeMptID(env.seq(alice) + 100, alice), env.seq(bob));
        });

        // Wrong transaction sequence in the proof context.
        runBadProof([&](Env& env,
                        MPTTester const& mpt,
                        Account const&,
                        Account const& bob,
                        Account const&) {
            return getConvertContextHash(bob.id(), mpt.issuanceID(), env.seq(bob) + 1);
        });
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
                .mutableFlags = tmfMPTSetCanHoldConfidentialBalance,
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
                .mutableFlags = tmfMPTSetCanHoldConfidentialBalance,
                .issuerPubKey = mptAlice.getPubKey(alice),
                .auditorPubKey = mptAlice.getPubKey(auditor),
            });

            // Verify lsfMPTCanHoldConfidentialBalance flag is set
            BEAST_EXPECT(mptAlice.checkFlags(
                lsfMPTCanTransfer | lsfMPTCanLock | lsfMPTCanHoldConfidentialBalance));

            // Verify keys are persisted on the issuance
            auto const sle = env.le(keylet::mptokenIssuance(mptAlice.issuanceID()));
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
                .issuerPubKey = gMakeZeroBuffer(kEcPubKeyLength),
                .err = temMALFORMED,
            });

            // Auditor key is invalid length
            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(alice),
                .auditorPubKey = gMakeZeroBuffer(10),
                .err = temMALFORMED,
            });

            // Auditor key has correct length but invalid EC point data
            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(alice),
                .auditorPubKey = gMakeZeroBuffer(kEcPubKeyLength),
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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

            // Create with tmfMPTCannotEnableCanHoldConfidentialBalance
            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock,
                .mutableFlags = tmfMPTCannotEnableCanHoldConfidentialBalance,
            });

            mptAlice.generateKeyPair(alice);

            // Trying to enable confidential amounts and set keys fails
            // because the issuance cannot mutate canConfidentialAmount
            mptAlice.set({
                .account = alice,
                .mutableFlags = tmfMPTSetCanHoldConfidentialBalance,
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
    testTransferFee(FeatureBitset features)
    {
        testcase("test transfer fee");
        using namespace test::jtx;

        // MPTokenIssuanceCreate: cannot create with both TransferFee > 0 and
        // tfMPTCanHoldConfidentialBalance
        {
            Env env{*this, features};
            Account const alice("alice");
            MPTTester mptAlice(env, alice, {.holders = {}});

            mptAlice.create({
                .transferFee = 100,
                .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance,
                .err = temBAD_TRANSFER_FEE,
            });

            // transferFee being 0 is allowed, even with tfMPTCanHoldConfidentialBalance
            mptAlice.create({
                .transferFee = 0,
                .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance,
            });
        }

        // MPTokenIssuanceSet (preflight): cannot enable confidential amounts and
        // set TransferFee > 0 in the same transaction
        {
            Env env{*this, features};
            Account const alice("alice");
            MPTTester mptAlice(env, alice, {.holders = {}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock,
                .mutableFlags = tmfMPTCanMutateTransferFee,
            });

            mptAlice.set({
                .account = alice,
                .mutableFlags = tmfMPTSetCanHoldConfidentialBalance,
                .transferFee = 100,
                .err = temBAD_TRANSFER_FEE,
            });
        }

        // MPTokenIssuanceSet (preclaim): cannot enable confidential amounts on
        // an issuance that already has a non-zero TransferFee
        {
            Env env{*this, features};
            Account const alice("alice");
            MPTTester mptAlice(env, alice, {.holders = {}});

            mptAlice.create({
                .transferFee = 100,
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock,
                .mutableFlags = tmfMPTCanMutateTransferFee,
            });

            mptAlice.set({
                .account = alice,
                .mutableFlags = tmfMPTSetCanHoldConfidentialBalance,
                .err = tecNO_PERMISSION,
            });
        }

        // MPTokenIssuanceSet (preclaim): cannot set TransferFee > 0 on an
        // issuance that already has lsfMPTCanHoldConfidentialBalance
        {
            Env env{*this, features};
            Account const alice("alice");
            MPTTester mptAlice(env, alice, {.holders = {}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
                .mutableFlags = tmfMPTCanMutateTransferFee,
            });

            mptAlice.set({
                .account = alice,
                .transferFee = 100,
                .err = tecNO_PERMISSION,
            });

            // Setting transfer fee to 0 is allowed, but have no effect.
            mptAlice.set({
                .account = alice,
                .transferFee = 0,
            });
        }
    }

    void
    testConvertPreclaim(FeatureBitset features)
    {
        testcase("Convert preclaim");
        using namespace test::jtx;

        // tfMPTCanHoldConfidentialBalance is not set on issuance
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
            Account const carol("carol");
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(carol);

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

            std::uint64_t const amount = 10;
            Buffer const blindingFactor = generateBlindingFactor();
            Buffer const holderCiphertext = mptAlice.encryptAmount(bob, amount, blindingFactor);

            // Holder ciphertext is valid for the amount and
            // blinding factor, but the issuer ciphertext is encrypted under a
            // different public key than the registered issuer key.
            Buffer const wrongIssuerCiphertext =
                mptAlice.encryptAmount(carol, amount, blindingFactor);

            mptAlice.convert({
                .account = bob,
                .amt = amount,
                .holderPubKey = mptAlice.getPubKey(bob),
                .holderEncryptedAmt = holderCiphertext,
                .issuerEncryptedAmt = wrongIssuerCiphertext,
                .blindingFactor = blindingFactor,
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTRequireAuth |
                    tfMPTCanHoldConfidentialBalance,
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTRequireAuth |
                    tfMPTCanHoldConfidentialBalance,
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
                .proof = std::string(kEcSchnorrProofLength * 2, 'A'),
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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

            env.require(MptBalance(mptAlice, bob, 0));

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

        // Merge with an empty inbox should succeed as a no-op.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
            });
            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(alice),
            });

            mptAlice.convert({
                .account = bob,
                .amt = 40,
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            mptAlice.mergeInbox({.account = bob});
            // Inbox is empty after the first merge; the second merge is a no-op.
            mptAlice.mergeInbox({.account = bob});
        }

        // Makes sure if merge inbox version is UINT32_MAX, the next merge wraps
        // the version back to 0.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
            });
            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(alice),
            });

            mptAlice.convert({
                .account = bob,
                .amt = 40,
                .holderPubKey = mptAlice.getPubKey(bob),
            });

            // Force the on-ledger version to UINT32_MAX, then apply a merge and
            // confirm the version wraps around to 0.
            auto const wrappedFrom = std::numeric_limits<std::uint32_t>::max();
            auto const jt = env.jt(mptAlice.mergeInboxJV({.account = bob}));
            BEAST_EXPECT(env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) {
                auto const sle = std::const_pointer_cast<SLE>(
                    view.read(keylet::mptoken(mptAlice.issuanceID(), bob.id())));
                if (!sle)
                    return false;

                (*sle)[sfConfidentialBalanceVersion] = wrappedFrom;
                view.rawReplace(sle);

                auto const result = xrpl::apply(env.app(), view, *jt.stx, TapNone, env.journal);
                BEAST_EXPECT(result.ter == tesSUCCESS);
                return result.applied;
            }));

            BEAST_EXPECT(mptAlice.getMPTokenVersion(bob) == 0);
        }
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
            .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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

        // tfMPTCanHoldConfidentialBalance is not set on issuance
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance |
                    tfMPTRequireAuth,
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
        Account const alice("alice"), bob("bob"), carol("carol");
        ConfidentialEnv confEnv{
            env,
            alice,
            {{.account = bob, .payAmount = 100, .convertAmount = 60},
             {.account = carol, .payAmount = 50, .convertAmount = 20}}};
        auto& mptAlice = confEnv.mpt;

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
        ConfidentialEnv confEnv{
            env,
            alice,
            {{.account = bob, .payAmount = 100, .convertAmount = 60},
             {.account = carol, .payAmount = 50, .convertAmount = 20}},
            tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
            auditor};
        auto& mptAlice = confEnv.mpt;

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
                .senderEncryptedAmt = gMakeZeroBuffer(kEcGamalEncryptedTotalLength),
                .destEncryptedAmt = gMakeZeroBuffer(kEcGamalEncryptedTotalLength),
                .issuerEncryptedAmt = gMakeZeroBuffer(kEcGamalEncryptedTotalLength),
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
                .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance,
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

            // can not send to issuer
            mptAlice.send({
                .account = bob,
                .dest = alice,
                .amt = 10,
                .err = temMALFORMED,
            });

            // sender encrypted amount wrong length
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .senderEncryptedAmt = gMakeZeroBuffer(10),
                .err = temBAD_CIPHERTEXT,
            });

            // dest encrypted amount wrong length
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .destEncryptedAmt = gMakeZeroBuffer(10),
                .err = temBAD_CIPHERTEXT,
            });

            // issuer encrypted amount wrong length
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .issuerEncryptedAmt = gMakeZeroBuffer(10),
                .err = temBAD_CIPHERTEXT,
            });

            // sender encrypted amount malformed
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(),
                .senderEncryptedAmt = gMakeZeroBuffer(kEcGamalEncryptedTotalLength),
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
                .destEncryptedAmt = gMakeZeroBuffer(kEcGamalEncryptedTotalLength),
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
                .issuerEncryptedAmt = gMakeZeroBuffer(kEcGamalEncryptedTotalLength),
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
                .amountCommitment = gMakeZeroBuffer(100),
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
                .balanceCommitment = gMakeZeroBuffer(100),
                .err = temMALFORMED,
            });

            // amount Pedersen commitment has correct length but invalid EC point data
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(),
                .amountCommitment = gMakeZeroBuffer(kEcPedersenCommitmentLength),
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
                .balanceCommitment = gMakeZeroBuffer(kEcPedersenCommitmentLength),
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
                .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance,
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
                .auditorEncryptedAmt = gMakeZeroBuffer(10),
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
            .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTRequireAuth |
                tfMPTCanHoldConfidentialBalance,
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
                .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance,
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

            json::Value jv;
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

            env(jv, Ter(tecOBJECT_NOT_FOUND));
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

        // destination requires destination tag but none provided
        {
            env(fset(carol, asfRequireDest));
            env.close();

            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(),
                .senderEncryptedAmt = getTrivialCiphertext(),
                .destEncryptedAmt = getTrivialCiphertext(),
                .issuerEncryptedAmt = getTrivialCiphertext(),
                .amountCommitment = getTrivialCommitment(),
                .balanceCommitment = getTrivialCommitment(),
                .err = tecDST_TAG_NEEDED,
            });

            env(fclear(carol, asfRequireDest));
            env.close();
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
            ConfidentialEnv confEnv{
                env,
                alice,
                {{.account = bob, .payAmount = 100, .convertAmount = 60},
                 {.account = carol, .payAmount = 50, .convertAmount = 20}},
                tfMPTCanLock | tfMPTCanHoldConfidentialBalance};
            auto& mptAlice = confEnv.mpt;

            // bob sends 10 to carol
            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,  // will be encrypted internally
                .err = tecNO_AUTH,
            });
        }

        // Confidential MPTs should not have a transfer fee. Force malformed
        // ledger state to cover the defensive preclaim check.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            ConfidentialEnv confEnv{
                env,
                alice,
                {{.account = bob, .payAmount = 100, .convertAmount = 60},
                 {.account = carol, .payAmount = 50, .convertAmount = 20}}};
            auto& mptAlice = confEnv.mpt;

            BEAST_EXPECT(env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) {
                auto const issuance = std::const_pointer_cast<SLE>(
                    view.read(keylet::mptokenIssuance(mptAlice.issuanceID())));
                if (!issuance)
                    return false;

                issuance->setFieldU16(sfTransferFee, 1);
                view.rawReplace(issuance);
                return true;
            }));

            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = 10,
                .proof = getTrivialSendProofHex(),
                .err = tecNO_PERMISSION,
            });
        }

        // bad proof
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            ConfidentialEnv confEnv{
                env,
                alice,
                {{.account = bob, .payAmount = 100, .convertAmount = 60},
                 {.account = carol, .payAmount = 50, .convertAmount = 20}}};
            auto& mptAlice = confEnv.mpt;

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
                .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance,
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
        Account const alice("alice"), bob("bob"), carol("carol");
        ConfidentialEnv confEnv{
            env,
            alice,
            {{.account = bob, .payAmount = 1000, .convertAmount = 60},
             {.account = carol, .payAmount = 1000, .convertAmount = 50}}};
        auto& mptAlice = confEnv.mpt;

        {
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
                .amt = std::numeric_limits<std::uint64_t>::max(),
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
            // Register keys only (amt=0) for both parties — spending stays 0.
            Env env2{*this, features};
            Account const alice2("alice"), bob2("bob"), carol2("carol");
            ConfidentialEnv zeroEnv{
                env2,
                alice2,
                {{.account = bob2, .payAmount = 100, .convertAmount = 0},
                 {.account = carol2, .payAmount = 50, .convertAmount = 0}}};
            auto& mptAlice2 = zeroEnv.mpt;

            // Trying to send any amount with 0 spending balance must fail:
            // the range proof for < 0 is invalid.
            mptAlice2.send({
                .account = bob2,
                .dest = carol2,
                .amt = 1,
                .err = tecBAD_PROOF,
            });

            BEAST_EXPECT(
                mptAlice2.getDecryptedBalance(bob2, MPTTester::holderEncryptedSpending) == 0);
        }

        // todo: test m exceeding range, require using scala and refactor
    }

    /* The equality proof library and range proof library do not
     * support generating proofs for amt=0 (they require a positive witness).
     * To test the VERIFIER without crashing the helper, we bypass normal proof
     * generation by supplying explicit ciphertexts, commitments, and a dummy
     * (all-zero) proof.  The preflight has no temBAD_AMOUNT guard for
     * ConfidentialMPTSend, so all validation occurs in verifySendProofs.
     */
    void
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
            .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
        BEAST_EXPECT(mptAlice.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) == 100);
        BEAST_EXPECT(mptAlice.getDecryptedBalance(carol, MPTTester::holderEncryptedInbox) == 0);
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

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
            ConfidentialEnv confEnv{
                env, alice, {{.account = bob, .payAmount = 100, .convertAmount = 100}}};
            auto& mptAlice = confEnv.mpt;

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
            auto const share = vaultSle->at(sfShareMPTID);

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
                // Set lsfMPTCanHoldConfidentialBalance on the share issuance
                // so the invariant allows encrypted fields on the MPToken
                auto issuance =
                    std::const_pointer_cast<SLE>(view.read(keylet::mptokenIssuance(share)));
                if (!issuance)
                    return false;
                issuance->setFlag(lsfMPTCanHoldConfidentialBalance);
                view.rawReplace(issuance);

                auto const k = keylet::mptoken(share, depositor.id());
                auto const sle = std::const_pointer_cast<SLE>(view.read(k));
                if (!sle)
                    return false;
                // Inject dummy confidential balance fields
                Buffer dummyCiphertext(kEcGamalEncryptedTotalLength);
                std::memset(dummyCiphertext.data(), 0, kEcGamalEncryptedTotalLength);
                dummyCiphertext.data()[0] = kEcCompressedPrefixEvenY;
                dummyCiphertext.data()[kEcCiphertextComponentLength] = kEcCompressedPrefixEvenY;
                dummyCiphertext.data()[kEcCiphertextComponentLength - 1] = 0x01;
                dummyCiphertext.data()[kEcGamalEncryptedTotalLength - 1] = 0x01;
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
            ConfidentialEnv confEnv{
                env, alice, {{.account = bob, .payAmount = 100, .convertAmount = 40}}};
            auto& mptAlice = confEnv.mpt;

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
            ConfidentialEnv confEnv{
                env, alice, {{.account = bob, .payAmount = 2, .convertAmount = 2}}};
            auto& mptAlice = confEnv.mpt;

            mptAlice.convertBack({
                .account = bob,
                .amt = 1,
            });
        }

        // Edge case: kMaxMpTokenAmount
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
            });

            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.pay(alice, bob, kMaxMpTokenAmount);

            mptAlice.generateKeyPair(alice);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.generateKeyPair(bob);

            // Convert kMaxMpTokenAmount to confidential using raw JSON
            Buffer const convertBlindingFactor = generateBlindingFactor();
            auto const convertHolderCiphertext =
                mptAlice.encryptAmount(bob, kMaxMpTokenAmount, convertBlindingFactor);
            auto const convertIssuerCiphertext =
                mptAlice.encryptAmount(alice, kMaxMpTokenAmount, convertBlindingFactor);
            auto const convertContextHash =
                getConvertContextHash(bob.id(), mptAlice.issuanceID(), env.seq(bob));
            auto const schnorrProof = requireOptional(
                mptAlice.getSchnorrProof(bob, convertContextHash), "Missing schnorr proof");

            {
                json::Value jv;
                jv[jss::Account] = bob.human();
                jv[jss::TransactionType] = jss::ConfidentialMPTConvert;
                jv[sfMPTokenIssuanceID] = to_string(mptAlice.issuanceID());
                jv[sfMPTAmount.jsonName] = std::to_string(kMaxMpTokenAmount);
                jv[sfHolderEncryptionKey.jsonName] =
                    strHex(requireOptional(mptAlice.getPubKey(bob), "Missing holder public key"));
                jv[sfHolderEncryptedAmount.jsonName] = strHex(convertHolderCiphertext);
                jv[sfIssuerEncryptedAmount.jsonName] = strHex(convertIssuerCiphertext);
                jv[sfBlindingFactor.jsonName] = strHex(convertBlindingFactor);
                jv[sfZKProof.jsonName] = strHex(schnorrProof);

                env(jv, Ter(tesSUCCESS));
            }

            // Merge inbox using raw JSON - moves funds from inbox to spending balance
            {
                json::Value jv;
                jv[jss::Account] = bob.human();
                jv[jss::TransactionType] = jss::ConfidentialMPTMergeInbox;
                jv[sfMPTokenIssuanceID] = to_string(mptAlice.issuanceID());

                env(jv, Ter(tesSUCCESS));
            }

            // ConvertBack kMaxMpTokenAmount - 1 using raw JSON
            // After convert + merge, spending balance = kMaxMpTokenAmount
            // We convert back kMaxMpTokenAmount - 1 to leave remainder of 1
            std::uint64_t const convertBackAmt = kMaxMpTokenAmount - 1;

            Buffer const convertBackBlindingFactor = generateBlindingFactor();
            auto const convertBackHolderCiphertext =
                mptAlice.encryptAmount(bob, convertBackAmt, convertBackBlindingFactor);
            auto const convertBackIssuerCiphertext =
                mptAlice.encryptAmount(alice, convertBackAmt, convertBackBlindingFactor);

            // Get the encrypted spending balance from ledger (no decryption needed)
            auto const encryptedSpendingBalance = requireOptional(
                mptAlice.getEncryptedBalance(bob, MPTTester::holderEncryptedSpending),
                "Missing encrypted spending balance");

            // Generate pedersen commitment for the known spending balance
            Buffer const pcBlindingFactor = generateBlindingFactor();
            Buffer const pedersenCommitment =
                mptAlice.getPedersenCommitment(kMaxMpTokenAmount, pcBlindingFactor);

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
                    .amt = kMaxMpTokenAmount,
                    .encryptedAmt = encryptedSpendingBalance,
                    .blindingFactor = pcBlindingFactor,
                });

            {
                json::Value jv;
                jv[jss::Account] = bob.human();
                jv[jss::TransactionType] = jss::ConfidentialMPTConvertBack;
                jv[sfMPTokenIssuanceID] = to_string(mptAlice.issuanceID());
                jv[sfMPTAmount.jsonName] = std::to_string(convertBackAmt);
                jv[sfHolderEncryptedAmount.jsonName] = strHex(convertBackHolderCiphertext);
                jv[sfIssuerEncryptedAmount.jsonName] = strHex(convertBackIssuerCiphertext);
                jv[sfBlindingFactor.jsonName] = strHex(convertBackBlindingFactor);
                jv[sfBalanceCommitment.jsonName] = strHex(pedersenCommitment);
                jv[sfZKProof.jsonName] = strHex(proof);

                env(jv, Ter(tesSUCCESS));
            }

            // Verify the public balance was restored (minus 1 remaining in confidential)
            env.require(MptBalance(mptAlice, bob, convertBackAmt));
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
        ConfidentialEnv confEnv{
            env,
            alice,
            {{.account = bob, .payAmount = 100, .convertAmount = 40}},
            tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
            auditor};
        auto& mptAlice = confEnv.mpt;

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
            ConfidentialEnv confEnv{
                env, alice, {{.account = bob, .payAmount = 100, .convertAmount = 40}}};
            auto& mptAlice = confEnv.mpt;

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
                .amt = kMaxMpTokenAmount + 1,
                .err = temBAD_AMOUNT,
            });

            // Balance commitment has correct length but invalid EC point data
            mptAlice.convertBack({
                .account = bob,
                .amt = 30,
                .pedersenCommitment = gMakeZeroBuffer(kEcPedersenCommitmentLength),
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
                .auditorEncryptedAmt = gMakeZeroBuffer(10),
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
                .proof = gMakeZeroBuffer(100),
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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

        // tfMPTCanHoldConfidentialBalance is not set on issuance
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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

        // mptoken exists but lacks confidential fields
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance,
            });

            mptAlice.authorize({
                .account = bob,
            });

            mptAlice.pay(alice, bob, 100);
            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            // Bob's MPToken lacks the confidential fields
            auto const sleBobMpt = env.le(keylet::mptoken(mptAlice.issuanceID(), bob.id()));
            BEAST_EXPECT(sleBobMpt);
            BEAST_EXPECT(!sleBobMpt->isFieldPresent(sfHolderEncryptionKey));
            BEAST_EXPECT(!sleBobMpt->isFieldPresent(sfConfidentialBalanceSpending));
            BEAST_EXPECT(!sleBobMpt->isFieldPresent(sfIssuerEncryptedBalance));

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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTRequireAuth |
                    tfMPTCanHoldConfidentialBalance,
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
            ConfidentialEnv confEnv{
                env, alice, {{.account = bob, .payAmount = 100, .convertAmount = 50}}};
            auto& mptAlice = confEnv.mpt;

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
            ConfidentialEnv confEnv{
                env, alice, {{.account = bob, .payAmount = 100, .convertAmount = 50}}};
            auto& mptAlice = confEnv.mpt;

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
            ConfidentialEnv confEnv{
                env,
                alice,
                {{.account = bob, .payAmount = 100, .convertAmount = 50}},
                tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
                auditor};
            auto& mptAlice = confEnv.mpt;

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
            .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanClawback |
                tfMPTCanHoldConfidentialBalance,
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

        // Confidential clawback is burn/reduce outstanding amount.
        // The holder public balance is unchanged, and OA/COA decrease.
        auto const preBobPublicBalance = mptAlice.getBalance(bob);
        auto const preOutstandingAmount = mptAlice.getIssuanceOutstandingBalance();
        auto const preConfidentialOutstandingAmount = mptAlice.getIssuanceConfidentialBalance();
        BEAST_EXPECT(!env.le(keylet::mptoken(mptAlice.issuanceID(), alice.id())));

        // alice clawback all confidential balance from bob, 110 in total.
        // bob has balance in both inbox and spending. These balances should
        // become zero after clawback, which is verified in the
        // confidentialClaw function.
        mptAlice.confidentialClaw({
            .account = alice,
            .holder = bob,
            .amt = 110,
        });
        BEAST_EXPECT(mptAlice.getBalance(bob) == preBobPublicBalance);
        auto const postOutstandingAmount = mptAlice.getIssuanceOutstandingBalance();
        BEAST_EXPECT(
            preOutstandingAmount && postOutstandingAmount &&
            *postOutstandingAmount == *preOutstandingAmount - 110);
        BEAST_EXPECT(
            mptAlice.getIssuanceConfidentialBalance() == preConfidentialOutstandingAmount - 110);
        BEAST_EXPECT(!env.le(keylet::mptoken(mptAlice.issuanceID(), alice.id())));

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
            .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanClawback |
                tfMPTCanHoldConfidentialBalance,
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
    testClawbackInvalidProofContextBinding(FeatureBitset features)
    {
        testcase("ConfidentialMPTClawback context binding");
        using namespace test::jtx;

        auto runBadProof = [&](auto makeContextHash) {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            ConfidentialEnv confEnv{
                env,
                alice,
                {{.account = bob, .payAmount = 100, .convertAmount = 60}},
                tfMPTCanTransfer | tfMPTCanLock | tfMPTCanClawback |
                    tfMPTCanHoldConfidentialBalance};
            auto& mptAlice = confEnv.mpt;

            auto const privKey = mptAlice.getPrivKey(alice);
            if (!BEAST_EXPECT(privKey.has_value()))
                return;

            auto const proof = mptAlice.getClawbackProof(
                bob,
                60,
                requireOptionalRef(privKey, "Missing private key"),
                makeContextHash(env, mptAlice, alice, bob, carol));
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            mptAlice.confidentialClaw({
                .account = alice,
                .holder = bob,
                .amt = 60,
                .proof = strHex(requireOptional(proof, "Missing proof")),
                .err = tecBAD_PROOF,
            });
        };

        // Wrong account (issuer) in the proof context.
        runBadProof([&](Env& env,
                        MPTTester const& mpt,
                        Account const& alice,
                        Account const& bob,
                        Account const& carol) {
            return getClawbackContextHash(carol.id(), mpt.issuanceID(), env.seq(alice), bob.id());
        });

        // Wrong issuance ID in the proof context.
        runBadProof([&](Env& env,
                        MPTTester const&,
                        Account const& alice,
                        Account const& bob,
                        Account const&) {
            return getClawbackContextHash(
                alice.id(), makeMptID(env.seq(alice) + 100, alice), env.seq(alice), bob.id());
        });

        // Wrong transaction sequence in the proof context.
        runBadProof([&](Env& env,
                        MPTTester const& mpt,
                        Account const& alice,
                        Account const& bob,
                        Account const&) {
            return getClawbackContextHash(
                alice.id(), mpt.issuanceID(), env.seq(alice) + 1, bob.id());
        });

        // Wrong holder in the proof context.
        runBadProof([&](Env& env,
                        MPTTester const& mpt,
                        Account const& alice,
                        Account const&,
                        Account const& carol) {
            return getClawbackContextHash(alice.id(), mpt.issuanceID(), env.seq(alice), carol.id());
        });
    }

    // Bob creates the AMM, but Bob is not the MPT holder checked below.
    // The AMM has its own pseudo-account (`ammHolder`) that can hold the
    // public MPT pool balance. That pseudo-account cannot normally
    // initialize confidential state because the confidential txn's must be
    // signed by sfAccount, and the AMM pseudo-account has no signing key.
    // So this is a construction/impossibility test: public AMM MPT state exists
    // but the corresponding confidential AMM clawback flow is not normally reachable.
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
                json::Value jv;
                jv[jss::Account] = alice.human();
                jv[sfHolder] = bob.human();
                jv[jss::TransactionType] = jss::ConfidentialMPTClawback;
                jv[sfMPTAmount] = std::to_string(10);
                jv[sfZKProof] = "123";

                // wrong issuance ID
                jv[sfMPTokenIssuanceID] = "00000004AE123A8556F3CF91154711376AFB0F894F832B3E";

                env(jv, Ter(temMALFORMED));
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
                    tfMPTCanHoldConfidentialBalance,
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
                .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance,
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
                .flags = tfMPTCanClawback | tfMPTCanHoldConfidentialBalance,
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
                .flags = tfMPTCanClawback | tfMPTCanHoldConfidentialBalance,
            });
            mptAlice.authorize({
                .account = bob,
            });
            mptAlice.generateKeyPair(alice);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            // destroy the issuance
            mptAlice.destroy();

            json::Value jv;
            jv[jss::Account] = alice.human();
            jv[sfHolder] = bob.human();
            jv[jss::TransactionType] = jss::ConfidentialMPTClawback;
            jv[sfMPTAmount] = std::to_string(10);
            std::string const dummyProof(kEcClawbackProofLength * 2, '0');
            jv[sfZKProof] = dummyProof;
            jv[sfMPTokenIssuanceID] = to_string(mptAlice.issuanceID());

            env(jv, Ter(tecOBJECT_NOT_FOUND));
        }

        // After setup, bob has confidential balance 60 in spending.
        std::uint32_t const setupFlags = tfMPTCanTransfer | tfMPTCanClawback | tfMPTRequireAuth |
            tfMPTCanLock | tfMPTCanHoldConfidentialBalance;
        std::string const dummyClawbackProof(kEcClawbackProofLength * 2, '0');

        auto removeMPTokenField =
            [&](Env& env, MPTTester const& mpt, Account const& holder, SField const& field) {
                BEAST_EXPECT(env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) {
                    auto const sle = std::const_pointer_cast<SLE>(
                        view.read(keylet::mptoken(mpt.issuanceID(), holder.id())));
                    if (!sle)
                        return false;

                    sle->makeFieldAbsent(field);
                    view.rawReplace(sle);
                    return true;
                }));
            };

        // After global COA is drained to zero, a further confidential clawback
        // fails because the amount exceeds the remaining confidential
        // outstanding amount.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            ConfidentialEnv confEnv{
                env, alice, {{.account = bob, .payAmount = 100, .convertAmount = 60}}, setupFlags};
            auto& mptAlice = confEnv.mpt;

            mptAlice.confidentialClaw({
                .account = alice,
                .holder = bob,
                .amt = 60,
            });

            mptAlice.confidentialClaw({
                .account = alice,
                .holder = bob,
                .amt = 1,
                .proof = dummyClawbackProof,
                .err = tecINSUFFICIENT_FUNDS,
            });
        }

        // Missing issuer encrypted balance should fail before proof
        // verification.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            ConfidentialEnv confEnv{
                env, alice, {{.account = bob, .payAmount = 100, .convertAmount = 60}}, setupFlags};
            auto& mptAlice = confEnv.mpt;

            removeMPTokenField(env, mptAlice, bob, sfIssuerEncryptedBalance);
            mptAlice.confidentialClaw({
                .account = alice,
                .holder = bob,
                .amt = 60,
                .proof = dummyClawbackProof,
                .err = tecNO_PERMISSION,
            });
        }

        // Missing holder encryption key should fail before proof verification.
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            ConfidentialEnv confEnv{
                env, alice, {{.account = bob, .payAmount = 100, .convertAmount = 60}}, setupFlags};
            auto& mptAlice = confEnv.mpt;

            removeMPTokenField(env, mptAlice, bob, sfHolderEncryptionKey);
            mptAlice.confidentialClaw({
                .account = alice,
                .holder = bob,
                .amt = 60,
                .proof = dummyClawbackProof,
                .err = tecNO_PERMISSION,
            });
        }

        // lock should not block clawback. lock bob individually
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            ConfidentialEnv confEnv{
                env, alice, {{.account = bob, .payAmount = 100, .convertAmount = 60}}, setupFlags};
            auto& mptAlice = confEnv.mpt;
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
            ConfidentialEnv confEnv{
                env, alice, {{.account = bob, .payAmount = 100, .convertAmount = 60}}, setupFlags};
            auto& mptAlice = confEnv.mpt;
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
            ConfidentialEnv confEnv{
                env, alice, {{.account = bob, .payAmount = 100, .convertAmount = 60}}, setupFlags};
            auto& mptAlice = confEnv.mpt;

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
            ConfidentialEnv confEnv{
                env, alice, {{.account = bob, .payAmount = 100, .convertAmount = 60}}, setupFlags};
            auto& mptAlice = confEnv.mpt;

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
                .flags = tfMPTCanTransfer | tfMPTCanClawback | tfMPTCanHoldConfidentialBalance,
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

        // SCENARIO 3: the clawback proof omits the holder's confidential
        // balance version. A proof generated before the version advances is
        // still accepted, because getClawbackContextHash has no version
        // component.
        {
            Env env{*this, features};
            auto mptAlice = setupEnv(env);

            mptAlice.convert({.account = bob, .amt = 500, .holderPubKey = mptAlice.getPubKey(bob)});
            mptAlice.mergeInbox({
                .account = bob,
            });

            auto const privKey = mptAlice.getPrivKey(alice);
            if (!BEAST_EXPECT(privKey.has_value()))
                return;

            auto const proof = mptAlice.getClawbackProof(
                bob,
                500,
                requireOptionalRef(privKey, "Missing private key"),
                getClawbackContextHash(
                    alice.id(), mptAlice.issuanceID(), env.seq(alice), bob.id()));
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            // Advance bob's balance version after the proof is generated. An
            // empty-inbox merge leaves the balance unchanged but still bumps
            // sfConfidentialBalanceVersion.
            auto const versionBefore = mptAlice.getMPTokenVersion(bob);
            mptAlice.mergeInbox({.account = bob});
            BEAST_EXPECT(mptAlice.getMPTokenVersion(bob) != versionBefore);

            // The stale-version proof is still accepted.
            mptAlice.confidentialClaw({
                .account = alice,
                .holder = bob,
                .amt = 500,
                .proof = strHex(requireOptional(proof, "Missing proof")),
            });
        }
    }

    void
    testPublicTransfersAfterClearingConfidentialFlag(FeatureBitset features)
    {
        testcase("Public transfers after clearing Confidential Flag");
        using namespace test::jtx;

        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");

        // After clearing the confidential flag, all four public MPT operations
        // must succeed regardless of which confidential path left encrypted-zero
        // fields on bob's MPToken.
        auto runPublicPayments = [&](MPTTester& mpt) {
            mpt.pay(bob, carol, 10);
            mpt.pay(carol, bob, 5);
            mpt.pay(alice, bob, 1);
            mpt.pay(carol, alice, 5);
        };

        auto drainAndDeleteBobMPToken = [&](Env& env, MPTTester& mpt) {
            auto const bobBalance = mpt.getBalance(bob);
            BEAST_EXPECT(bobBalance > 0);

            mpt.pay(bob, alice, bobBalance);
            BEAST_EXPECT(mpt.getBalance(bob) == 0);

            mpt.authorize({.account = bob, .flags = tfMPTUnauthorize});
            BEAST_EXPECT(!env.le(keylet::mptoken(mpt.issuanceID(), bob.id())));
        };

        // Alice pays Bob 100 public, Bob converts 50 confidential
        // Bob converts 50 back to public, and make sure can receive public payments
        {
            Env env{*this, features};
            ConfidentialEnv ct{
                env,
                alice,
                {{.account = bob, .payAmount = 100, .convertAmount = 50}},
                tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance};

            env.fund(XRP(1'000), carol);
            ct.mpt.authorize({.account = carol});
            ct.mpt.pay(alice, carol, 50);

            ct.mpt.convertBack({.account = bob, .amt = 50});

            runPublicPayments(ct.mpt);
            drainAndDeleteBobMPToken(env, ct.mpt);
        }

        // Same path as above but with Auditor
        {
            Env env{*this, features};
            Account const auditor("auditor");
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}, .auditor = auditor});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance,
            });

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});
            mptAlice.pay(alice, bob, 100);
            mptAlice.pay(alice, carol, 50);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(auditor);
            mptAlice.set(
                {.account = alice,
                 .issuerPubKey = mptAlice.getPubKey(alice),
                 .auditorPubKey = mptAlice.getPubKey(auditor)});

            mptAlice.convert({
                .account = bob,
                .amt = 50,
                .holderPubKey = mptAlice.getPubKey(bob),
            });
            mptAlice.mergeInbox({.account = bob});
            mptAlice.convertBack({.account = bob, .amt = 50});

            runPublicPayments(mptAlice);
            drainAndDeleteBobMPToken(env, mptAlice);
        }

        // Confidential clawback leaves encrypted-zero fields;
        // the public balance remaining after the clawback must stay usable.
        {
            Env env{*this, features};
            ConfidentialEnv ct{
                env,
                alice,
                {{.account = bob, .payAmount = 100, .convertAmount = 50}},
                tfMPTCanTransfer | tfMPTCanClawback | tfMPTCanHoldConfidentialBalance};

            env.fund(XRP(1'000), carol);
            ct.mpt.authorize({.account = carol});
            ct.mpt.pay(alice, carol, 50);

            ct.mpt.confidentialClaw({.account = alice, .holder = bob, .amt = 50});

            runPublicPayments(ct.mpt);
            drainAndDeleteBobMPToken(env, ct.mpt);
        }
    }

    void
    testMutatePrivacy(FeatureBitset features)
    {
        testcase("mutate lsfMPTCanHoldConfidentialBalance");
        using namespace test::jtx;

        // can not create mpt issuance with tmfMPTCannotEnableCanHoldConfidentialBalance
        // when featureDynamicMPT is disabled
        {
            Env env{*this, features - featureDynamicMPT};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 0,
                .mutableFlags = tmfMPTCannotEnableCanHoldConfidentialBalance,
                .err = temDISABLED,
            });
        }

        // can not create mpt issuance with tmfMPTCannotEnableCanHoldConfidentialBalance when
        // featureConfidentialTransfer is disabled
        {
            Env env{*this, features - featureConfidentialTransfer};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 0,
                .mutableFlags = tmfMPTCannotEnableCanHoldConfidentialBalance,
                .err = temDISABLED,
            });
        }

        // if lsmfMPTCannotEnableCanHoldConfidentialBalance is set, can not set/clear
        // lsfMPTCanHoldConfidentialBalance
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer,
                .mutableFlags = tmfMPTCannotEnableCanHoldConfidentialBalance,
            });

            mptAlice.set({
                .account = alice,
                .mutableFlags = tmfMPTSetCanHoldConfidentialBalance,
                .err = tecNO_PERMISSION,
            });
        }

        // Toggle lsfMPTCanHoldConfidentialBalance
        {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            MPTTester mptAlice(env, alice, {.holders = {bob}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance,
                .mutableFlags = tmfMPTCanEnableCanLock,
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
                {
                    mptAlice.convert({
                        .account = bob,
                        .amt = amt,
                        .holderPubKey = mptAlice.getPubKey(bob),
                        .err = expectedResult,
                    });
                }
                else
                {
                    mptAlice.convert({
                        .account = bob,
                        .amt = amt,
                        .err = expectedResult,
                    });
                }

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

            // set lsfMPTCanHoldConfidentialBalance, but no effect because
            // lsfMPTCanHoldConfidentialBalance was already set
            mptAlice.set({
                .account = alice,
                .mutableFlags = tmfMPTSetCanHoldConfidentialBalance,
            });
            verifyToggle(tesSUCCESS, 10);

            // set tmfMPTSetCanHoldConfidentialBalance again
            mptAlice.set({
                .account = alice,
                .mutableFlags = tmfMPTSetCanHoldConfidentialBalance,
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

            // lsmfMPTCannotEnableCanHoldConfidentialBalance is false by default,
            // so that lsfMPTCanHoldConfidentialBalance can be mutated
            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance,
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

            // set lsfMPTCanHoldConfidentialBalance should fail because of
            // confidential outstanding balance
            mptAlice.set({
                .account = alice,
                .mutableFlags = tmfMPTSetCanHoldConfidentialBalance,
                .err = tecNO_PERMISSION,
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
        ConfidentialEnv confEnv{
            env, alice, {{.account = bob, .payAmount = 100, .convertAmount = 40}}};
        auto& mptAlice = confEnv.mpt;

        // for ease of understanding, generate all the fields here instead of
        // autofilling
        uint64_t const amt = 10;
        Buffer const blindingFactor = generateBlindingFactor();
        Buffer const pcBlindingFactor = generateBlindingFactor();

        auto const spendingBalance = requireOptional(
            mptAlice.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending),
            "Missing spending balance");
        auto const encryptedSpendingBalance = requireOptional(
            mptAlice.getEncryptedBalance(bob, MPTTester::holderEncryptedSpending),
            "Missing encrypted spending balance");
        BEAST_EXPECT(!encryptedSpendingBalance.empty());

        Buffer const pedersenCommitment =
            mptAlice.getPedersenCommitment(spendingBalance, pcBlindingFactor);
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
                    .amt = spendingBalance,
                    .encryptedAmt = encryptedSpendingBalance,
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
                    .amt = spendingBalance,
                    .encryptedAmt = encryptedSpendingBalance,
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
                    .encryptedAmt = encryptedSpendingBalance,
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
                    .amt = spendingBalance,
                    .encryptedAmt = encryptedSpendingBalance,
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
                    .amt = spendingBalance,
                    .encryptedAmt = encryptedSpendingBalance,
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
                    .amt = spendingBalance,
                    .encryptedAmt = encryptedSpendingBalance,
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
        ConfidentialEnv confEnv{
            env, alice, {{.account = bob, .payAmount = 100, .convertAmount = 40}}};
        auto& mptAlice = confEnv.mpt;

        // for ease of understanding, generate all the fields here instead of
        // autofilling
        uint64_t const amt = 10;
        Buffer const blindingFactor = generateBlindingFactor();
        Buffer const pcBlindingFactor = generateBlindingFactor();

        auto const spendingBalance = requireOptional(
            mptAlice.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending),
            "Missing spending balance");
        auto const encryptedSpendingBalance = requireOptional(
            mptAlice.getEncryptedBalance(bob, MPTTester::holderEncryptedSpending),
            "Missing encrypted spending balance");
        BEAST_EXPECT(!encryptedSpendingBalance.empty());

        Buffer const pedersenCommitment =
            mptAlice.getPedersenCommitment(spendingBalance, pcBlindingFactor);
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
                    .encryptedAmt = encryptedSpendingBalance,
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
                    .amt = spendingBalance,
                    .encryptedAmt = encryptedSpendingBalance,
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
                    .amt = spendingBalance,
                    .encryptedAmt = encryptedSpendingBalance,
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
                    .amt = spendingBalance,
                    .encryptedAmt = encryptedSpendingBalance,
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

    // A convert-back proof is bound to (account, issuance, sequence, version) via
    // the Fiat-Shamir context hash. Crafting a proof against any single wrong
    // variable and submitting it with the real parameters must be rejected
    // with tecBAD_PROOF
    void
    testConvertBackInvalidProofContextBinding(FeatureBitset features)
    {
        testcase("ConvertBack proof context binding");
        using namespace test::jtx;

        auto runBadProof = [&](auto makeContextHash) {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            ConfidentialEnv confEnv{
                env, alice, {{.account = bob, .payAmount = 100, .convertAmount = 40}}};
            auto& mptAlice = confEnv.mpt;

            std::uint64_t const amt = 10;
            Buffer const blindingFactor = generateBlindingFactor();
            Buffer const pcBlindingFactor = generateBlindingFactor();

            auto const spendingBalance =
                mptAlice.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending);
            auto const encryptedSpendingBalance =
                mptAlice.getEncryptedBalance(bob, MPTTester::holderEncryptedSpending);
            if (!BEAST_EXPECT(spendingBalance && encryptedSpendingBalance))
                return;

            Buffer const pedersenCommitment = mptAlice.getPedersenCommitment(
                requireOptional(spendingBalance, "Missing spending balance"), pcBlindingFactor);
            Buffer const issuerCiphertext = mptAlice.encryptAmount(alice, amt, blindingFactor);
            Buffer const bobCiphertext = mptAlice.encryptAmount(bob, amt, blindingFactor);
            auto const version = mptAlice.getMPTokenVersion(bob);

            Buffer const proof = mptAlice.getConvertBackProof(
                bob,
                amt,
                makeContextHash(env, mptAlice, alice, bob, carol, version),
                {
                    .pedersenCommitment = pedersenCommitment,
                    .amt = requireOptional(spendingBalance, "Missing spending balance"),
                    .encryptedAmt = requireOptionalRef(
                        encryptedSpendingBalance, "Missing encrypted spending balance"),
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
        };

        // Wrong account in the proof context.
        runBadProof([&](Env& env,
                        MPTTester const& mpt,
                        Account const&,
                        Account const& bob,
                        Account const& carol,
                        std::uint32_t version) {
            return getConvertBackContextHash(carol.id(), mpt.issuanceID(), env.seq(bob), version);
        });

        // Wrong issuance ID in the proof context.
        runBadProof([&](Env& env,
                        MPTTester const&,
                        Account const& alice,
                        Account const& bob,
                        Account const&,
                        std::uint32_t version) {
            return getConvertBackContextHash(
                bob.id(), makeMptID(env.seq(alice) + 100, alice), env.seq(bob), version);
        });

        // Wrong transaction sequence in the proof context.
        runBadProof([&](Env& env,
                        MPTTester const& mpt,
                        Account const&,
                        Account const& bob,
                        Account const&,
                        std::uint32_t version) {
            return getConvertBackContextHash(bob.id(), mpt.issuanceID(), env.seq(bob) + 1, version);
        });

        // Wrong balance version in the proof context.
        runBadProof([&](Env& env,
                        MPTTester const& mpt,
                        Account const&,
                        Account const& bob,
                        Account const&,
                        std::uint32_t version) {
            return getConvertBackContextHash(bob.id(), mpt.issuanceID(), env.seq(bob), version + 1);
        });
    }

    // This test simulates a valid proof π extracted from a transaction
    // for amount m1 is reused in a new transaction for a different
    // amount m2 with different ciphertexts. It confirms the context hash
    // recomputation fails due to the ciphertext binding mismatch, resulting
    // in tecBAD_PROOF.
    void
    testConvertBackProofCiphertextBinding(FeatureBitset features)
    {
        testcase("ConvertBack: proof ciphertext binding");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice"), bob("bob");
        ConfidentialEnv confEnv{
            env, alice, {{.account = bob, .payAmount = 100, .convertAmount = 50}}};
        auto& mptAlice = confEnv.mpt;

        auto const spendingBalance = requireOptional(
            mptAlice.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending),
            "Missing spending balance");
        auto const encryptedSpendingBalance = requireOptional(
            mptAlice.getEncryptedBalance(bob, MPTTester::holderEncryptedSpending),
            "Missing encrypted spending balance");
        auto const version = mptAlice.getMPTokenVersion(bob);
        Buffer const pcBlindingFactor = generateBlindingFactor();
        Buffer const pedersenCommitment =
            mptAlice.getPedersenCommitment(spendingBalance, pcBlindingFactor);

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
                .amt = spendingBalance,
                .encryptedAmt = encryptedSpendingBalance,
                .blindingFactor = pcBlindingFactor,
            });

        // Construct Transaction B with Amount m2 = 20 and attach Proof pi
        uint64_t const amtB = 20;
        Buffer const blindingFactorB = generateBlindingFactor();
        Buffer const bobCiphertextB = mptAlice.encryptAmount(bob, amtB, blindingFactorB);
        Buffer const issuerCiphertextB = mptAlice.encryptAmount(alice, amtB, blindingFactorB);

        // We attempt to verify the proof pi (for amt 10) against the new ciphertexts (for amt 20).
        mptAlice.convertBack({
            .account = bob,
            .amt = amtB,
            .proof = proofA,  // Extracted/Reused proof from Transaction A
            .holderEncryptedAmt = bobCiphertextB,
            .issuerEncryptedAmt = issuerCiphertextB,
            .blindingFactor = blindingFactorB,
            .pedersenCommitment = pedersenCommitment,
            .err = tecBAD_PROOF,  // Expected failure
        });
    }

    // This test simulates a valid proof π and ciphertext are
    // tied to version v, but are reused after an inbox merge has incremented
    // the CBS version to v+1. It confirms the validator rejects the transaction
    // before acceptance due to the ContextID mismatch.
    void
    testConvertBackProofVersionMismatch(FeatureBitset features)
    {
        testcase("ConvertBack: proof version mismatch");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice"), bob("bob");
        ConfidentialEnv confEnv{
            env, alice, {{.account = bob, .payAmount = 1000, .convertAmount = 100}}};
        auto& mptAlice = confEnv.mpt;

        auto const versionV = mptAlice.getMPTokenVersion(bob);
        auto const spendingBalanceV = requireOptional(
            mptAlice.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending),
            "Missing spending balance");
        auto const encryptedSpendingBalanceV = requireOptional(
            mptAlice.getEncryptedBalance(bob, MPTTester::holderEncryptedSpending),
            "Missing encrypted spending balance");

        // Parameters for the intended ConvertBack transaction
        uint64_t const amt = 10;
        Buffer const blindingFactor = generateBlindingFactor();
        Buffer const pcBlindingFactor = generateBlindingFactor();
        Buffer const pedersenCommitment =
            mptAlice.getPedersenCommitment(spendingBalanceV, pcBlindingFactor);
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
                .amt = spendingBalanceV,
                .encryptedAmt = encryptedSpendingBalanceV,
                .blindingFactor = pcBlindingFactor,
            });

        // Submit and verify failure
        mptAlice.convertBack({
            .account = bob,
            .amt = amt,
            .proof = oldProof,
            .holderEncryptedAmt = bobCiphertext,
            .issuerEncryptedAmt = issuerCiphertext,
            .blindingFactor = blindingFactor,
            .pedersenCommitment = pedersenCommitment,
            .err = tecBAD_PROOF,  // Fails because TransactionContextID differs
        });
    }

    /* This test simulates an attack where the holder ciphertext is modified
     * via homomorphic addition (adding Encrypted_amt(1)) while leaving the issuer
     * ciphertext unchanged. It confirms that the validator detects the
     * mismatch between the re-computed ciphertexts and the submitted ones,
     * resulting in tecBAD_PROOF.   */
    void
    testConvertBackHomomorphicCiphertextModification(FeatureBitset features)
    {
        testcase("ConvertBack: homomorphic ciphertext modification");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice"), bob("bob");
        ConfidentialEnv confEnv{
            env, alice, {{.account = bob, .payAmount = 100, .convertAmount = 50}}};
        auto& mptAlice = confEnv.mpt;

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
        Buffer tamperedHolderCipherText = requireOptional(
            homomorphicAdd(holderCipherText, deltaCipherText), "Missing tampered ciphertext");

        // Generate a valid proof for the ORIGINAL amount (10)
        auto const spendingBal = requireOptional(
            mptAlice.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending),
            "Missing spending balance");
        auto const spendingBalEnc = requireOptional(
            mptAlice.getEncryptedBalance(bob, MPTTester::holderEncryptedSpending),
            "Missing encrypted spending balance");
        Buffer const pcBf = generateBlindingFactor();
        auto const pedersenCommitment = mptAlice.getPedersenCommitment(spendingBal, pcBf);

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
                .amt = spendingBal,
                .encryptedAmt = spendingBalEnc,
                .blindingFactor = pcBf,
            });

        // Submit transaction with Divergent Ciphertexts
        // Holder Ciphertext encrypts 11. Issuer Ciphertext encrypts 10.
        // The consistency check (re-encryption of `amt` with `bf`) will match Issuer but FAIL for
        // Holder.
        mptAlice.convertBack({
            .account = bob,
            .amt = amt,
            .proof = proof,
            .holderEncryptedAmt = tamperedHolderCipherText,  // Tampered (11)
            .issuerEncryptedAmt = issuerCipherText,          // Original (10)
            .blindingFactor = bf,
            .pedersenCommitment = pedersenCommitment,
            .err = tecBAD_PROOF,
        });
    }

    /* This test verifies that xrpld correctly rejects attempts to
     * overflow the maximum allowable token amount via homomorphic manipulation.
     * It simulates an attack where an individual takes a valid ciphertext encrypting
     * the maximum amount (kMaxMpTokenAmount) and homomorphically adds an encryption of
     * 1 to it, producing a ciphertext for MAX+1. The test confirms that the Bulletproof
     * range proof or inner-product constraints detect this overflow and invalidate the
     * transaction, preserving the supply invariant. */
    void
    testSendHomomorphicOverflow(FeatureBitset features)
    {
        testcase("Send: homomorphic overflow attack via Enc(MAX) + Enc(1)");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice"), bob("bob"), carol("carol");
        ConfidentialEnv confEnv{
            env,
            alice,
            {{.account = bob, .payAmount = 100, .convertAmount = 100},
             {.account = carol, .payAmount = 50, .convertAmount = 50}}};
        auto& mptAlice = confEnv.mpt;

        // Bob sends 10 to carol.  The send amount (10) and Bob's remaining balance
        // (90) are both within [0, kMaxMpTokenAmount].  Range proof passes.
        mptAlice.send({.account = bob, .dest = carol, .amt = 10});

        // Bob's spending balance is 90 after the baseline send.
        auto const bobSpendingBefore =
            mptAlice.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending);
        BEAST_EXPECT(bobSpendingBefore == 90);

        // Construct Enc(kMaxMpTokenAmount) with Bob's public key.
        Buffer const bf1 = generateBlindingFactor();
        Buffer const encMax = mptAlice.encryptAmount(bob, kMaxMpTokenAmount, bf1);

        // Construct Enc(1) with a separate blinding factor.
        Buffer const bf2 = generateBlindingFactor();
        Buffer const encOne = mptAlice.encryptAmount(bob, 1, bf2);

        // Homomorphically add to produce CB_S_holder' = Enc(MAX) + Enc(1)
        Buffer overflowedCt =
            requireOptional(homomorphicAdd(encMax, encOne), "Missing overflowed ciphertext");

        // Submit the send transaction with the tampered ciphertext.
        // Setting amt = kMaxMpTokenAmount + 1 drives proof generation for the
        // overflowed value.  The bulletproof range check [0, kMaxMpTokenAmount]
        // rejects MAX+1; the validator must return tecBAD_PROOF.
        mptAlice.send({
            .account = bob,
            .dest = carol,
            .amt = kMaxMpTokenAmount + 1,
            .senderEncryptedAmt = overflowedCt,
            .err = tecBAD_PROOF,
        });

        auto const bobSpendingAfter =
            mptAlice.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending);
        BEAST_EXPECT(bobSpendingBefore == bobSpendingAfter);
    }

    /* This test ensures that the system prevents underflow attacks where a user
     * attempts to create a negative balance through homomorphic subtraction. It
     * simulates a scenario where an attacker takes a ciphertext encrypting zero
     * and subtracts an encryption of 1, resulting in a value of -1.
     * The test asserts that the range proof verification fails because the resulting
     * value falls outside the valid non-negative range [0, kMaxMpTokenAmount],
     * causing the validator to reject the transaction with tecBAD_PROOF. */
    void
    testConvertBackHomomorphicUnderflow(FeatureBitset features)
    {
        testcase("ConvertBack: homomorphic underflow attack via Enc(0) - Enc(1)");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice"), bob("bob");
        ConfidentialEnv confEnv{
            env, alice, {{.account = bob, .payAmount = 10, .convertAmount = 10}}};
        auto& mptAlice = confEnv.mpt;

        // Converting back 1 from 10 leaves remaining balance = 9 (non-negative).
        // Range proof [0, kMaxMpTokenAmount] passes.
        mptAlice.convertBack({.account = bob, .amt = 1});

        // Bob's spending balance is now 9; public balance is 1.
        auto const bobSpendingBefore =
            mptAlice.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending);
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
        // = Enc(−1), which lies below [0, kMaxMpTokenAmount].
        Buffer underflowedCt =
            requireOptional(homomorphicSubtract(encZero, encOne), "Missing underflowed ciphertext");

        // The underflowed value as uint64_t: 0 - 1 wraps to 0xFFFFFFFFFFFFFFFF.
        // Generate a real proof using this wrapped value. The validator must still reject it
        // because 0xFFFFFFFFFFFFFFFE (remaining balance) is outside [0, kMaxMpTokenAmount].
        constexpr std::uint64_t kUnderflowedAmt =
            static_cast<std::uint64_t>(0) - static_cast<std::uint64_t>(1);

        Buffer const pcBf = generateBlindingFactor();
        Buffer const pedersenCommitment = mptAlice.getPedersenCommitment(kUnderflowedAmt, pcBf);

        auto const currentVersion = mptAlice.getMPTokenVersion(bob);
        uint256 const contextHash =
            getConvertBackContextHash(bob, mptAlice.issuanceID(), env.seq(bob), currentVersion);

        Buffer const proof = mptAlice.getConvertBackProof(
            bob,
            1,
            contextHash,
            {
                .pedersenCommitment = pedersenCommitment,
                .amt = kUnderflowedAmt,
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
            mptAlice.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending);
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
            Account const alice("alice"), bob("bob"), carol("carol");
            Env env{*this, features};
            ConfidentialEnv confEnv{
                env,
                alice,
                {{.account = bob, .payAmount = 100, .convertAmount = 60},
                 {.account = carol, .payAmount = 50, .convertAmount = 30}}};
            auto& mptAlice = confEnv.mpt;

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
            Buffer badCommitment(kEcPedersenCommitmentLength);
            std::memset(badCommitment.data(), 0xFF, kEcPedersenCommitmentLength);
            badCommitment.data()[0] = kEcCompressedPrefixEvenY;

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
            Account const alice("alice"), bob("bob"), carol("carol");
            Env env{*this, features};
            ConfidentialEnv confEnv{
                env,
                alice,
                {{.account = bob, .payAmount = 100, .convertAmount = 60},
                 {.account = carol, .payAmount = 50, .convertAmount = 30}}};
            auto& mptAlice = confEnv.mpt;

            Buffer badProof(kEcSendProofLength);
            std::memset(badProof.data(), 0xFF, kEcSendProofLength);
            badProof.data()[0] = kEcCompressedPrefixEvenY;

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
            Account const alice("alice"), bob("bob"), carol("carol");
            Env env{*this, features};
            ConfidentialEnv confEnv{
                env,
                alice,
                {{.account = bob, .payAmount = 100, .convertAmount = 60},
                 {.account = carol, .payAmount = 50, .convertAmount = 30}}};
            auto& mptAlice = confEnv.mpt;

            // getTrivialCiphertext() has both C1 and C2 as valid (but trivial)
            // curve coordinates.  We replace one half at a time with 0xFF...FF.
            auto const& tc = getTrivialCiphertext();

            // C1 = bad (0xFF...FF), C2 = valid trivial point
            Buffer badC1goodC2(kEcGamalEncryptedTotalLength);
            std::memset(badC1goodC2.data(), 0xFF, kEcGamalEncryptedTotalLength);
            badC1goodC2.data()[0] = kEcCompressedPrefixEvenY;
            std::memcpy(
                badC1goodC2.data() + kEcCiphertextComponentLength,
                tc.data() + kEcCiphertextComponentLength,
                kEcCiphertextComponentLength);

            // C1 = valid trivial point, C2 = bad (0xFF...FF)
            Buffer goodC1badC2(kEcGamalEncryptedTotalLength);
            std::memset(goodC1badC2.data(), 0xFF, kEcGamalEncryptedTotalLength);
            std::memcpy(goodC1badC2.data(), tc.data(), kEcCiphertextComponentLength);
            goodC1badC2.data()[kEcCiphertextComponentLength] = kEcCompressedPrefixEvenY;

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
        ConfidentialEnv confEnv{
            env,
            alice,
            {{.account = bob, .payAmount = 100, .convertAmount = 60},
             {.account = carol, .payAmount = 50, .convertAmount = 30}}};
        auto& mptAlice = confEnv.mpt;

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
        Buffer wrongGroupCt(kEcGamalEncryptedTotalLength);
        wrongGroupCt.data()[0] = kEcCompressedPrefixEvenY;
        std::memcpy(wrongGroupCt.data() + 1, kP256GeneratorX, 32);
        wrongGroupCt.data()[kEcCiphertextComponentLength] = kEcCompressedPrefixEvenY;
        std::memcpy(wrongGroupCt.data() + kEcCiphertextComponentLength + 1, kP256GeneratorX, 32);

        // A 33-byte commitment using the same wrong-curve x-coordinate.
        Buffer wrongGroupCommitment(kEcPedersenCommitmentLength);
        wrongGroupCommitment.data()[0] = kEcCompressedPrefixEvenY;
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
    testConvertIdentityElementRejection(FeatureBitset features)
    {
        testcase("Convert: all-zero public key rejected");
        using namespace test::jtx;

        // 33 zero bytes — not a real public key; no valid secret maps to this.
        Buffer const nullKey = gMakeZeroBuffer(kEcPubKeyLength);

        // Recipient (holder) tries to register an all-zero key.
        // Must be rejected so no account ends up with an unprotected balance.
        {
            Env env{*this, features};
            Account const alice("alice"), bob("bob"), carol("carol");
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});
            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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
        Account const alice("alice"), bob("bob"), carol("carol");
        ConfidentialEnv confEnv{
            env,
            alice,
            {{.account = bob, .payAmount = 100, .convertAmount = 100},
             {.account = carol, .payAmount = 50, .convertAmount = 50}}};
        auto& mptAlice = confEnv.mpt;

        auto const bobSpendingBefore =
            mptAlice.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending);

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
            mptAlice.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending) ==
            bobSpendingBefore);
        BEAST_EXPECT(mptAlice.getDecryptedBalance(carol, MPTTester::holderEncryptedInbox) == 0);
    }

    // This test verifies that the compact AND-composed Send sigma proof
    // enforces the shared-randomness invariant across participants.
    void
    testSendSharedRandomnessViolation(FeatureBitset features)
    {
        testcase("divergent C1 across participants in ConfidentialMPTSend");
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Account const auditor("auditor");
        ConfidentialEnv confEnv{
            env,
            alice,
            {{.account = bob, .payAmount = 100, .convertAmount = 50},
             {.account = carol, .payAmount = 50, .convertAmount = 50}},
            tfMPTCanLock | tfMPTCanHoldConfidentialBalance | tfMPTCanTransfer,
            auditor};
        auto& mptAlice = confEnv.mpt;

        // Send amount is 10.
        uint64_t const amt = 10;

        enum class Participant { Sender, Dest, Issuer, Auditor };

        // This lambda submits a send transaction where one of the four ciphertexts
        // is encrypted with different randomness than the one used to build the proof.
        // Note: When divergent is nullopt, all participants
        // will use the same randomness and expected to succeed, this is the
        // control case that confirms the test setup itself is sound, the bad proof
        // is actually from divergent randomness, not other causes.
        auto submitWithDivergentC1 = [&](std::optional<Participant> divergent) {
            ConfidentialSendSetup setup(mptAlice, bob, carol, alice, amt, std::cref(auditor));

            auto const proofOpt =
                requireOptional(setup.generateProof(mptAlice, env, bob, carol), "Missing proof");

            // Re-encrypt one participant's ciphertext with divergent randomness.
            Buffer senderCt = setup.senderAmt;
            Buffer destCt = setup.destAmt;
            Buffer issuerCt = setup.issuerAmt;
            Buffer auditorCt =
                requireOptionalRef(setup.auditorAmt, "Missing auditor encrypted amount");
            if (divergent)
            {
                Buffer const bfDivergent = generateBlindingFactor();
                switch (*divergent)
                {
                    case Participant::Sender:
                        senderCt = mptAlice.encryptAmount(bob, amt, bfDivergent);
                        break;
                    case Participant::Dest:
                        destCt = mptAlice.encryptAmount(carol, amt, bfDivergent);
                        break;
                    case Participant::Issuer:
                        issuerCt = mptAlice.encryptAmount(alice, amt, bfDivergent);
                        break;
                    case Participant::Auditor:
                        auditorCt = mptAlice.encryptAmount(auditor, amt, bfDivergent);
                        break;
                }
            }

            TER const expectedErr = divergent ? TER{tecBAD_PROOF} : TER{tesSUCCESS};

            mptAlice.send({
                .account = bob,
                .dest = carol,
                .amt = amt,
                .proof = strHex(proofOpt),
                .senderEncryptedAmt = senderCt,
                .destEncryptedAmt = destCt,
                .issuerEncryptedAmt = issuerCt,
                .auditorEncryptedAmt = auditorCt,
                .blindingFactor = setup.blindingFactor,
                .amountCommitment = setup.amountCommitment,
                .balanceCommitment = setup.balanceCommitment,
                .err = expectedErr,
            });

            // Verify balances.
            auto const spendingAfter =
                mptAlice.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending);
            if (divergent)
            {
                BEAST_EXPECT(spendingAfter == setup.prevSpending);
            }
            else
            {
                BEAST_EXPECT(spendingAfter == setup.prevSpending - amt);
            }
        };

        // This confirms the test setup is sound, if any of the divergent cases below
        // fail, it is due to the C1 mismatch and not a setup bug.
        submitWithDivergentC1(std::nullopt);

        // Divergent C1 for different participants should all fail with tecBAD_PROOF:
        submitWithDivergentC1(Participant::Sender);
        submitWithDivergentC1(Participant::Dest);
        submitWithDivergentC1(Participant::Issuer);
        submitWithDivergentC1(Participant::Auditor);
    }

    void
    testConfidentialMPTBaseFee(FeatureBitset features)
    {
        testcase("test confidential transactions fee");
        using namespace test::jtx;

        auto setup =
            [&](MPTTester& mpt, Account const& alice, Account const& bob, Account const& carol) {
                mpt.create({
                    .ownerCount = 1,
                    .flags = tfMPTCanLock | tfMPTCanHoldConfidentialBalance | tfMPTCanTransfer |
                        tfMPTCanClawback,
                });
                mpt.authorize({.account = bob});
                mpt.authorize({.account = carol});
                mpt.pay(alice, bob, 100);
                mpt.pay(alice, carol, 50);
                mpt.generateKeyPair(alice);
                mpt.generateKeyPair(bob);
                mpt.generateKeyPair(carol);
                mpt.set({.account = alice, .issuerPubKey = mpt.getPubKey(alice)});
            };

        // test expected base fee for confidential transactions
        {
            Env env{*this, features};
            Account const alice("alice"), bob("bob"), carol("carol");
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});
            setup(mptAlice, alice, bob, carol);

            auto const baseFee = env.current()->fees().base;
            auto const expectedFee = baseFee * (kConfidentialFeeMultiplier + 1);

            // lambda function to submit confidential transaction and check fee charged to the
            // account
            auto checkFee = [&](Account const& acct, auto&& submitFn) {
                auto const before = env.balance(acct);
                submitFn();
                auto const after = env.balance(acct);
                BEAST_EXPECT(before - after == expectedFee);
            };

            checkFee(bob, [&]() {
                mptAlice.convert(
                    {.account = bob,
                     .amt = 50,
                     .holderPubKey = mptAlice.getPubKey(bob),
                     .fee = expectedFee});
            });
            checkFee(carol, [&]() {
                mptAlice.convert(
                    {.account = carol,
                     .amt = 10,
                     .holderPubKey = mptAlice.getPubKey(carol),
                     .fee = expectedFee});
            });
            checkFee(bob, [&]() { mptAlice.mergeInbox({.account = bob, .fee = expectedFee}); });
            checkFee(carol, [&]() { mptAlice.mergeInbox({.account = carol, .fee = expectedFee}); });
            checkFee(bob, [&]() {
                mptAlice.send({.account = bob, .dest = carol, .amt = 5, .fee = expectedFee});
            });
            checkFee(bob, [&]() {
                mptAlice.convertBack({.account = bob, .amt = 5, .fee = expectedFee});
            });
            checkFee(alice, [&]() {
                mptAlice.confidentialClaw(
                    {.account = alice, .holder = carol, .amt = 15, .fee = expectedFee});
            });
        }

        // test insufficient fee for confidential transactions
        {
            Env env{*this, features};
            Account const alice("alice"), bob("bob"), carol("carol");
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});
            setup(mptAlice, alice, bob, carol);
            auto const baseFee = env.current()->fees().base;
            auto const expectedFee = baseFee * (kConfidentialFeeMultiplier + 1);

            mptAlice.convert(
                {.account = bob,
                 .amt = 1,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .fee = expectedFee - 1,
                 .err = telINSUF_FEE_P});
            mptAlice.mergeInbox({.account = bob, .fee = baseFee, .err = telINSUF_FEE_P});
            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = 1,
                 .fee = baseFee * kConfidentialFeeMultiplier,
                 .err = telINSUF_FEE_P});
            mptAlice.convertBack({.account = bob, .amt = 1, .fee = baseFee, .err = telINSUF_FEE_P});
            mptAlice.confidentialClaw(
                {.account = alice,
                 .holder = carol,
                 .amt = 1,
                 .fee = baseFee,
                 .err = telINSUF_FEE_P});
        }

        // test excessive fee for confidential transactions
        {
            Env env{*this, features};
            Account const alice("alice"), bob("bob"), carol("carol");
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});
            setup(mptAlice, alice, bob, carol);

            auto const baseFee = env.current()->fees().base;
            auto const highFee = baseFee * (kConfidentialFeeMultiplier + 1) * 2;
            auto const bobBefore = env.balance(bob);
            mptAlice.convert(
                {.account = bob,
                 .amt = 1,
                 .holderPubKey = mptAlice.getPubKey(bob),
                 .fee = highFee});
            BEAST_EXPECT(env.balance(bob) == bobBefore - highFee);
        }
    }

    void
    testSendForgedEqualityProof(FeatureBitset features)
    {
        testcase("Send: forged equality proof");

        // Test that modifying a ciphertext after proof generation causes
        // verification to fail. The Fiat-Shamir challenge binds ciphertexts
        // to the proof, so any modification invalidates the proof.

        using namespace test::jtx;
        Env env{*this, features};
        Account const alice("alice"), bob("bob"), carol("carol");
        ConfidentialEnv confEnv{
            env,
            alice,
            {{.account = bob}, {.account = carol, .payAmount = 1000, .convertAmount = 50}}};
        auto& mptAlice = confEnv.mpt;

        ConfidentialSendSetup const setup(mptAlice, bob, carol, alice, 10);

        // Forge destination ciphertext (Enc(20) instead of Enc(10))
        {
            auto const proof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            Buffer const forgedBlindingFactor = generateBlindingFactor();
            auto const forgedDestAmt = mptAlice.encryptAmount(carol, 20, forgedBlindingFactor);

            auto args = setup.sendArgs(
                bob, carol, requireOptionalRef(proof, "Missing proof"), tecBAD_PROOF);
            args.destEncryptedAmt = forgedDestAmt;
            mptAlice.send(args);
        }

        // Forge sender's ciphertext (Enc(5) instead of Enc(10))
        {
            auto const proof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            Buffer const forgedBlindingFactor = generateBlindingFactor();
            auto const forgedSenderAmt = mptAlice.encryptAmount(bob, 5, forgedBlindingFactor);

            auto args = setup.sendArgs(
                bob, carol, requireOptionalRef(proof, "Missing proof"), tecBAD_PROOF);
            args.senderEncryptedAmt = forgedSenderAmt;
            mptAlice.send(args);
        }

        // Forge issuer's ciphertext (Enc(100) instead of Enc(10))
        {
            auto const proof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            Buffer const forgedBlindingFactor = generateBlindingFactor();
            auto const forgedIssuerAmt = mptAlice.encryptAmount(alice, 100, forgedBlindingFactor);

            auto args = setup.sendArgs(
                bob, carol, requireOptionalRef(proof, "Missing proof"), tecBAD_PROOF);
            args.issuerEncryptedAmt = forgedIssuerAmt;
            mptAlice.send(args);
        }
    }

    void
    testSendForgedRangeProof(FeatureBitset features)
    {
        testcase("Send: forged range proof");

        // Attack: send uint64_max tokens using Enc(uint64_max) ciphertexts
        // and a corrupted bulletproof. Verifier rejects due to inner-product
        // mismatch and Fiat-Shamir transcript divergence. Supply invariant
        // is preserved.

        using namespace test::jtx;
        Env env{*this, features};
        Account const alice("alice"), bob("bob"), carol("carol");
        ConfidentialEnv confEnv{
            env,
            alice,
            {{.account = bob}, {.account = carol, .payAmount = 1000, .convertAmount = 50}}};
        auto& mptAlice = confEnv.mpt;

        uint64_t const badAmount = std::numeric_limits<uint64_t>::max();
        Buffer const blindingFactor = generateBlindingFactor();

        // Construct Enc(uint64_max) ciphertexts and commitment.
        auto const senderAmt = mptAlice.encryptAmount(bob, badAmount, blindingFactor);
        auto const destAmt = mptAlice.encryptAmount(carol, badAmount, blindingFactor);
        auto const issuerAmt = mptAlice.encryptAmount(alice, badAmount, blindingFactor);
        auto const amountCommitment = mptAlice.getPedersenCommitment(badAmount, blindingFactor);

        // Balance commitment for Bob's actual balance.
        auto const prevSpending = requireOptional(
            mptAlice.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending),
            "Missing previous spending balance");
        auto const balanceBlindingFactor = generateBlindingFactor();
        auto const balanceCommitment =
            mptAlice.getPedersenCommitment(prevSpending, balanceBlindingFactor);

        // Generate a valid proof for a legitimate amount, then corrupt
        // the bulletproof segment to simulate a forged range proof.
        ConfidentialSendSetup const setup(mptAlice, bob, carol, alice, 10);
        auto const validProof = setup.generateProof(mptAlice, env, bob, carol);
        if (!BEAST_EXPECT(validProof.has_value()))
            return;

        // Corrupt bulletproof bytes.
        Buffer forgedProof = requireOptional(validProof, "Missing valid proof");
        for (size_t i = kBulletproofOffset; i < forgedProof.size(); i += 7)
            forgedProof.data()[i] ^= 0xFF;

        // Submit — rejected due to commitment mismatch.
        mptAlice.send(
            {.account = bob,
             .dest = carol,
             .amt = badAmount,
             .proof = strHex(forgedProof),
             .senderEncryptedAmt = senderAmt,
             .destEncryptedAmt = destAmt,
             .issuerEncryptedAmt = issuerAmt,
             .amountCommitment = amountCommitment,
             .balanceCommitment = balanceCommitment,
             .err = tecBAD_PROOF});

        // Supply invariant: Bob's balance unchanged.
        auto const postSpending = requireOptional(
            mptAlice.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending),
            "Missing post spending balance");
        BEAST_EXPECT(postSpending == prevSpending);
    }

    void
    testSendNegativeValueMalleability(FeatureBitset features)
    {
        testcase("Send: negative value malleability");

        // Attack: forge a bulletproof claiming remaining = (uint64_t)(-10).
        // Bob has 10 tokens, sends 10. Honest remaining is 0, but the
        // forged proof claims 0xFFFFFFFFFFFFFFF6. Rejected because
        // PC(0) != PC(0xFFFFFFFFFFFFFFF6).

        using namespace test::jtx;
        // Bob converts exactly 10 tokens, leaving honest remaining = 0.
        Env env{*this, features};
        Account const alice("alice"), bob("bob"), carol("carol");
        ConfidentialEnv confEnv{
            env,
            alice,
            {{.account = bob, .payAmount = 1000, .convertAmount = 10},
             {.account = carol, .payAmount = 1000, .convertAmount = 50}}};
        auto& mptAlice = confEnv.mpt;

        uint64_t const sendAmount = 10;
        uint64_t const negativeRemaining = static_cast<uint64_t>(-10);  // 0xFFFFFFFFFFFFFFF6

        ConfidentialSendSetup const setup(mptAlice, bob, carol, alice, sendAmount);

        auto const ctxHash = getSendContextHash(
            bob.id(), mptAlice.issuanceID(), env.seq(bob), carol.id(), setup.version);

        auto const validProof = setup.generateProof(mptAlice, env, bob, carol);
        if (!BEAST_EXPECT(validProof.has_value()))
            return;

        // Forge bulletproof for {10, 0xFFFFFFFFFFFFFFF6} and splice it in.
        auto const forgedBulletproof = getForgedBulletproof(
            {sendAmount, negativeRemaining},
            {setup.amountBlindingFactor, setup.balanceBlindingFactor},
            ctxHash);

        Buffer forgedProof(requireOptionalRef(validProof, "Missing valid proof").size());
        std::memcpy(
            forgedProof.data(),
            requireOptionalRef(validProof, "Missing valid proof").data(),
            kBulletproofOffset);
        std::memcpy(
            forgedProof.data() + kBulletproofOffset,
            forgedBulletproof.data(),
            kEcDoubleBulletproofLength);

        mptAlice.send(setup.sendArgs(bob, carol, forgedProof, tecBAD_PROOF));

        // Supply invariant: Bob's balance unchanged.
        auto const postSpending = requireOptional(
            mptAlice.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending),
            "Missing post spending balance");
        BEAST_EXPECT(postSpending == setup.prevSpending);
    }

    void
    testSendInvalidProofContextBinding(FeatureBitset features)
    {
        testcase("Send proof context binding");
        using namespace test::jtx;

        auto runBadProof = [&](auto makeContextHash) {
            Env env{*this, features};
            Account const alice("alice");
            Account const bob("bob");
            Account const carol("carol");
            ConfidentialEnv confEnv{
                env,
                alice,
                {{.account = bob, .payAmount = 100, .convertAmount = 40}, {.account = carol}}};
            auto& mptAlice = confEnv.mpt;

            ConfidentialSendSetup const setup(mptAlice, bob, carol, alice, 10);

            auto const proof = mptAlice.getConfidentialSendProof(
                bob,
                setup.sendAmount,
                setup.recipients,
                setup.blindingFactor,
                makeContextHash(env, mptAlice, alice, bob, carol, setup.version),
                {
                    .pedersenCommitment = setup.amountCommitment,
                    .amt = setup.sendAmount,
                    .encryptedAmt = setup.senderAmt,
                    .blindingFactor = setup.amountBlindingFactor,
                },
                {
                    .pedersenCommitment = setup.balanceCommitment,
                    .amt = setup.prevSpending,
                    .encryptedAmt = setup.prevEncryptedSpending,
                    .blindingFactor = setup.balanceBlindingFactor,
                });
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            mptAlice.send(setup.sendArgs(
                bob, carol, requireOptionalRef(proof, "Missing proof"), tecBAD_PROOF));
        };

        // Wrong sender account in the proof context.
        runBadProof([&](Env& env,
                        MPTTester const& mpt,
                        Account const&,
                        Account const& bob,
                        Account const& carol,
                        std::uint32_t version) {
            return getSendContextHash(
                carol.id(), mpt.issuanceID(), env.seq(bob), carol.id(), version);
        });

        // Wrong issuance ID in the proof context.
        runBadProof([&](Env& env,
                        MPTTester const&,
                        Account const& alice,
                        Account const& bob,
                        Account const& carol,
                        std::uint32_t version) {
            return getSendContextHash(
                bob.id(),
                makeMptID(env.seq(alice) + 100, alice),
                env.seq(bob),
                carol.id(),
                version);
        });

        // Wrong transaction sequence in the proof context.
        runBadProof([&](Env& env,
                        MPTTester const& mpt,
                        Account const&,
                        Account const& bob,
                        Account const& carol,
                        std::uint32_t version) {
            return getSendContextHash(
                bob.id(), mpt.issuanceID(), env.seq(bob) + 1, carol.id(), version);
        });

        // Wrong destination in the proof context.
        runBadProof([&](Env& env,
                        MPTTester const& mpt,
                        Account const&,
                        Account const& bob,
                        Account const&,
                        std::uint32_t version) {
            return getSendContextHash(bob.id(), mpt.issuanceID(), env.seq(bob), bob.id(), version);
        });

        // Wrong balance version in the proof context.
        runBadProof([&](Env& env,
                        MPTTester const& mpt,
                        Account const&,
                        Account const& bob,
                        Account const& carol,
                        std::uint32_t version) {
            return getSendContextHash(
                bob.id(), mpt.issuanceID(), env.seq(bob), carol.id(), version + 1);
        });
    }

    void
    testSendFiatShamirBinding(FeatureBitset features)
    {
        testcase("Send: Fiat-Shamir Binding");

        using namespace test::jtx;
        Env env{*this, features};
        Account const alice("alice"), bob("bob"), carol("carol");
        ConfidentialEnv confEnv{
            env,
            alice,
            {{.account = bob}, {.account = carol, .payAmount = 1000, .convertAmount = 50}}};
        auto& mptAlice = confEnv.mpt;

        ConfidentialSendSetup const setup(mptAlice, bob, carol, alice, 10);

        // Variant A: forged amount commitment.
        {
            auto const proof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            auto const forgedBlindingFactor = generateBlindingFactor();
            auto const forgedCommitment =
                mptAlice.getPedersenCommitment(setup.sendAmount + 5, forgedBlindingFactor);

            auto args = setup.sendArgs(
                bob, carol, requireOptionalRef(proof, "Missing proof"), tecBAD_PROOF);
            args.amountCommitment = forgedCommitment;
            mptAlice.send(args);
        }

        // Variant B: proof replay at a different sequence.
        {
            auto const proof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            mptAlice.pay(bob, carol, 1);
            env.close();

            mptAlice.send(setup.sendArgs(
                bob, carol, requireOptionalRef(proof, "Missing proof"), tecBAD_PROOF));
        }

        // Variant C: tampered response scalars.
        {
            auto const proof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            auto const& proofRef = requireOptionalRef(proof, "Missing proof");
            Buffer tamperedProof(proofRef.size());
            std::memcpy(tamperedProof.data(), proofRef.data(), proofRef.size());
            size_t const tamperOffset = tamperedProof.size() / 2;
            tamperedProof.data()[tamperOffset] ^= 0xFF;

            mptAlice.send(setup.sendArgs(bob, carol, tamperedProof, tecBAD_PROOF));
        }
    }

    void
    testSendProofComponentReuse(FeatureBitset features)
    {
        testcase("Send: Proof Component Reuse");

        using namespace test::jtx;
        Env env{*this, features};
        Account const alice("alice"), bob("bob"), carol("carol"), dan("dan");
        ConfidentialEnv confEnv{
            env,
            alice,
            {{.account = bob},
             {.account = carol, .payAmount = 1000, .convertAmount = 50},
             {.account = dan, .payAmount = 1000, .convertAmount = 50}}};
        auto& mptAlice = confEnv.mpt;

        uint64_t const sendAmount = 10;

        // Variant A: replay proof to same destination after sequence changes.
        {
            ConfidentialSendSetup const setup(mptAlice, bob, carol, alice, sendAmount);

            auto const proof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            mptAlice.send(setup.sendArgs(bob, carol, requireOptionalRef(proof, "Missing proof")));
            mptAlice.mergeInbox({.account = carol});

            mptAlice.send(setup.sendArgs(
                bob, carol, requireOptionalRef(proof, "Missing proof"), tecBAD_PROOF));
        }

        // Variant B: replay proof to a different destination.
        {
            ConfidentialSendSetup const setup(mptAlice, bob, carol, alice, sendAmount);

            auto const proof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            mptAlice.send(setup.sendArgs(bob, carol, requireOptionalRef(proof, "Missing proof")));
            mptAlice.mergeInbox({.account = carol});

            auto const destAmtDan = mptAlice.encryptAmount(dan, sendAmount, setup.blindingFactor);
            auto const issuerAmtDan =
                mptAlice.encryptAmount(alice, sendAmount, setup.blindingFactor);

            auto args =
                setup.sendArgs(bob, dan, requireOptionalRef(proof, "Missing proof"), tecBAD_PROOF);
            args.destEncryptedAmt = destAmtDan;
            args.issuerEncryptedAmt = issuerAmtDan;
            mptAlice.send(args);
        }
    }

    void
    testSendSpecialWitnessValues(FeatureBitset features)
    {
        testcase("Send: special witness values");

        using namespace test::jtx;
        Env env{*this, features};
        Account const alice("alice"), bob("bob"), carol("carol");
        ConfidentialEnv confEnv{
            env,
            alice,
            {{.account = bob}, {.account = carol, .payAmount = 1000, .convertAmount = 50}}};
        auto& mptAlice = confEnv.mpt;

        ConfidentialSendSetup const setup(mptAlice, bob, carol, alice, 10);

        // Variant A: zero-valued response scalars.
        {
            auto const proof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            Buffer forgedProof = requireOptionalRef(proof, "Missing proof");

            static constexpr size_t kSigmaScalarSize = 32;
            static constexpr size_t kChallengeOffset = 0;
            static constexpr size_t kResponseOffset = kChallengeOffset + kSigmaScalarSize;
            static constexpr size_t kResponseSize = 5 * kSigmaScalarSize;  // z_m..z_sk
            std::memset(forgedProof.data() + kResponseOffset, 0, kResponseSize);

            mptAlice.send(setup.sendArgs(bob, carol, forgedProof, tecBAD_PROOF));
        }

        // Variant B: identity element in ciphertext.
        {
            auto const proof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            Buffer invalidCiphertext(kEcGamalEncryptedTotalLength);
            std::memset(invalidCiphertext.data(), 0, kEcGamalEncryptedTotalLength);

            auto args = setup.sendArgs(
                bob, carol, requireOptionalRef(proof, "Missing proof"), temBAD_CIPHERTEXT);
            args.senderEncryptedAmt = invalidCiphertext;
            mptAlice.send(args);
        }

        // Variant B2: identity element in commitment.
        {
            auto const proof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            Buffer invalidCommitment(kEcPedersenCommitmentLength);
            std::memset(invalidCommitment.data(), 0, kEcPedersenCommitmentLength);

            auto args = setup.sendArgs(
                bob, carol, requireOptionalRef(proof, "Missing proof"), temMALFORMED);
            args.amountCommitment = invalidCommitment;
            mptAlice.send(args);
        }

        // Variant C: boundary scalar (curve order).
        {
            auto const proof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            Buffer forgedProof = requireOptionalRef(proof, "Missing proof");

            static constexpr unsigned char kCurveOrder[32] = {
                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  //
                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,  //
                0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48, 0xA0, 0x3B,  //
                0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x41   //
            };

            std::memcpy(forgedProof.data() + 32, kCurveOrder, 32);

            mptAlice.send(setup.sendArgs(bob, carol, forgedProof, tecBAD_PROOF));
        }

        // Variant C2: overflow scalar (curve order + 1).
        {
            auto const proof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(proof.has_value()))
                return;

            Buffer forgedProof = requireOptionalRef(proof, "Missing proof");

            static constexpr unsigned char kOverflowScalar[32] = {
                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  //
                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,  //
                0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48, 0xA0, 0x3B,  //
                0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x42   //
            };

            std::memcpy(forgedProof.data() + 32, kOverflowScalar, 32);

            mptAlice.send(setup.sendArgs(bob, carol, forgedProof, tecBAD_PROOF));
        }
    }

    void
    testSendCrossStatementProofSubstitution(FeatureBitset features)
    {
        testcase("Send: cross-statement proof substitution");

        // This test verifies that proofs generated for one protocol component
        // cannot be used in place of another, and that proofs bound to
        // different public parameters are rejected.

        using namespace test::jtx;
        Env env{*this, features};
        Account const alice("alice"), bob("bob"), carol("carol");
        ConfidentialEnv confEnv{
            env,
            alice,
            {{.account = bob}, {.account = carol, .payAmount = 1000, .convertAmount = 50}},
            tfMPTCanLock | tfMPTCanHoldConfidentialBalance | tfMPTCanTransfer | tfMPTCanClawback};
        auto& mptAlice = confEnv.mpt;

        uint64_t const sendAmount = 10;

        // Variant A: Swap proof type (cross-statement substitution)
        // -----------------------------------------------------------------
        // Attack: Generate a valid convertBack proof (compact sigma +
        // single bulletproof) and attempt to use it as the ZK proof in a
        // ConfidentialMPTSend transaction.
        //
        // Expected: The send proof has a different structure
        // (equality + 2×pedersen + double bulletproof). Even if sized to
        // match, the domain-separated Fiat-Shamir transcript differs,
        // so verification equations fail.
        {
            ConfidentialSendSetup const setup(mptAlice, bob, carol, alice, sendAmount);

            // Generate a valid convertBack proof for bob
            auto const spendingBalance = requireOptional(
                mptAlice.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending),
                "Missing spending balance");
            auto const encryptedSpending = requireOptional(
                mptAlice.getEncryptedBalance(bob, MPTTester::holderEncryptedSpending),
                "Missing encrypted spending balance");

            Buffer const pcBlindingFactor = generateBlindingFactor();
            Buffer const pedersenCommitment =
                mptAlice.getPedersenCommitment(spendingBalance, pcBlindingFactor);

            auto const version = mptAlice.getMPTokenVersion(bob);
            uint256 const convertBackCtxHash =
                getConvertBackContextHash(bob.id(), mptAlice.issuanceID(), env.seq(bob), version);

            Buffer const convertBackProof = mptAlice.getConvertBackProof(
                bob,
                sendAmount,
                convertBackCtxHash,
                {
                    .pedersenCommitment = pedersenCommitment,
                    .amt = spendingBalance,
                    .encryptedAmt = encryptedSpending,
                    .blindingFactor = pcBlindingFactor,
                });

            // Resize the convertBack proof to match the expected send proof
            // size so it passes preflight's size check and reaches the actual
            // ZK verification in doApply.
            auto const expectedSendSize = kEcSendProofLength;
            Buffer resizedProof(expectedSendSize);
            auto const copyLen = std::min(convertBackProof.size(), expectedSendSize);
            std::memcpy(resizedProof.data(), convertBackProof.data(), copyLen);
            // Zero-pad the rest (if convertBack proof is shorter)
            if (copyLen < expectedSendSize)
                std::memset(resizedProof.data() + copyLen, 0, expectedSendSize - copyLen);

            mptAlice.send(setup.sendArgs(bob, carol, resizedProof, tecBAD_PROOF));
        }

        // Variant B: Valid proof bound to wrong public parameters
        // -----------------------------------------------------------------
        // Attack: Generate a valid send proof using a wrong context hash
        // (computed with a different issuanceID). The proof is
        // mathematically valid for the wrong statement, but when the
        // verifier recomputes the Fiat-Shamir challenge using the correct
        // issuanceID, the challenge differs and verification fails.
        {
            ConfidentialSendSetup const setup(mptAlice, bob, carol, alice, sendAmount);

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
                wrongCtxHash,
                {
                    .pedersenCommitment = setup.amountCommitment,
                    .amt = sendAmount,
                    .encryptedAmt = setup.senderAmt,
                    .blindingFactor = setup.amountBlindingFactor,
                },
                {
                    .pedersenCommitment = setup.balanceCommitment,
                    .amt = setup.prevSpending,
                    .encryptedAmt = setup.prevEncryptedSpending,
                    .blindingFactor = setup.balanceBlindingFactor,
                });

            if (!BEAST_EXPECT(wrongProof.has_value()))
                return;

            // Submit with the correct issuanceID — verifier recomputes
            // the challenge using the real issuanceID, which differs from
            // the one baked into the proof.
            mptAlice.send(setup.sendArgs(
                bob, carol, requireOptionalRef(wrongProof, "Missing wrong proof"), tecBAD_PROOF));
        }
    }

    void
    testSendCiphertextMalleability(FeatureBitset features)
    {
        testcase("Send: ciphertext malleability");

        // Attack: replace ElGamal ciphertext Enc(m) with Enc(2m) to inflate
        // the amount credited to the recipient. ElGamal is homomorphic, so
        // scalar multiplication (C1, C2) → (k*C1, k*C2) decrypts to k*m.

        using namespace test::jtx;
        Env env{*this, features};
        Account const alice("alice"), bob("bob"), carol("carol");
        ConfidentialEnv confEnv{
            env,
            alice,
            {{.account = bob}, {.account = carol, .payAmount = 1000, .convertAmount = 50}},
            tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance};
        auto& mptAlice = confEnv.mpt;

        uint64_t const sendAmount = 10;

        // Variant A: Post-signature tampering.
        // Build a valid signed transaction, then replace the destination
        // ciphertext with Enc(2m) in the serialized blob. The original
        // signature no longer covers the modified data.
        {
            auto const seq = env.seq(bob);
            auto jv = mptAlice.sendJV({.account = bob, .dest = carol, .amt = sendAmount}, seq);
            auto jtx = env.jt(jv);
            BEAST_EXPECT(jtx.stx);

            // Serialize signed tx, deserialize into mutable STObject
            Serializer s;
            jtx.stx->add(s);
            SerialIter sit(s.slice());
            STObject obj(sit, sfTransaction);

            // Replace dest ciphertext with Enc(2m) — a valid EC point
            // encrypting an inflated amount under carol's key
            Buffer const bf = generateBlindingFactor();
            auto const inflatedCiphertext = mptAlice.encryptAmount(carol, sendAmount * 2, bf);
            obj.setFieldVL(sfDestinationEncryptedAmount, inflatedCiphertext);

            // Re-serialize with the original (now-stale) signature
            Serializer tampered;
            obj.add(tampered);

            // Signature verification fails — rejected before ZKP check
            auto const jr = env.rpc("submit", strHex(tampered.slice()));
            BEAST_EXPECT(jr[jss::result][jss::error] == "invalidTransaction");
        }

        // Variant B: Re-signed with inflated ciphertext.
        // Generate a valid proof for amount m, then replace the destination
        // ciphertext with Enc(2m) and re-sign. Signature passes, but the
        // compact sigma proof fails: the proof binds Enc(m) to the Pedersen
        // commitment PC(m, r), so substituting Enc(2m) breaks the linkage.
        {
            ConfidentialSendSetup const setup(mptAlice, bob, carol, alice, sendAmount);

            auto const ctxHash = getSendContextHash(
                bob.id(), mptAlice.issuanceID(), env.seq(bob), carol.id(), setup.version);

            auto const validProof = mptAlice.getConfidentialSendProof(
                bob,
                sendAmount,
                setup.recipients,
                setup.blindingFactor,
                ctxHash,
                {
                    .pedersenCommitment = setup.amountCommitment,
                    .amt = sendAmount,
                    .encryptedAmt = setup.senderAmt,
                    .blindingFactor = setup.amountBlindingFactor,
                },
                {
                    .pedersenCommitment = setup.balanceCommitment,
                    .amt = setup.prevSpending,
                    .encryptedAmt = setup.prevEncryptedSpending,
                    .blindingFactor = setup.balanceBlindingFactor,
                });

            if (!BEAST_EXPECT(validProof.has_value()))
                return;

            // Replace dest ciphertext with Enc(2m) using the same blinding
            // factor — even with matching randomness the proof rejects
            // because the committed plaintext differs
            auto const inflatedDestAmt =
                mptAlice.encryptAmount(carol, sendAmount * 2, setup.blindingFactor);

            auto args = setup.sendArgs(
                bob, carol, requireOptionalRef(validProof, "Missing valid proof"), tecBAD_PROOF);
            args.destEncryptedAmt = inflatedDestAmt;
            mptAlice.send(args);
        }
    }

    void
    testSendCiphertextNegation(FeatureBitset features)
    {
        testcase("Send: ciphertext negation");

        // Attack: negate ciphertext -Enc(m) = (-C1, -C2) to reverse the
        // transaction direction. Negation decrypts to the group-level
        // additive inverse of m*G, effectively turning a credit into a debit.

        using namespace test::jtx;
        Env env{*this, features};
        Account const alice("alice"), bob("bob"), carol("carol");
        ConfidentialEnv confEnv{
            env,
            alice,
            {{.account = bob}, {.account = carol, .payAmount = 1000, .convertAmount = 50}},
            tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance};
        auto& mptAlice = confEnv.mpt;

        uint64_t const sendAmount = 10;

        // Negate an ElGamal ciphertext by flipping the y-coordinate parity
        // of both compressed EC points. For secp256k1 compressed form,
        // prefix 0x02 means even-y and 0x03 means odd-y; negation
        // swaps them: -P has the same x but opposite y.
        auto negateCiphertext = [](Buffer const& ct) -> Buffer {
            Buffer neg = ct;
            neg.data()[0] ^= 0x01;                             // negate C1
            neg.data()[kEcCiphertextComponentLength] ^= 0x01;  // negate C2
            return neg;
        };

        // Variant A: Post-signature negation.
        // Negate the destination ciphertext in the signed blob.
        // Signature no longer covers the modified field.
        {
            auto const seq = env.seq(bob);
            auto jv = mptAlice.sendJV({.account = bob, .dest = carol, .amt = sendAmount}, seq);
            auto jtx = env.jt(jv);
            BEAST_EXPECT(jtx.stx);

            Serializer s;
            jtx.stx->add(s);

            SerialIter sit(s.slice());
            STObject obj(sit, sfTransaction);

            auto const origDestAmt = obj.getFieldVL(sfDestinationEncryptedAmount);
            Buffer const origBuf(origDestAmt.data(), origDestAmt.size());
            auto const negDestAmt = negateCiphertext(origBuf);
            obj.setFieldVL(
                sfDestinationEncryptedAmount, Slice(negDestAmt.data(), negDestAmt.size()));

            Serializer tampered;
            obj.add(tampered);

            auto const jr = env.rpc("submit", strHex(tampered.slice()));
            BEAST_EXPECT(jr[jss::result][jss::error] == "invalidTransaction");
        }

        // Variant B: Re-signed with all negated ciphertexts.
        // Signature passes, but the compact sigma proof fails — the proof
        // was generated for Enc(m), not Enc(-m).
        {
            ConfidentialSendSetup const setup(mptAlice, bob, carol, alice, sendAmount);

            auto const validProof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(validProof.has_value()))
                return;

            // Negate all three ciphertexts: Enc(m) -> Enc(-m)
            auto const negSenderAmt = negateCiphertext(setup.senderAmt);
            auto const negDestAmt = negateCiphertext(setup.destAmt);
            auto const negIssuerAmt = negateCiphertext(setup.issuerAmt);

            auto args = setup.sendArgs(
                bob, carol, requireOptionalRef(validProof, "Missing valid proof"), tecBAD_PROOF);
            args.senderEncryptedAmt = negSenderAmt;
            args.destEncryptedAmt = negDestAmt;
            args.issuerEncryptedAmt = negIssuerAmt;
            mptAlice.send(args);
        }

        // Variant C: Negate only the sender ciphertext.
        // The verifier uses the sender ciphertext to derive the remainder
        // commitment: Enc(b) - Enc(m) becomes Enc(b) - (-Enc(m)) = Enc(b+m).
        // The bulletproof was generated for (b - m), not (b + m), so the
        // aggregated range proof fails.
        {
            ConfidentialSendSetup const setup(mptAlice, bob, carol, alice, sendAmount);

            auto const validProof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(validProof.has_value()))
                return;

            auto const negSenderAmt = negateCiphertext(setup.senderAmt);

            auto args = setup.sendArgs(
                bob, carol, requireOptionalRef(validProof, "Missing valid proof"), tecBAD_PROOF);
            args.senderEncryptedAmt = negSenderAmt;
            mptAlice.send(args);
        }
    }

    void
    testSendCiphertextCombination(FeatureBitset features)
    {
        testcase("Send: ciphertext combination");

        // Attack: exploit ElGamal homomorphism to combine ciphertexts
        // Enc(m1) + Enc(m2) = Enc(m1+m2), inflating the credited amount
        // without knowing the private keys.

        using namespace test::jtx;
        Env env{*this, features};
        Account const alice("alice"), bob("bob"), carol("carol");
        ConfidentialEnv confEnv{
            env,
            alice,
            {{.account = bob, .payAmount = 1000, .convertAmount = 200},
             {.account = carol, .payAmount = 1000, .convertAmount = 100}},
            tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance};
        auto& mptAlice = confEnv.mpt;

        uint64_t const m1 = 10;
        uint64_t const m2 = 5;

        // Variant A: Post-signature combination.
        // Add Enc(m2) to the signed destination ciphertext Enc(m1).
        // The original signature doesn't cover the combined ciphertext.
        {
            auto const seq = env.seq(bob);
            auto jv = mptAlice.sendJV({.account = bob, .dest = carol, .amt = m1}, seq);
            auto jtx = env.jt(jv);
            BEAST_EXPECT(jtx.stx);

            Serializer s;
            jtx.stx->add(s);

            SerialIter sit(s.slice());
            STObject obj(sit, sfTransaction);

            auto const origDestCt = obj.getFieldVL(sfDestinationEncryptedAmount);

            // Homomorphically add Enc(m2) to the original Enc(m1)
            Buffer const bf2 = generateBlindingFactor();
            auto const encM2 = mptAlice.encryptAmount(carol, m2, bf2);
            auto const combined = requireOptional(
                homomorphicAdd(
                    Slice(origDestCt.data(), origDestCt.size()), Slice(encM2.data(), encM2.size())),
                "Missing combined ciphertext");

            obj.setFieldVL(sfDestinationEncryptedAmount, combined);

            Serializer tampered;
            obj.add(tampered);

            auto const jr = env.rpc("submit", strHex(tampered.slice()));
            BEAST_EXPECT(jr[jss::result][jss::error] == "invalidTransaction");
        }

        // Variant B: Re-signed with combined ciphertext.
        // Generate a valid proof for m1, then replace dest ciphertext with
        // Enc(m1) + Enc(m2). Sigma proof fails because the proof was
        // generated for Enc(m1) only — the combined ciphertext has
        // different randomness.
        {
            ConfidentialSendSetup const setup(mptAlice, bob, carol, alice, m1);

            auto const validProof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(validProof.has_value()))
                return;

            // Homomorphically add Enc(m2) to the valid dest ciphertext
            Buffer const bf2 = generateBlindingFactor();
            auto const encM2 = mptAlice.encryptAmount(carol, m2, bf2);
            auto const combinedDest = homomorphicAdd(setup.destAmt, encM2);
            BEAST_EXPECT(combinedDest.has_value());

            auto args = setup.sendArgs(
                bob, carol, requireOptionalRef(validProof, "Missing valid proof"), tecBAD_PROOF);
            args.destEncryptedAmt = combinedDest;
            mptAlice.send(args);
        }

        // Variant C: Cross-transaction ciphertext reuse.
        // Execute a valid send of m1, then build a new send for m2 using
        // a combined ciphertext oldEnc(m1) + newEnc(m2) = Enc(m1+m2),
        // where oldEnc(m1) is the actual ciphertext from the previous tx.
        // The proof was generated for the new transaction's context, but
        // the ciphertext includes stale randomness from the old Enc(m1).
        {
            // Execute a valid send of m1, capturing the actual ciphertext used
            ConfidentialSendSetup const setup1(mptAlice, bob, carol, alice, m1);
            auto const proof1 = setup1.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(proof1.has_value()))
                return;
            mptAlice.send(setup1.sendArgs(bob, carol, requireOptionalRef(proof1, "Missing proof")));

            ConfidentialSendSetup const setup2(mptAlice, bob, carol, alice, m2);

            auto const proof2 = setup2.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(proof2.has_value()))
                return;

            // Combine the actual prior-tx Enc(m1) with the new Enc(m2)
            auto const crossCombined = homomorphicAdd(setup1.destAmt, setup2.destAmt);
            BEAST_EXPECT(crossCombined.has_value());

            auto args = setup2.sendArgs(
                bob, carol, requireOptionalRef(proof2, "Missing proof"), tecBAD_PROOF);
            args.destEncryptedAmt = crossCombined;
            mptAlice.send(args);
        }
    }

    void
    testSendCiphertextRerandomization(FeatureBitset features)
    {
        testcase("Send: ciphertext rerandomization");

        // Attack: substitute the randomness component C1 of an ElGamal
        // ciphertext (C1, C2) while keeping the message component C2
        // unchanged. This "rerandomizes" the ciphertext to break
        // linkability or forge fresh-looking ciphertexts.
        //
        // The compact sigma proof binds C1 to the shared randomness used
        // across all recipients, so any C1 substitution breaks the proof.

        using namespace test::jtx;
        Env env{*this, features};
        Account const alice("alice"), bob("bob"), carol("carol");
        ConfidentialEnv confEnv{
            env,
            alice,
            {{.account = bob}, {.account = carol, .payAmount = 1000, .convertAmount = 50}},
            tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance};
        auto& mptAlice = confEnv.mpt;

        uint64_t const sendAmount = 10;

        // Helper: replace C1 in a ciphertext with C1 from another
        // ciphertext, keeping C2 unchanged. Returns a rerandomized
        // ciphertext (C1', C2).
        auto substituteC1 = [](Buffer const& target, Buffer const& source) -> Buffer {
            Buffer result = target;
            // Copy C1 (the first ciphertext component) from source.
            std::memcpy(result.data(), source.data(), kEcCiphertextComponentLength);
            return result;
        };

        // Variant A: Post-signature C1 substitution.
        // Replace C1 in the dest ciphertext after signing.
        // Signature no longer covers the modified ciphertext.
        {
            auto const seq = env.seq(bob);
            auto jv = mptAlice.sendJV({.account = bob, .dest = carol, .amt = sendAmount}, seq);
            auto jtx = env.jt(jv);
            BEAST_EXPECT(jtx.stx);

            Serializer s;
            jtx.stx->add(s);
            SerialIter sit(s.slice());
            STObject obj(sit, sfTransaction);

            // Generate a random C1' by encrypting a different amount
            Buffer const bf2 = generateBlindingFactor();
            auto const otherCt = mptAlice.encryptAmount(carol, 99, bf2);

            // Replace C1 in the dest ciphertext
            auto const origDestAmt = obj.getFieldVL(sfDestinationEncryptedAmount);
            Buffer const origBuf(origDestAmt.data(), origDestAmt.size());
            auto const rerandomized = substituteC1(origBuf, otherCt);
            obj.setFieldVL(
                sfDestinationEncryptedAmount, Slice(rerandomized.data(), rerandomized.size()));

            Serializer tampered;
            obj.add(tampered);

            // Signature verification fails
            auto const jr = env.rpc("submit", strHex(tampered.slice()));
            BEAST_EXPECT(jr[jss::result][jss::error] == "invalidTransaction");
        }

        // Variant B: Re-signed C1 substitution.
        // Replace C1 in the dest ciphertext with a fresh random point
        // and re-sign. Sigma proof fails because the shared-randomness
        // binding no longer holds — C1' wasn't generated with the same r
        // used in the proof.
        {
            ConfidentialSendSetup const setup(mptAlice, bob, carol, alice, sendAmount);

            auto const validProof = setup.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(validProof.has_value()))
                return;

            // Create a ciphertext with different randomness to get C1'
            Buffer const bf2 = generateBlindingFactor();
            auto const otherCt = mptAlice.encryptAmount(carol, sendAmount, bf2);

            // Replace C1 in dest ciphertext, keep C2
            auto const rerandomizedDest = substituteC1(setup.destAmt, otherCt);

            auto args = setup.sendArgs(
                bob, carol, requireOptionalRef(validProof, "Missing valid proof"), tecBAD_PROOF);
            args.destEncryptedAmt = rerandomizedDest;
            mptAlice.send(args);
        }
    }

    void
    testSendZeroRandomnessCiphertext(FeatureBitset features)
    {
        testcase("Send: zero randomness ciphertext");

        // Setting r = 0 in ElGamal yields C1 = O (identity), C2 = mG —
        // a deterministic ciphertext that reveals the plaintext.

        using namespace test::jtx;
        Env env{*this, features};
        Account const alice("alice"), bob("bob"), carol("carol");
        ConfidentialEnv confEnv{
            env,
            alice,
            {{.account = bob}, {.account = carol, .payAmount = 1000, .convertAmount = 50}},
            tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance};
        auto& mptAlice = confEnv.mpt;

        uint64_t const sendAmount = 10;

        // -----------------------------------------------------------------
        // Variant A: Post-signature zero-randomness substitution
        // -----------------------------------------------------------------
        // Construct a valid ConfidentialMPTSend transaction with proper
        // ciphertexts and ZKPs, sign it, then replace the sender ciphertext
        // with a deterministic form (C1 = 0x00...00, C2 = arbitrary).
        // Since the identity element has no valid compressed encoding,
        // the modified blob fails deserialization / signature check.
        {
            auto const seq = env.seq(bob);
            auto jv = mptAlice.sendJV({.account = bob, .dest = carol, .amt = sendAmount}, seq);
            auto jtx = env.jt(jv);
            BEAST_EXPECT(jtx.stx);

            // Serialize the signed transaction
            Serializer s;
            jtx.stx->add(s);
            SerialIter sit(s.slice());
            STObject obj(sit, sfTransaction);

            // Replace sender ciphertext with zero-randomness form:
            // C1 = all zeros (identity element — invalid encoding)
            // C2 = valid trivial point (simulating mG)
            Buffer zeroCiphertext(kEcGamalEncryptedTotalLength);
            std::memset(zeroCiphertext.data(), 0, kEcGamalEncryptedTotalLength);
            // C2 half: use a valid point so only C1 is the problem
            auto const& tc = getTrivialCiphertext();
            std::memcpy(
                zeroCiphertext.data() + kEcCiphertextComponentLength,
                tc.data() + kEcCiphertextComponentLength,
                kEcCiphertextComponentLength);
            obj.setFieldVL(sfSenderEncryptedAmount, zeroCiphertext);

            // Re-serialize with the original (now-stale) signature
            Serializer tampered;
            obj.add(tampered);

            // Signature verification fails because ciphertext fields are
            // signed — transaction rejected before ZKP verification.
            auto const jr = env.rpc("submit", strHex(tampered.slice()));
            BEAST_EXPECT(jr[jss::result][jss::error] == "invalidTransaction");
        }

        // -----------------------------------------------------------------
        // Variant B: Re-signed zero-randomness ciphertext
        // -----------------------------------------------------------------
        // Same zero-randomness ciphertext as Variant A (C1 = 0, C2 = mG),
        // but submitted normally via send() which re-signs the transaction.
        // Signature verification passes, but preflight's isValidCiphertext
        // rejects it: the identity element has no valid compressed encoding
        // on secp256k1, so secp256k1_ec_pubkey_parse fails on C1 = 0.
        {
            // Build zero-randomness ciphertext: C1 = all zeros (identity),
            // C2 = valid trivial point (simulating mG)
            Buffer zeroCiphertext(kEcGamalEncryptedTotalLength);
            std::memset(zeroCiphertext.data(), 0, kEcGamalEncryptedTotalLength);
            auto const& tc = getTrivialCiphertext();
            std::memcpy(
                zeroCiphertext.data() + kEcCiphertextComponentLength,
                tc.data() + kEcCiphertextComponentLength,
                kEcCiphertextComponentLength);

            mptAlice.send(
                {.account = bob,
                 .dest = carol,
                 .amt = sendAmount,
                 .senderEncryptedAmt = zeroCiphertext,
                 .err = temBAD_CIPHERTEXT});
        }

        // -----------------------------------------------------------------
        // Variant C: Deterministic ciphertext reuse across transactions
        // -----------------------------------------------------------------
        // Construct two transactions using identical deterministic
        // ciphertexts (same fixed blinding factor).  Even if a valid
        // proof could be generated for one, it cannot be reused because
        // the TransactionContextID (which includes account sequence)
        // differs between transactions.
        {
            // First transaction: generate valid proof for sendAmount
            ConfidentialSendSetup const setup1(mptAlice, bob, carol, alice, sendAmount);

            auto const proof1 = setup1.generateProof(mptAlice, env, bob, carol);
            if (!BEAST_EXPECT(proof1.has_value()))
                return;

            // Submit first transaction successfully
            mptAlice.send(setup1.sendArgs(bob, carol, requireOptionalRef(proof1, "Missing proof")));

            mptAlice.mergeInbox({.account = carol});

            // Second transaction: reuse the same proof from tx1.
            // The context hash includes the new account sequence, so the
            // proof generated for the old sequence is invalid.
            ConfidentialSendSetup const setup2(mptAlice, bob, carol, alice, sendAmount);

            mptAlice.send(setup2.sendArgs(
                bob, carol, requireOptionalRef(proof1, "Missing proof"), tecBAD_PROOF));
        }
    }

    void
    testSendRerandomizesRecipientInboxAgainstMergeCancellation(FeatureBitset features)
    {
        testcase("Send: recipient inbox rerandomization prevents merge cancellation");

        using namespace test::jtx;

        // Derive the deterministic canonical-zero randomness r0 used for
        // Bob's first spending balance.
        auto getCanonicalZeroBlindingFactor = [](AccountID const& account, MPTID const& mptID) {
            Buffer scalar(kEcBlindingFactorLength);
            std::array<unsigned char, 51> hashInput{};
            std::memcpy(hashInput.data(), "EncZero", 7);
            std::memcpy(hashInput.data() + 7, account.data(), account.size());
            std::memcpy(hashInput.data() + 27, mptID.data(), mptID.size());

            for (;;)
            {
                unsigned int mdLen = kEcBlindingFactorLength;
                if (EVP_Digest(
                        hashInput.data(),
                        hashInput.size(),
                        scalar.data(),
                        &mdLen,
                        EVP_sha256(),
                        nullptr) != 1)
                {
                    Throw<std::runtime_error>("Failed to derive canonical zero blinding factor");
                }

                if (secp256k1_ec_seckey_verify(mpt_secp256k1_context(), scalar.data()))
                    return scalar;

                std::memcpy(hashInput.data(), scalar.data(), scalar.size());
            }
        };

        // Pick randomness that would cancel Bob's MergeInbox C1 to infinity
        // without receiver-side re-randomization.
        auto negateScalarSum = [](Buffer const& lhs, Buffer const& rhs) {
            Buffer sum(kEcBlindingFactorLength);
            Buffer negated(kEcBlindingFactorLength);
            secp256k1_mpt_scalar_add(sum.data(), lhs.data(), rhs.data());
            secp256k1_mpt_scalar_negate(negated.data(), sum.data());
            return negated;
        };

        // Without an auditor, target Bob's holder inbox. The crafted send
        // randomness would make MergeInbox hit the point at infinity unless
        // ConfidentialMPTSend re-randomizes the recipient ciphertext.
        {
            Env env{*this, features};
            Account const alice("alice"), bob("bob"), carol("carol");
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
            });

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});
            mptAlice.pay(alice, bob, 100);
            mptAlice.pay(alice, carol, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(carol);
            mptAlice.set({.account = alice, .issuerPubKey = mptAlice.getPubKey(alice)});

            mptAlice.convert({
                .account = carol,
                .amt = 50,
                .holderPubKey = mptAlice.getPubKey(carol),
            });
            mptAlice.mergeInbox({.account = carol});

            Buffer const convertBlindingFactor = generateBlindingFactor();
            mptAlice.convert({
                .account = bob,
                .amt = 20,
                .holderPubKey = mptAlice.getPubKey(bob),
                .blindingFactor = convertBlindingFactor,
            });

            Buffer const canonicalZeroBlindingFactor =
                getCanonicalZeroBlindingFactor(bob.id(), mptAlice.issuanceID());

            // Holder inbox cancellation happens later in MergeInbox, when
            // Bob's spending Enc(0; r0) is added to inbox Enc(25; -r0).
            Buffer const maliciousSendBlindingFactor =
                negateScalarSum(canonicalZeroBlindingFactor, convertBlindingFactor);

            mptAlice.send({
                .account = carol,
                .dest = bob,
                .amt = 5,
                .blindingFactor = maliciousSendBlindingFactor,
            });

            mptAlice.mergeInbox({.account = bob});

            auto const bobSpending =
                mptAlice.getDecryptedBalance(bob, MPTTester::holderEncryptedSpending);
            BEAST_EXPECT(bobSpending && *bobSpending == 25);
        }

        // With an auditor, verify the destination auditor balance is also
        // re-randomized. Auditor balance is updated during send, and this crafted
        // randomness would otherwise make that homomorphic sum hit infinity without
        // re-randomization.
        {
            Env env{*this, features};
            Account const alice("alice"), bob("bob"), carol("carol"), auditor("auditor");
            MPTTester mptAlice(env, alice, {.holders = {bob, carol}, .auditor = auditor});

            mptAlice.create({
                .ownerCount = 1,
                .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
            });

            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = carol});
            mptAlice.pay(alice, bob, 100);
            mptAlice.pay(alice, carol, 100);

            mptAlice.generateKeyPair(alice);
            mptAlice.generateKeyPair(bob);
            mptAlice.generateKeyPair(carol);
            mptAlice.generateKeyPair(auditor);
            mptAlice.set({
                .account = alice,
                .issuerPubKey = mptAlice.getPubKey(alice),
                .auditorPubKey = mptAlice.getPubKey(auditor),
            });

            mptAlice.convert({
                .account = carol,
                .amt = 50,
                .holderPubKey = mptAlice.getPubKey(carol),
            });
            mptAlice.mergeInbox({.account = carol});

            Buffer const convertBlindingFactor = generateBlindingFactor();
            mptAlice.convert({
                .account = bob,
                .amt = 20,
                .holderPubKey = mptAlice.getPubKey(bob),
                .blindingFactor = convertBlindingFactor,
            });

            // This would make the homomorphic sum hit infinity.
            Buffer const maliciousSendBlindingFactor =
                negateScalarSum(gMakeZeroBuffer(kEcBlindingFactorLength), convertBlindingFactor);

            mptAlice.send({
                .account = carol,
                .dest = bob,
                .amt = 5,
                .blindingFactor = maliciousSendBlindingFactor,
            });

            auto const bobAuditor =
                mptAlice.getDecryptedBalance(bob, MPTTester::auditorEncryptedBalance);
            BEAST_EXPECT(bobAuditor && *bobAuditor == 25);
        }
    }

    void
    testWithFeats(FeatureBitset features)
    {
        // ConfidentialMPTConvert
        testConvert(features);
        testConvertPreflight(features);
        testConvertInvalidProofContextBinding(features);
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

        testSendZeroAmount(features);
        testSendWithAuditor(features);

        // ConfidentialMPTClawback
        testClawback(features);
        testClawbackPreflight(features);
        testClawbackPreclaim(features);
        testClawbackProof(features);
        testClawbackWithAuditor(features);
        testClawbackInvalidProofContextBinding(features);

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
        testConvertBackHomomorphicCiphertextModification(features);
        testConvertBackHomomorphicUnderflow(features);

        // Invalid curve points
        testSendInvalidCurvePoints(features);
        testSendWrongGroupPointInjection(features);
        testConvertIdentityElementRejection(features);
        testSendWrongIssuerPublicKey(features);

        // public and private txns
        testPublicTransfersAfterClearingConfidentialFlag(features);

        // Replay tests
        testMutatePrivacy(features);
        testConvertBackInvalidProofContextBinding(features);
        testConvertBackProofCiphertextBinding(features);
        testConvertBackProofVersionMismatch(features);

        // Crafted-proof Tests
        testSendSharedRandomnessViolation(features);

        // Transaction Fee Tests
        testConfidentialMPTBaseFee(features);

        // TransferFee (transfer rate) Tests
        testTransferFee(features);

        // Zero knowledge proof tests
        testSendInvalidProofContextBinding(features);
        testSendForgedEqualityProof(features);
        testSendForgedRangeProof(features);
        testSendNegativeValueMalleability(features);
        testSendFiatShamirBinding(features);
        testSendProofComponentReuse(features);
        testSendSpecialWitnessValues(features);
        testSendCrossStatementProofSubstitution(features);

        // Ciphertext malleability tests
        testSendCiphertextMalleability(features);
        testSendCiphertextNegation(features);
        testSendCiphertextCombination(features);
        testSendCiphertextRerandomization(features);
        testSendZeroRandomnessCiphertext(features);
        testSendRerandomizesRecipientInboxAgainstMergeCancellation(features);
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

BEAST_DEFINE_TESTSUITE(ConfidentialTransfer, app, xrpl);

}  // namespace xrpl
