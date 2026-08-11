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
 * @brief Collects vault-related ledger-entry snapshots and balance-change
 *        deltas for use in per-transactor invariant checks.
 *
 * Each vault transactor that performs per-transactor invariant checking holds
 * one instance of this class as a private member. During
 * @c visitInvariantEntry the transactor calls @c visitEntry for every touched
 * SLE; during @c finalizeInvariants it queries the collected data via the
 * accessor and helper methods below.
 */
class VaultInvariantData
{
public:
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

        [[nodiscard]] static Vault
        make(SLE const&);
    };

    struct Shares final
    {
        uint256 sleKey = beast::kZero;
        MPTIssue share;
        std::uint64_t sharesTotal = 0;
        std::uint64_t sharesMaximum = 0;

        [[nodiscard]] static Shares
        make(SLE const&);
    };

    struct DeltaInfo final
    {
        Number delta = kNumZero;
        std::optional<int> scale;

        [[nodiscard]] static DeltaInfo
        makeDelta(Number const& before, Number const& after, Asset const& asset);
    };

    // Feed a single SLE change into the collected data.
    void
    visitEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after);

    // Snapshot accessors -------------------------------------------------------

    [[nodiscard]] std::vector<Vault> const&
    afterVault() const
    {
        return afterVault_;
    }

    [[nodiscard]] std::vector<Vault> const&
    beforeVault() const
    {
        return beforeVault_;
    }

    // Search afterMPTs_ for the share issuance matching @p afterVault.
    [[nodiscard]] std::optional<Shares>
    resolveUpdatedShares(Vault const& afterVault) const;

    // Search beforeMPTs_ for the share issuance matching @p beforeVault.
    [[nodiscard]] std::optional<Shares>
    resolveBeforeShares(Vault const& beforeVault) const;

    // Delta helpers ------------------------------------------------------------

    [[nodiscard]] std::optional<DeltaInfo>
    deltaAssets(AccountID const& id) const;

    [[nodiscard]] std::optional<DeltaInfo>
    deltaAssetsTxAccount(STTx const& tx, XRPAmount fee) const;

    [[nodiscard]] std::optional<DeltaInfo>
    deltaShares(AccountID const& id) const;

    // Utilities ----------------------------------------------------------------

    [[nodiscard]] static bool
    isVaultEmpty(Vault const& vault);

    [[nodiscard]] static std::int32_t
    computeCoarsestScale(std::vector<DeltaInfo> const& numbers);

    [[nodiscard]] std::int32_t
    computeVaultMinScale(DeltaInfo const& vaultDelta, Rules const& rules) const;

private:
    std::vector<Vault> afterVault_;
    std::vector<Shares> afterMPTs_;
    std::vector<Vault> beforeVault_;
    std::vector<Shares> beforeMPTs_;
    std::unordered_map<uint256, DeltaInfo> deltas_;
};

}  // namespace xrpl
