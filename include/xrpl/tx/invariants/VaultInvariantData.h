#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/XRPAmount.h>

#include <optional>
#include <unordered_map>
#include <vector>

namespace xrpl {

/**
 * @brief Collects vault and share-issuance snapshots from ledger entry visits,
 * including full balance-delta tracking for per-transaction invariant checks.
 *
 * Used by per-transaction invariant checks (e.g. VaultCreate, VaultWithdraw)
 * that need vault and MPTokenIssuance state with balance-delta tracking.
 */
class VaultInvariantData
{
public:
    struct Vault
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

        static Vault
        make(SLE const&);
    };

    struct Shares
    {
        MPTIssue share;
        std::uint64_t sharesTotal = 0;
        std::uint64_t sharesMaximum = 0;

        static Shares
        make(SLE const&);
    };

    struct DeltaInfo
    {
        Number delta = kNumZero;
        std::optional<int> scale;

        // Compute the delta between two Numbers, taking the coarsest scale
        [[nodiscard]] static DeltaInfo
        makeDelta(Number const& before, Number const& after, Asset const& asset);
    };

    void
    visitEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after);

    [[nodiscard]] std::vector<Vault> const&
    afterVaults() const
    {
        return afterVault_;
    }

    [[nodiscard]] std::vector<Vault> const&
    beforeVaults() const
    {
        return beforeVault_;
    }

    /** Find shares in afterMPTs_ whose mptID matches. */
    [[nodiscard]] std::optional<Shares>
    findShares(uint192 const& mptID) const;

    /**
     * @brief Find a deleted (before-only) MPTokenIssuance whose mptID matches.
     *
     * Returns the Shares snapshot captured in beforeMPTs_ for the given mptID,
     * or std::nullopt if none was deleted in this transaction.
     */
    [[nodiscard]] std::optional<Shares>
    findDeletedShares(uint192 const& mptID) const;

    /** All MPTokenIssuance snapshots captured before modification or deletion. */
    [[nodiscard]] std::vector<Shares> const&
    beforeMPTIssuances() const
    {
        return beforeMPTs_;
    }

    /**
     * @brief Return the vault-asset balance-change delta for an account.
     *
     * Looks up the ledger-entry delta recorded during visitEntry for the
     * account entry (XRP), trust line (IOU), or MPToken (MPT) that corresponds
     * to the vault asset held by id.
     *
     * @param id Account whose asset delta is requested.
     * @returns The delta, or std::nullopt if the entry was not touched.
     */
    [[nodiscard]] std::optional<DeltaInfo>
    deltaAssets(AccountID const& id) const;

    /**
     * @brief Return the vault-asset delta for the transaction's sending
     *        account, adjusted for the fee.
     *
     * Calls deltaAssets for tx[sfAccount] and, for non-delegated XRP
     * transactions, adds the consumed fee back so the invariant sees the net
     * asset movement rather than the fee-reduced balance change.
     *
     * @param tx  The transaction being applied.
     * @param fee Fee charged by this transaction.
     * @returns The fee-adjusted delta, or std::nullopt if the net delta is
     *          zero or the account entry was not touched.
     */
    [[nodiscard]] std::optional<DeltaInfo>
    deltaAssetsTxAccount(STTx const& tx, XRPAmount fee) const;

    /**
     * @brief Return the vault-share balance-change delta for an account.
     *
     * For the vault's pseudo-account the MPTokenIssuance outstanding-amount
     * delta is returned; for all other accounts the MPToken delta is returned.
     *
     * @param id Account whose share delta is requested.
     * @returns The delta, or std::nullopt if the entry was not touched.
     */
    [[nodiscard]] std::optional<DeltaInfo>
    deltaShares(AccountID const& id) const;

    /**
     * @brief Compute the coarsest scale required to represent all numbers.
     */
    [[nodiscard]] static std::int32_t
    computeCoarsestScale(std::vector<DeltaInfo> const& numbers);

    /**
     * @brief Compute the minimum STAmount scale for rounding invariant
     *        calculations.
     *
     * Post-amendment (fixCleanup3_2_0) this is simply the posterior
     * assetsTotal scale.  Pre-amendment it is the coarsest scale across
     * vaultDelta and both asset-field deltas.
     *
     * @param vaultDelta Delta of the vault's asset balance for this transaction.
     * @param rules      Active ledger rules (used to check the amendment).
     * @returns The minimum scale to apply when rounding vault-related amounts.
     */
    [[nodiscard]] std::int32_t
    computeVaultMinScale(DeltaInfo const& vaultDelta, Rules const& rules) const;

private:
    std::vector<Vault> afterVault_;
    std::vector<Vault> beforeVault_;
    std::vector<Shares> afterMPTs_;
    std::vector<Shares> beforeMPTs_;
    std::unordered_map<uint256, DeltaInfo> deltas_;

    [[nodiscard]] std::optional<DeltaInfo>
    lookupDelta(uint256 const& key) const;
};

}  // namespace xrpl
