#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace xrpl {

/**
 * @brief Collects vault and share-issuance snapshots from ledger entry visits.
 *
 * Used by per-transaction invariant checks (e.g. VaultCreate, VaultSet) that
 * need vault and MPTokenIssuance state, optionally with balance-delta tracking.
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

    /**
     * @brief Balance-change delta for a single ledger entry.
     *
     * Mirrors ValidVault::DeltaInfo.  @c scale carries the STAmount exponent
     * so that callers can round to the coarsest representable precision.
     */
    struct DeltaInfo final
    {
        Number delta = kNumZero;
        std::optional<int> scale;

        /**
         * @brief Compute the delta between two Numbers at the coarsest scale.
         */
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
     * @brief Find shares in beforeMPTs_ whose mptID matches (deleted entries).
     */
    [[nodiscard]] std::optional<Shares>
    findDeletedShares(uint192 const& mptID) const;

    /**
     * @brief Access the raw vector of before-state MPTokenIssuance snapshots.
     */
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
     * to the vault asset held by @p id.
     *
     * @param vaultAsset The asset held by the vault.
     * @param id         Account whose asset delta is requested.
     * @returns The delta, or std::nullopt if the entry was not touched.
     */
    [[nodiscard]] std::optional<DeltaInfo>
    deltaAssets(Asset const& vaultAsset, AccountID const& id) const;

    /**
     * @brief Compute the coarsest scale required to represent all numbers.
     */
    [[nodiscard]] static std::int32_t
    computeCoarsestScale(std::vector<DeltaInfo> const& numbers);

private:
    std::vector<Vault> afterVault_;
    std::vector<Vault> beforeVault_;
    std::vector<Shares> afterMPTs_;
    std::vector<Shares> beforeMPTs_;
    std::unordered_map<uint256, DeltaInfo> deltas_;
};

}  // namespace xrpl
