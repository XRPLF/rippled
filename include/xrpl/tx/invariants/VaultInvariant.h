#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <unordered_map>
#include <vector>

namespace xrpl {

/*
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
 * - no vault transaction can change loss unrealized (it's updated by loan
 *   transactions)
 *
 */
class ValidVault
{
    Number static constexpr zero{};

    struct Vault final
    {
        uint256 key = beast::zero;
        Asset asset = {};
        AccountID pseudoId = {};
        AccountID owner = {};
        uint192 shareMPTID = beast::zero;
        Number assetsTotal = 0;
        Number assetsAvailable = 0;
        Number assetsMaximum = 0;
        Number lossUnrealized = 0;

        Vault static make(SLE const&);
    };

    struct Shares final
    {
        MPTIssue share = {};
        std::uint64_t sharesTotal = 0;
        std::uint64_t sharesMaximum = 0;

        Shares static make(SLE const&);
    };

    std::vector<Vault> afterVault_ = {};
    std::vector<Shares> afterMPTs_ = {};
    std::vector<Vault> beforeVault_ = {};
    std::vector<Shares> beforeMPTs_ = {};
    std::unordered_map<uint256, Number> deltas_ = {};

public:
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
