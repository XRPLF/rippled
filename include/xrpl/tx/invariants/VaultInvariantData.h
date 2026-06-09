#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <optional>
#include <vector>

namespace xrpl {

/**
 * @brief Collects vault and share-issuance snapshots from ledger entry visits.
 *
 * Used by per-transaction invariant checks (e.g. VaultCreate) that need
 * vault and MPTokenIssuance state without the full balance-delta tracking
 * that ValidVault maintains.
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

    /** Find deleted shares in beforeMPTs_ whose mptID matches. */
    [[nodiscard]] std::optional<Shares>
    findDeletedShares(uint192 const& mptID) const;

private:
    std::vector<Vault> afterVault_;
    std::vector<Vault> beforeVault_;
    std::vector<Shares> afterMPTs_;
    std::vector<Shares> beforeMPTs_;
};

}  // namespace xrpl
