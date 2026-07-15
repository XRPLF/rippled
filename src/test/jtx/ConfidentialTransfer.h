#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/mpt.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <utility/mpt_utility.h>

#include <secp256k1.h>
#include <secp256k1_mpt.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace xrpl {

class ConfidentialTransferTestBase : public beast::unit_test::Suite
{
protected:
    template <class T>
    static T
    requireOptional(std::optional<T> value, char const* message)
    {
        if (!value)
            Throw<std::runtime_error>(message);
        return std::move(*value);
    }

    template <class T>
    static T const&
    requireOptionalRef(std::optional<T> const& value, char const* message)
    {
        if (!value)
            Throw<std::runtime_error>(message);
        return *value;
    }

    // Offset where the bulletproof begins in a send proof blob.
    // Proof layout: [compact_sigma | bulletproof]
    static constexpr size_t kBulletproofOffset = kEcSendProofLength - kEcDoubleBulletproofLength;

    // Generate a forged aggregated bulletproof (double bulletproof) for
    // the given values and blinding factors. Used to test that splicing
    // a bulletproof claiming a different remaining balance is rejected.
    // secp256k1 convention: returns 1 on success, 0 on failure.
    static Buffer
    getForgedBulletproof(
        std::array<uint64_t, 2> const& values,
        std::array<Buffer, 2> const& blindingFactors,
        uint256 const& contextHash)
    {
        auto* const ctx = mpt_secp256k1_context();

        secp256k1_pubkey h;
        secp256k1_mpt_get_h_generator(ctx, &h);

        Buffer proof(kEcDoubleBulletproofLength);
        size_t proofLen = kEcDoubleBulletproofLength;

        unsigned char blindings[64];
        std::memcpy(blindings, blindingFactors[0].data(), 32);
        std::memcpy(blindings + 32, blindingFactors[1].data(), 32);

        if (secp256k1_bulletproof_prove_agg(
                ctx,
                proof.data(),
                &proofLen,
                values.data(),
                blindings,
                2,
                &h,
                contextHash.data()) == 0)
            Throw<std::runtime_error>("Failed to generate forged bulletproof");

        return proof;
    }

    // Get a bad ciphertext with valid structure but cryptographic invalid for
    // testing purposes. For preflight test purposes.
    static Buffer const&
    getBadCiphertext()
    {
        static Buffer const kBadCiphertext = []() {
            Buffer buf(kEcGamalEncryptedTotalLength);
            std::memset(buf.data(), 0xFF, kEcGamalEncryptedTotalLength);

            buf.data()[0] = kEcCompressedPrefixEvenY;
            buf.data()[kEcCiphertextComponentLength] = kEcCompressedPrefixEvenY;
            return buf;
        }();

        return kBadCiphertext;
    }

    // Get a trivial buffer that is structurally and mathematically valid, but
    // contains invalid data that does not match the ledger state. For preclaim
    // test purposes.
    static Buffer const&
    getTrivialCiphertext()
    {
        static Buffer const kTrivialCiphertext = []() {
            Buffer buf(kEcGamalEncryptedTotalLength);
            std::memset(buf.data(), 0, kEcGamalEncryptedTotalLength);

            buf.data()[0] = kEcCompressedPrefixEvenY;
            buf.data()[kEcCiphertextComponentLength] = kEcCompressedPrefixEvenY;

            buf.data()[kEcCiphertextComponentLength - 1] = 0x01;
            buf.data()[kEcGamalEncryptedTotalLength - 1] = 0x01;

            return buf;
        }();

        return kTrivialCiphertext;
    }

    // Returns a valid compressed EC point (33 bytes) that can pass preflight
    // validation but contains invalid data for preclaim test purposes.
    static Buffer const&
    getTrivialCommitment()
    {
        static Buffer const kTrivialCommitment = []() {
            Buffer buf(kEcPedersenCommitmentLength);
            std::memset(buf.data(), 0, kEcPedersenCommitmentLength);

            buf.data()[0] = kEcCompressedPrefixEvenY;
            // Set last byte to make it a valid x-coordinate on the curve
            buf.data()[kEcPedersenCommitmentLength - 1] = 0x01;

            return buf;
        }();

        return kTrivialCommitment;
    }

    static std::string
    getTrivialSendProofHex()
    {
        Buffer buf(kEcSendProofLength);
        std::memset(buf.data(), 0, kEcSendProofLength);

        for (std::size_t i = 0; i < kEcSendProofLength; i += kEcCiphertextComponentLength)
        {
            buf.data()[i] = kEcCompressedPrefixEvenY;
            if (i + kEcCiphertextComponentLength - 1 < kEcSendProofLength)
                buf.data()[i + kEcCiphertextComponentLength - 1] = 0x01;
        }

        return strHex(buf);
    }

    // Helper struct to encapsulate common setup for integration tests.
    struct ConfidentialSendSetup
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
        ConfidentialSendSetup(
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
            , amountBlindingFactor(blindingFactor)
            , balanceBlindingFactor(generateBlindingFactor())
            , senderAmt(mpt.encryptAmount(sender, amount, blindingFactor))
            , destAmt(mpt.encryptAmount(dest, amount, blindingFactor))
            , issuerAmt(mpt.encryptAmount(issuer, amount, blindingFactor))
            , auditorAmt(
                  auditor ? std::optional<Buffer>(
                                mpt.encryptAmount(auditor->get(), amount, blindingFactor))
                          : std::nullopt)
            , amountCommitment(mpt.getPedersenCommitment(amount, amountBlindingFactor))
            , senderPubKey(requireOptional(mpt.getPubKey(sender), "Missing sender public key"))
            , destPubKey(requireOptional(mpt.getPubKey(dest), "Missing destination public key"))
            , issuerPubKey(requireOptional(mpt.getPubKey(issuer), "Missing issuer public key"))
            , auditorPubKey(auditor ? mpt.getPubKey(auditor->get()) : std::nullopt)
            , prevSpending(requireOptional(
                  mpt.getDecryptedBalance(sender, test::jtx::MPTTester::holderEncryptedSpending),
                  "Missing sender spending balance"))
            , prevEncryptedSpending(requireOptional(
                  mpt.getEncryptedBalance(sender, test::jtx::MPTTester::holderEncryptedSpending),
                  "Missing sender encrypted spending balance"))
            , balanceCommitment(mpt.getPedersenCommitment(prevSpending, balanceBlindingFactor))
        {
            recipients.push_back({
                .publicKey = Slice(senderPubKey),
                .encryptedAmount = senderAmt,
            });
            recipients.push_back({
                .publicKey = Slice(destPubKey),
                .encryptedAmount = destAmt,
            });
            recipients.push_back({
                .publicKey = Slice(issuerPubKey),
                .encryptedAmount = issuerAmt,
            });
            if (auditor)
            {
                recipients.push_back({
                    .publicKey =
                        Slice(requireOptionalRef(auditorPubKey, "Missing auditor public key")),
                    .encryptedAmount =
                        requireOptionalRef(auditorAmt, "Missing auditor encrypted amount"),
                });
            }
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
                ctxHash,
                {
                    .pedersenCommitment = amountCommitment,
                    .amt = sendAmount,
                    .encryptedAmt = senderAmt,
                    .blindingFactor = amountBlindingFactor,
                },
                {
                    .pedersenCommitment = balanceCommitment,
                    .amt = prevSpending,
                    .encryptedAmt = prevEncryptedSpending,
                    .blindingFactor = balanceBlindingFactor,
                });
        }

        [[nodiscard]] test::jtx::MPTConfidentialSend
        sendArgs(
            test::jtx::Account const& sender,
            test::jtx::Account const& dest,
            Buffer const& proof,
            std::optional<TER> err = std::nullopt) const
        {
            return {
                .account = sender,
                .dest = dest,
                .amt = sendAmount,
                .proof = strHex(proof),
                .senderEncryptedAmt = senderAmt,
                .destEncryptedAmt = destAmt,
                .issuerEncryptedAmt = issuerAmt,
                .auditorEncryptedAmt = auditorAmt,
                .amountCommitment = amountCommitment,
                .balanceCommitment = balanceCommitment,
                .err = err,
            };
        }
    };

    // Helper that wraps the boilerplate setup: Env + MPT creation, funding, key
    // generation, and seeding each holder with a confidential balance.
    // The caller supplies the issuer and any number of holders.
    struct ConfidentialEnv
    {
        // Per-holder configuration: the account, how much MPT to fund it
        // with, and how much of that to convert to a confidential balance.
        struct HolderInit
        {
            test::jtx::Account account;
            std::uint64_t payAmount = 1000;
            std::uint64_t convertAmount = 100;
        };

        test::jtx::MPTTester mpt;

        ConfidentialEnv(
            test::jtx::Env& env,
            test::jtx::Account const& issuer,
            std::vector<HolderInit> const& holders,
            std::uint32_t flags = tfMPTCanLock | tfMPTCanHoldConfidentialBalance | tfMPTCanTransfer,
            std::optional<test::jtx::Account> auditor = std::nullopt)
            : mpt{env, issuer, {.holders = extractAccounts(holders), .auditor = auditor}}
        {
            mpt.create({.ownerCount = 1, .flags = flags});

            for (auto const& h : holders)
            {
                mpt.authorize({.account = h.account});
                if ((flags & tfMPTRequireAuth) != 0)
                    mpt.authorize({.account = issuer, .holder = h.account});
                mpt.pay(issuer, h.account, h.payAmount);
            }

            mpt.generateKeyPair(issuer);
            for (auto const& h : holders)
                mpt.generateKeyPair(h.account);
            if (auditor)
                mpt.generateKeyPair(requireOptionalRef(auditor, "Missing auditor"));

            mpt.set({
                .account = issuer,
                .issuerPubKey = mpt.getPubKey(issuer),
                .auditorPubKey = auditor
                    ? mpt.getPubKey(requireOptionalRef(auditor, "Missing auditor"))
                    : std::optional<Buffer>{},
            });

            for (auto const& h : holders)
            {
                mpt.convert({
                    .account = h.account,
                    .amt = h.convertAmount,
                    .holderPubKey = mpt.getPubKey(h.account),
                });
                mpt.mergeInbox({.account = h.account});
            }
        }

    private:
        static std::vector<test::jtx::Account>
        extractAccounts(std::vector<HolderInit> const& holders)
        {
            std::vector<test::jtx::Account> accounts;
            accounts.reserve(holders.size());
            for (auto const& h : holders)
                accounts.push_back(h.account);
            return accounts;
        }
    };

    // Set up an MPT environment suitable for batch testing.
    // alice is issuer; bob has 'bobAmt' in confidential spending; carol has
    // 'carolAmt' in confidential spending; dave is initialised with pubkey but
    // zero spending/inbox.
    static void
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
            .flags = tfMPTCanTransfer | tfMPTCanLock | tfMPTCanHoldConfidentialBalance,
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

        mpt.set({
            .account = alice,
            .issuerPubKey = mpt.getPubKey(alice),
        });

        if (bobAmt > 0)
        {
            mpt.convert({
                .account = bob,
                .amt = bobAmt,
                .holderPubKey = mpt.getPubKey(bob),
            });
            mpt.mergeInbox({.account = bob});
        }
        else
        {
            mpt.convert({
                .account = bob,
                .amt = 0,
                .holderPubKey = mpt.getPubKey(bob),
            });
        }

        if (carolAmt > 0)
        {
            mpt.convert({
                .account = carol,
                .amt = carolAmt,
                .holderPubKey = mpt.getPubKey(carol),
            });
            mpt.mergeInbox({.account = carol});
        }
        else
        {
            mpt.convert({
                .account = carol,
                .amt = 0,
                .holderPubKey = mpt.getPubKey(carol),
            });
        }

        // dave: register pubkey only (0 spending/inbox)
        mpt.convert({
            .account = dave,
            .amt = 0,
            .holderPubKey = mpt.getPubKey(dave),
        });
    }
};

}  // namespace xrpl
