#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/mpt.h>

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <array>
#include <cstddef>
#include <cstdint>
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

    // Creates the MPT issuance on the given Env, authorizes and funds each
    // holder, generates keys for the issuer, holders and optional auditor,
    // registers the issuer/auditor keys, and converts part of each holder's
    // balance to a confidential balance.
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
            std::optional<test::jtx::Account> auditor = std::nullopt);

    private:
        static std::vector<test::jtx::Account>
        extractAccounts(std::vector<HolderInit> const& holders);
    };

    // Create an issuance that can hold confidential balances, with the listed
    // holders funded and authorized, and a key pair generated for the issuer,
    // every holder, and every extra key owner. The keys are
    // generated but not registered.
    static void
    setupConfidentialIssuance(
        test::jtx::MPTTester& mpt,
        test::jtx::Account const& issuer,
        std::vector<test::jtx::Account> const& holders,
        std::vector<test::jtx::Account> const& keyOwners = {},
        std::uint32_t flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance);

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
        std::uint64_t carolAmt);

    // Helper struct to encapsulate common setup for integration tests.
    struct ConfidentialSendSetup
    {
        // Constants
        uint64_t sendAmount;
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
            std::optional<std::reference_wrapper<test::jtx::Account const>> auditor = std::nullopt);

        // Generate proof with current account sequence
        std::optional<Buffer>
        generateProof(
            test::jtx::MPTTester& mpt,
            test::jtx::Env& env,
            test::jtx::Account const& sender,
            test::jtx::Account const& dest) const;

        [[nodiscard]] test::jtx::MPTConfidentialSend
        sendArgs(
            test::jtx::Account const& sender,
            test::jtx::Account const& dest,
            Buffer const& proof,
            std::optional<TER> err = std::nullopt) const;
    };

    // Get a bad ciphertext with valid structure but cryptographic invalid for
    // testing purposes. For preflight test purposes.
    static Buffer const&
    getBadCiphertext();

    // Get a trivial buffer that is structurally and mathematically valid, but
    // contains invalid data that does not match the ledger state. For preclaim
    // test purposes.
    static Buffer const&
    getTrivialCiphertext();

    // Returns a valid compressed EC point (33 bytes) that can pass preflight
    // validation but contains invalid data for preclaim test purposes.
    static Buffer const&
    getTrivialCommitment();

    // Returns a hex-encoded send proof of the correct length filled with
    // placeholder data. It passes the proof length check in preflight but
    // fails proof verification.
    static std::string
    getTrivialSendProofHex();

    // Offset where the bulletproof begins in a send proof blob.
    // Proof layout: [compact_sigma | bulletproof]
    static constexpr size_t kBulletproofOffset = kEcSendProofLength - kEcDoubleBulletproofLength;

    // Generate a forged aggregated bulletproof (double bulletproof) for
    // the given values and blinding factors. Used to test that splicing
    // a bulletproof claiming a different remaining balance is rejected.
    static Buffer
    getForgedBulletproof(
        std::array<uint64_t, 2> const& values,
        std::array<Buffer, 2> const& blindingFactors,
        uint256 const& contextHash);

    // Generate a forged single bulletproof for a single value and blinding factor.
    // Used to test ConvertBack overdraft prevention via bulletproof verification.
    static Buffer
    getForgedSingleBulletproof(
        uint64_t value,
        Buffer const& blindingFactor,
        uint256 const& contextHash);

    // Forges a ConvertBack proof (compact sigma + single bulletproof) whose
    // sigma component claims claimedBalance (which may be wrong) while binding
    // to the real pedersen commitment and to the encrypted spending balance
    // already on the ledger. The bulletproof component is built from the real
    // remaining balance (realBalance - amt) so it stays honest.
    // mpt_get_convert_back_proof validates its inputs before proving, so it
    // cannot be used to build such an inconsistent proof.
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
        uint256 const& contextHash);

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
        ConfidentialSendSetup const& setup);
};

}  // namespace xrpl
