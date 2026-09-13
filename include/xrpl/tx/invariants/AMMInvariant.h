#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace xrpl {

class ValidAMM
{
    std::optional<AccountID> ammAccount_;
    std::optional<STAmount> lptAMMBalanceAfter_;
    std::optional<STAmount> lptAMMBalanceBefore_;
    std::optional<STAmount> lptAMMBalanceBeforeDeletion_;
    // feeGrowthGlobal0/1 before/after the tx (CL only — nullopt for CP/SS).
    // Used to enforce monotonicity (audit #21): fee growth can only ever
    // increase. A regression would mean fees were stolen from LPs.
    std::optional<Number> feeGrowthGlobal0Before_;
    std::optional<Number> feeGrowthGlobal1Before_;
    std::optional<Number> feeGrowthGlobal0After_;
    std::optional<Number> feeGrowthGlobal1After_;
    // CL tick lifecycle this tx touched. (ammID, tickIndex, expectedBitSet)
    // - expectedBitSet=true  if tick SLE was created (before=null, after!=null)
    // - expectedBitSet=false if tick SLE was deleted (before!=null, after=null)
    // finalize verifies the bitmap word's bit at finalize-time matches expected.
    struct TickLifecycle
    {
        uint256 ammID;
        std::int32_t tickIndex;
        bool expectedBitSet;
    };
    std::vector<TickLifecycle> tickLifecycles_;
    std::shared_ptr<SLE const> ammSle_;
    bool ammPoolChanged_{false};
    bool ammDeleted_{false};

public:
    enum class ZeroAllowed : bool { No = false, Yes = true };

    ValidAMM() = default;
    void
    visitEntry(bool, SLE::const_ref, SLE::const_ref);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);

private:
    [[nodiscard]] bool
    finalizeBid(bool enforce, beast::Journal const&) const;
    [[nodiscard]] bool
    finalizeVote(bool enforce, beast::Journal const&) const;
    [[nodiscard]] bool
    finalizeCreate(STTx const&, ReadView const&, bool enforce, beast::Journal const&) const;
    [[nodiscard]] bool
    finalizeDelete(bool enforce, bool enforceAMMDelete, TER res, beast::Journal const&) const;
    [[nodiscard]] bool
    finalizeDeposit(STTx const&, ReadView const&, bool enforce, beast::Journal const&) const;
    // Includes clawback
    [[nodiscard]] bool
    finalizeWithdraw(
        STTx const&,
        ReadView const&,
        bool enforce,
        bool enforceAMMDelete,
        beast::Journal const&) const;
    [[nodiscard]] bool
    finalizeDEX(bool enforce, beast::Journal const&) const;
    [[nodiscard]] bool
    generalInvariant(STTx const&, ReadView const&, ZeroAllowed zeroAllowed, beast::Journal const&)
        const;
    // feeGrowthGlobal0/1 monotonicity check (audit #21). Returns false if
    // the post-tx fee growth on either side is strictly less than pre-tx.
    [[nodiscard]] bool
    finalizeFeeGrowthMonotonic(bool enforce, beast::Journal const&) const;
    // Tick bitmap ↔ tick-SLE consistency. For every tick SLE whose
    // existence changed during the tx, verify the corresponding bit in the
    // bitmap word matches: set if the SLE now exists, clear (or word
    // absent) if it was deleted. Catches forgotten bitmap maintenance.
    [[nodiscard]] bool
    finalizeTickBitmapConsistency(
        ReadView const& view,
        bool enforce,
        beast::Journal const&) const;

    // For CtBinned pools: enumerate bin SLEs via the AMM pseudo-account
    // owner directory, sum reserves, and assert:
    //   (a) sum(bin.reserve0) == AMM trustline balance of asset0
    //   (b) sum(bin.reserve1) == AMM trustline balance of asset1
    //   (c) sfActiveBinID is valid: either the pool is empty (no bins)
    //       or the bin pointed to exists in the registry
    [[nodiscard]] bool
    finalizeBinnedConsistency(
        ReadView const& view,
        bool enforce,
        beast::Journal const&) const;
};

}  // namespace xrpl
