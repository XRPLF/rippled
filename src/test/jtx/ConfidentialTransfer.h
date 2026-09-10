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

    // Generate a forged single bulletproof for a single value and blinding factor.
    // Used to test ConvertBack overdraft prevention via bulletproof verification.
    static Buffer
    getForgedSingleBulletproof(
        uint64_t value,
        Buffer const& blindingFactor,
        uint256 const& contextHash)
    {
        auto* const ctx = mpt_secp256k1_context();

        secp256k1_pubkey h;
        secp256k1_mpt_get_h_generator(ctx, &h);

        Buffer proof(kEcSingleBulletproofLength);
        size_t proofLen = kEcSingleBulletproofLength;

        if (secp256k1_bulletproof_prove_agg(
                ctx,
                proof.data(),
                &proofLen,
                &value,
                blindingFactor.data(),
                1,  // m = 1 (single bulletproof)
                &h,
                contextHash.data()) == 0)
            Throw<std::runtime_error>("Failed to generate forged single bulletproof");

        return proof;
    }

    // Forges a ConvertBack proof (compact sigma + single bulletproof) whose
    // sigma component claims claimedBalance (which may be wrong) while binding
    // to the real pedersen commitment and encrypted spending balance
    // ciphertext already on the ledger. The bulletproof component is built
    // from realBalance so it stays honest.
    // mpt_get_convert_back_proof does not allow to build a proof whose amount
    // exceeds the holder's claimed balance.
    static Buffer
    getForgedConvertBackProof(
        test::jtx::MPTTester& mpt,
        test::jtx::Account const& holder,
        uint64_t claimedBalance,
        uint64_t realBalance,
        uint64_t amt,
        Buffer const& pedersenCommitment,
        Buffer const& encryptedSpendingBalance,
        Buffer const& pcBlindingFactor,
        uint256 const& contextHash)
    {
        if (pedersenCommitment.size() != kCompressedEcPointLength)
            Throw<std::runtime_error>("getForgedConvertBackProof: bad pedersenCommitment length");
        if (encryptedSpendingBalance.size() != kEcGamalEncryptedTotalLength)
        {
            Throw<std::runtime_error>(
                "getForgedConvertBackProof: bad encryptedSpendingBalance length");
        }
        if (amt > realBalance)
            Throw<std::runtime_error>("getForgedConvertBackProof: amt exceeds realBalance");

        auto* const ctx = mpt_secp256k1_context();
        auto const holderPubKey = requireOptional(mpt.getPubKey(holder), "Missing holder pubkey");
        auto const holderPrivKey =
            requireOptional(mpt.getPrivKey(holder), "Missing holder privkey");

        secp256k1_pubkey pkHolder;
        if (secp256k1_ec_pubkey_parse(
                ctx, &pkHolder, holderPubKey.data(), kCompressedEcPointLength) != 1)
            Throw<std::runtime_error>("Failed to parse holder's public key");

        secp256k1_pubkey pcB;
        if (secp256k1_ec_pubkey_parse(
                ctx, &pcB, pedersenCommitment.data(), kCompressedEcPointLength) != 1)
            Throw<std::runtime_error>("Failed to parse pedersen commitment");

        secp256k1_pubkey b1, b2;
        if (secp256k1_ec_pubkey_parse(
                ctx, &b1, encryptedSpendingBalance.data(), kCompressedEcPointLength) != 1 ||
            secp256k1_ec_pubkey_parse(
                ctx,
                &b2,
                encryptedSpendingBalance.data() + kCompressedEcPointLength,
                kCompressedEcPointLength) != 1)
            Throw<std::runtime_error>("Failed to parse balance ciphertext");

        Buffer sigmaProof(SECP256K1_COMPACT_CONVERTBACK_PROOF_SIZE);
        if (secp256k1_compact_convertback_prove(
                ctx,
                sigmaProof.data(),
                claimedBalance,
                holderPrivKey.data(),
                pcBlindingFactor.data(),
                &pkHolder,
                &b1,
                &b2,
                &pcB,
                contextHash.data()) != 1)
            Throw<std::runtime_error>("Failed to generate convertback sigma proof");

        auto const forgedBulletproof =
            getForgedSingleBulletproof(realBalance - amt, pcBlindingFactor, contextHash);

        Buffer proof(kEcConvertBackProofLength);
        std::memcpy(proof.data(), sigmaProof.data(), SECP256K1_COMPACT_CONVERTBACK_PROOF_SIZE);
        std::memcpy(
            proof.data() + SECP256K1_COMPACT_CONVERTBACK_PROOF_SIZE,
            forgedBulletproof.data(),
            kEcSingleBulletproofLength);

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

    // Forges a ConfidentialMPTSend proof (compact sigma + double bulletproof)
    // for setup.sendAmount against setup's real balance commitment/ciphertext.
    // mpt_get_confidential_send_proof does not allow to build a proof whose amount
    // exceeds the sender's claimed balance.
    static Buffer
    getForgedSendProof(
        test::jtx::MPTTester& mpt,
        test::jtx::Env& env,
        test::jtx::Account const& sender,
        test::jtx::Account const& dest,
        ConfidentialSendSetup const& setup)
    {
        auto* const ctx = mpt_secp256k1_context();

        secp256k1_pubkey c1;
        std::vector<secp256k1_pubkey> c2Vec(setup.recipients.size());
        std::vector<secp256k1_pubkey> pkVec(setup.recipients.size());
        for (std::size_t i = 0; i < setup.recipients.size(); ++i)
        {
            auto const& r = setup.recipients[i];
            if (i == 0 &&
                secp256k1_ec_pubkey_parse(
                    ctx, &c1, r.encryptedAmount.data(), kCompressedEcPointLength) != 1)
                Throw<std::runtime_error>("Failed to parse C1");
            if (secp256k1_ec_pubkey_parse(
                    ctx,
                    &c2Vec[i],
                    r.encryptedAmount.data() + kCompressedEcPointLength,
                    kCompressedEcPointLength) != 1)
                Throw<std::runtime_error>("Failed to parse C2");
            if (secp256k1_ec_pubkey_parse(
                    ctx, &pkVec[i], r.publicKey.data(), kCompressedEcPointLength) != 1)
                Throw<std::runtime_error>("Failed to parse recipient pubkey");
        }

        secp256k1_pubkey pkSender, pcAmount, pcBalance, b1, b2;
        if (secp256k1_ec_pubkey_parse(
                ctx, &pkSender, setup.senderPubKey.data(), kCompressedEcPointLength) != 1 ||
            secp256k1_ec_pubkey_parse(
                ctx, &pcAmount, setup.amountCommitment.data(), kCompressedEcPointLength) != 1 ||
            secp256k1_ec_pubkey_parse(
                ctx, &pcBalance, setup.balanceCommitment.data(), kCompressedEcPointLength) != 1 ||
            secp256k1_ec_pubkey_parse(
                ctx, &b1, setup.prevEncryptedSpending.data(), kCompressedEcPointLength) != 1 ||
            secp256k1_ec_pubkey_parse(
                ctx,
                &b2,
                setup.prevEncryptedSpending.data() + kCompressedEcPointLength,
                kCompressedEcPointLength) != 1)
            Throw<std::runtime_error>("Failed to parse commitments/ciphertext");

        Buffer const senderPrivKey =
            requireOptional(mpt.getPrivKey(sender), "Missing sender privkey");
        auto const ctxHash = getSendContextHash(
            sender.id(), mpt.issuanceID(), env.seq(sender), dest.id(), setup.version);

        Buffer sigmaProof(SECP256K1_COMPACT_STANDARD_PROOF_SIZE);
        if (secp256k1_compact_standard_prove(
                ctx,
                sigmaProof.data(),
                setup.sendAmount,
                setup.prevSpending,
                setup.blindingFactor.data(),
                senderPrivKey.data(),
                setup.balanceBlindingFactor.data(),
                setup.recipients.size(),
                &c1,
                c2Vec.data(),
                pkVec.data(),
                &pcAmount,
                &pkSender,
                &pcBalance,
                &b1,
                &b2,
                ctxHash.data()) != 1)
            Throw<std::runtime_error>("Failed to generate sigma proof");

        // Wraps (mod 2^64) for overdrafts, unlike the ledger's own homomorphic
        // commitment subtraction (mod the curve order) — that mismatch is
        // exactly what makes the forged proof fail verification.
        // Computed without a wrapping `uint64` subtract: Clang UBSan treats
        // unsigned overflow as fatal (see incrementConfidentialVersion).
        std::uint64_t const remaining = setup.sendAmount <= setup.prevSpending
            ? setup.prevSpending - setup.sendAmount
            : ~setup.sendAmount + setup.prevSpending + 1;

        Buffer negAmountBf(kEcBlindingFactorLength);
        Buffer remainingBf(kEcBlindingFactorLength);
        secp256k1_mpt_scalar_negate(negAmountBf.data(), setup.amountBlindingFactor.data());
        secp256k1_mpt_scalar_add(
            remainingBf.data(), setup.balanceBlindingFactor.data(), negAmountBf.data());

        auto const forgedBulletproof = getForgedBulletproof(
            {setup.sendAmount, remaining}, {setup.amountBlindingFactor, remainingBf}, ctxHash);

        Buffer combinedProof(kEcSendProofLength);
        std::memcpy(combinedProof.data(), sigmaProof.data(), SECP256K1_COMPACT_STANDARD_PROOF_SIZE);
        std::memcpy(
            combinedProof.data() + SECP256K1_COMPACT_STANDARD_PROOF_SIZE,
            forgedBulletproof.data(),
            kEcDoubleBulletproofLength);

        return combinedProof;
    }

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
