#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <optional>

namespace xrpl {

class ValidAMM
{
    std::optional<AccountID> ammAccount_;
    std::optional<STAmount> lptAMMBalanceAfter_;
    std::optional<STAmount> lptAMMBalanceBefore_;
    bool ammPoolChanged_{false};

public:
    enum class ZeroAllowed : bool { No = false, Yes = true };

    ValidAMM() = default;
    // `after` is never null. `isDelete` is the only correct way to check for deletions.
    // Check for null defensively, but do not make any logic decisions
// based on whether `after` is set.
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, SLE const&);

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
    finalizeDelete(bool enforce, TER res, beast::Journal const&) const;
    [[nodiscard]] bool
    finalizeDeposit(STTx const&, ReadView const&, bool enforce, beast::Journal const&) const;
    // Includes clawback
    [[nodiscard]] bool
    finalizeWithdraw(STTx const&, ReadView const&, bool enforce, beast::Journal const&) const;
    [[nodiscard]] bool
    finalizeDEX(bool enforce, beast::Journal const&) const;
    [[nodiscard]] bool
    generalInvariant(STTx const&, ReadView const&, ZeroAllowed zeroAllowed, beast::Journal const&)
        const;
};

}  // namespace xrpl
