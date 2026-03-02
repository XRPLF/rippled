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
    bool ammPoolChanged_;

public:
    enum class ZeroAllowed : bool { No = false, Yes = true };

    ValidAMM() : ammPoolChanged_{false}
    {
    }
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);

private:
    bool
    finalizeBid(bool enforce, beast::Journal const&) const;
    bool
    finalizeVote(bool enforce, beast::Journal const&) const;
    bool
    finalizeCreate(STTx const&, ReadView const&, bool enforce, beast::Journal const&) const;
    bool
    finalizeDelete(bool enforce, TER res, beast::Journal const&) const;
    bool
    finalizeDeposit(STTx const&, ReadView const&, bool enforce, beast::Journal const&) const;
    // Includes clawback
    bool
    finalizeWithdraw(STTx const&, ReadView const&, bool enforce, beast::Journal const&) const;
    bool
    finalizeDEX(bool enforce, beast::Journal const&) const;
    bool
    generalInvariant(STTx const&, ReadView const&, ZeroAllowed zeroAllowed, beast::Journal const&)
        const;
};

}  // namespace xrpl
