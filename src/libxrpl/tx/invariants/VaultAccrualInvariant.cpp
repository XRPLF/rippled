#include <xrpl/tx/invariants/VaultAccrualInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>

namespace xrpl {

namespace {

[[nodiscard]] bool
isLoanTransaction(TxType const txType)
{
    switch (txType)
    {
        case ttLOAN_SET:
        case ttLOAN_PAY:
        case ttLOAN_MANAGE:
        case ttLOAN_DELETE:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] bool
isDealingTransaction(TxType const txType)
{
    switch (txType)
    {
        case ttVAULT_DEPOSIT:
        case ttVAULT_WITHDRAW:
        case ttVAULT_CLAWBACK:
            return true;
        default:
            return false;
    }
}

}  // namespace

void
ValidVaultAccrual::visitEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after)
{
    auto const read = [](SLE::const_ref sle) {
        return Accrual{
            .accrualRate = sle->at(sfAccrualRate),
            .unearnedInterest = sle->at(sfUnearnedInterest),
            .lastAccrualTime = sle->at(sfLastAccrualTime),
            .struckPrice = sle->at(sfStruckPrice),
            .struckUntil = sle->at(sfStruckUntil)};
    };

    if (before && before->getType() == ltVAULT)
        before_.push_back(read(before));

    // A deleted vault constrains nothing about the state it no longer has.
    if (!isDelete && after && after->getType() == ltVAULT)
        after_.push_back(read(after));
}

bool
ValidVaultAccrual::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const&,
    beast::Journal const& j)
{
    if (!isTesSuccess(result))
        return true;

    for (auto const& accrual : after_)
    {
        if (accrual.accrualRate < Number{})
        {
            JLOG(j.fatal()) << "Invariant failed: vault accrual rate is negative";
            return false;
        }

        if (accrual.unearnedInterest < Number{})
        {
            JLOG(j.fatal()) << "Invariant failed: vault unearned interest is negative";
            return false;
        }
    }

    // Pair the states only when the transaction touched a single vault, which is
    // every transaction that can move these fields.
    if (before_.size() != 1 || after_.size() != 1)
        return true;

    auto const& was = before_.front();
    auto const& is = after_.front();
    auto const txType = tx.getTxnType();

    if (is.lastAccrualTime < was.lastAccrualTime)
    {
        JLOG(j.fatal()) << "Invariant failed: vault last accrual time moved backwards";
        return false;
    }

    if (is.lastAccrualTime != was.lastAccrualTime && !isLoanTransaction(txType))
    {
        JLOG(j.fatal()) << "Invariant failed: vault settled outside a loan transaction";
        return false;
    }

    if (isDealingTransaction(txType) &&
        (is.accrualRate != was.accrualRate || is.unearnedInterest != was.unearnedInterest ||
         is.lastAccrualTime != was.lastAccrualTime))
    {
        JLOG(j.fatal()) << "Invariant failed: dealing transaction changed vault accrual state";
        return false;
    }

    // A window's price, once struck, stands until the window it was struck for
    // has passed. Only a deal inside a window may strike one.
    if (is.struckUntil < was.struckUntil)
    {
        JLOG(j.fatal()) << "Invariant failed: vault struck window moved backwards";
        return false;
    }

    bool const struck = is.struckPrice != was.struckPrice || is.struckUntil != was.struckUntil;
    if (struck && !isDealingTransaction(txType))
    {
        JLOG(j.fatal()) << "Invariant failed: vault price struck outside a dealing transaction";
        return false;
    }

    // Striking again within the same window would let a later deal reprice an
    // earlier one, which is the timing problem the struck price exists to close.
    if (is.struckPrice != was.struckPrice && is.struckUntil == was.struckUntil)
    {
        JLOG(j.fatal()) << "Invariant failed: vault price struck twice in one window";
        return false;
    }

    return true;
}

}  // namespace xrpl
