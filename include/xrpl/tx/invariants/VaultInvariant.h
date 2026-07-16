#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace xrpl {

/**
 * @brief Invariants: Vault object and MPTokenIssuance for vault shares
 *
 * - vault deleted and vault created is empty
 * - vault created must be linked to pseudo-account for shares and assets
 * - vault must have MPTokenIssuance for shares
 * - vault without shares outstanding must have no shares
 * - loss unrealized does not exceed the difference between assets total and
 *   assets available
 * - assets available do not exceed assets total
 * - vault deposit increases assets and share issuance, and adds to:
 *   total assets, assets available, shares outstanding
 * - vault withdrawal and clawback reduce assets and share issuance, and
 *   subtracts from: total assets, assets available, shares outstanding
 * - vault set must not alter the vault assets or shares balance
 * - loan set moves the requested principal out of the vault: it must create
 *   exactly one loan, decreases assets available (and the vault balance) by the
 *   principal, grows assets outstanding by exactly the interest due booked on
 *   the created loan, and does not change shares
 * - loan manage never removes assets from the vault: assets available may only
 *   grow (and the vault balance grows with it, by the returned first-loss
 *   capital on a default), assets outstanding may only shrink (realized loss),
 *   loss unrealized stays non-negative, and shares do not change; a loan
 *   manage with none of the sub-operation flags (impair, unimpair, default)
 *   is a no-op and must not modify the vault
 * - loan pay adds the paid principal and interest to the vault: assets
 *   available (and the vault balance) increase by the same amount, which is at
 *   most the amount paid, loss unrealized stays non-negative, and shares do not
 *   change; and assets outstanding move in lock-step: their change equals the
 *   cash received plus the change in the paid loan's claim on the vault, which
 *   verifies the payment was split correctly between principal and interest
 * - no vault transaction can change loss unrealized (it's updated by loan
 *   transactions)
 *
 */
class ValidVault
{
    static constexpr Number kZero{};

    struct Vault final
    {
        uint256 key = beast::kZero;
        Asset asset;
        AccountID pseudoId;
        AccountID owner;
        uint192 shareMPTID = beast::kZero;
        Number assetsTotal = 0;
        Number assetsAvailable = 0;
        Number assetsMaximum = 0;
        Number lossUnrealized = 0;
        std::uint8_t withdrawalPolicy = 0;
        std::uint8_t scale = 0;

        Vault static make(SLE const&);
    };

    struct Shares final
    {
        MPTIssue share;
        std::uint64_t sharesTotal = 0;
        std::uint64_t sharesMaximum = 0;

        Shares static make(SLE const&);
    };

    struct Loan final
    {
        uint256 key = beast::kZero;
        Number principalOutstanding = 0;
        Number totalValueOutstanding = 0;
        Number managementFeeOutstanding = 0;

        // Interest booked to the vault at loan creation: the portion of the
        // total value owed that is neither principal nor broker management fee.
        [[nodiscard]] Number
        interestDue() const;

        // The vault's claim on the loan: the total value owed less the broker's
        // management fee (which belongs to the broker, not the vault).
        [[nodiscard]] Number
        claim() const;

        Loan static make(SLE const&);
    };

public:
    struct DeltaInfo final
    {
        Number delta = kNumZero;
        std::optional<int> scale;

        // Compute the delta between two Numbers, taking the coarsest scale
        [[nodiscard]] static DeltaInfo
        makeDelta(Number const& before, Number const& after, Asset const& asset);
    };

private:
    std::vector<Vault> afterVault_;
    std::vector<Shares> afterMPTs_;
    std::vector<Loan> afterLoan_;
    std::vector<Vault> beforeVault_;
    std::vector<Shares> beforeMPTs_;
    std::vector<Loan> beforeLoan_;
    std::unordered_map<uint256, DeltaInfo> deltas_;

    /**
     * @brief Compute the minimum STAmount scale for rounding invariant
     *        calculations.
     *
     * Post-amendment (@c fixCleanup3_2_0) this is simply the posterior
     * @c assetsTotal scale.  Pre-amendment it is the coarsest scale across
     * @p vaultDelta and both asset-field deltas.
     *
     * @param vaultDelta Delta of the vault's asset balance for this transaction.
     * @param rules      Active ledger rules (used to check the amendment).
     * @return The minimum scale to apply when rounding vault-related amounts.
     */
    [[nodiscard]] std::int32_t
    computeVaultMinScale(DeltaInfo const& vaultDelta, Rules const& rules) const;

    /**
     * @brief Return the vault-asset balance-change delta for an account.
     *
     * Looks up the ledger-entry delta recorded during @c visitEntry for the
     * account entry (XRP), trust line (IOU), or MPToken (MPT) that corresponds
     * to the vault asset held by @p id.
     *
     * @param id Account whose asset delta is requested.
     * @return The delta, or @c std::nullopt if the entry was not touched.
     */
    [[nodiscard]] std::optional<DeltaInfo>
    deltaAssets(AccountID const& id) const;

    /**
     * @brief Return the vault-asset delta for the transaction's sending
     *        account, adjusted for the fee.
     *
     * Calls @c deltaAssets for @c tx[sfAccount] and, for non-delegated XRP
     * transactions, adds the consumed fee back so the invariant sees the net
     * asset movement rather than the fee-reduced balance change.
     *
     * @param tx  The transaction being applied.
     * @param fee Fee charged by this transaction.
     * @return The fee-adjusted delta, or @c std::nullopt if the net delta is
     *         zero or the account entry was not touched.
     */
    [[nodiscard]] std::optional<DeltaInfo>
    deltaAssetsTxAccount(STTx const& tx, XRPAmount fee) const;

    /**
     * @brief Return the vault-share balance-change delta for an account.
     *
     * For the vault's pseudo-account the @c MPTokenIssuance outstanding-amount
     * delta is returned; for all other accounts the @c MPToken delta is
     * returned.
     *
     * @param id Account whose share delta is requested.
     * @return The delta, or @c std::nullopt if the entry was not touched.
     */
    [[nodiscard]] std::optional<DeltaInfo>
    deltaShares(AccountID const& id) const;

    /**
     * @brief Check whether a vault holds no assets.
     *
     * @param vault Snapshot of the vault to test.
     * @return @c true when both @c assetsAvailable and @c assetsTotal are
     *         zero.
     */
    [[nodiscard]] static bool
    isVaultEmpty(Vault const& vault);

public:
    // Compute the coarsest scale required to represent all numbers
    [[nodiscard]] static std::int32_t
    computeCoarsestScale(std::vector<DeltaInfo> const& numbers);

    void
    visitEntry(bool, SLE::const_ref, SLE::const_ref);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
