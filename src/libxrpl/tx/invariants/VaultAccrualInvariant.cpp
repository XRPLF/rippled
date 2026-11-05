#include <xrpl/tx/invariants/VaultAccrualInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>

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
ValidVaultAccrual::visitEntry(bool isDelete, SLE::ConstRef before, SLE::ConstRef after)
{
    auto const read = [](SLE::ConstRef sle) {
        return Accrual{
            .accrualRate = sle->at(sfAccrualRate),
            .unearnedInterest = sle->at(sfUnearnedInterest),
            .accrualAnchor = sle->at(sfAccrualAnchor),
            .struckPrice = sle->at(sfStruckPrice),
            .struckDepositPrice = sle->at(sfStruckDepositPrice),
            .struckUntil = sle->at(sfStruckUntil),
            .assetsTotal = sle->at(sfAssetsTotal),
            .assetsAvailable = sle->at(sfAssetsAvailable),
        };
    };

    if (before && before->getType() == ltVAULT)
    {
        before_.push_back(read(before));
        beforeVault_ = before;
    }

    // A deleted vault constrains nothing about the state it no longer has.
    if (!isDelete && after && after->getType() == ltVAULT)
        after_.push_back(read(after));

    if (after && after->getType() == ltLOAN)
    {
        std::uint32_t const stampAfter = after->at(sfLastAccrualTime);
        std::uint32_t const stampBefore = before ? before->at(sfLastAccrualTime) : 0;
        loanClockBackwards_ = loanClockBackwards_ || stampAfter < stampBefore;
        loanClockMoved_ = loanClockMoved_ || stampAfter != stampBefore;
        loanAccruedChange_ += Number{after->at(sfAccruedInterest)} -
            (before ? Number{before->at(sfAccruedInterest)} : Number{});
    }
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

    auto const txType = tx.getTxnType();

    if (loanClockBackwards_)
    {
        JLOG(j.fatal()) << "Invariant failed: loan last accrual time moved backwards";
        return false;
    }

    // A loan's clock moves only where the loan settles.
    if (loanClockMoved_ && !isLoanTransaction(txType))
    {
        JLOG(j.fatal()) << "Invariant failed: loan settled outside a loan transaction";
        return false;
    }

    // Pair the states only when the transaction touched a single vault, which is
    // every transaction that can move these fields.
    if (before_.size() != 1 || after_.size() != 1)
        return true;

    auto const& was = before_.front();
    auto const& is = after_.front();

    bool const accrualMoved = is.accrualRate != was.accrualRate ||
        is.unearnedInterest != was.unearnedInterest || is.accrualAnchor != was.accrualAnchor;

    if (isDealingTransaction(txType) && accrualMoved)
    {
        JLOG(j.fatal()) << "Invariant failed: dealing transaction changed vault accrual state";
        return false;
    }

    if (accrualMoved && !isLoanTransaction(txType) && !isDealingTransaction(txType))
    {
        JLOG(j.fatal()) << "Invariant failed: vault settled outside a loan transaction";
        return false;
    }

    // A window's price, once struck, stands until the window it was struck for
    // has passed. Only a deal inside a window may strike one.
    if (is.struckUntil < was.struckUntil)
    {
        JLOG(j.fatal()) << "Invariant failed: vault struck window moved backwards";
        return false;
    }

    bool const priced =
        is.struckPrice != was.struckPrice || is.struckDepositPrice != was.struckDepositPrice;
    bool const struck = priced || is.struckUntil != was.struckUntil;
    // A LoanManage that moves the vault's value strikes the window's prices
    // again, for the window already struck.
    bool const restruck = txType == ttLOAN_MANAGE && is.struckUntil == was.struckUntil;
    if (struck && !isDealingTransaction(txType) && !restruck)
    {
        JLOG(j.fatal()) << "Invariant failed: vault price struck outside a dealing transaction";
        return false;
    }

    // A deal striking again within the same window would reprice an earlier
    // one, which is the timing problem the struck price exists to close.
    if (isDealingTransaction(txType) && priced && is.struckUntil == was.struckUntil)
    {
        JLOG(j.fatal()) << "Invariant failed: vault price struck twice in one window";
        return false;
    }

    // A loan transaction recognizes at most what its loans' clocks added to
    // their sfAccruedInterest, plus the cash it brought in, to the last digit
    // of sfAssetsTotal.
    if (isLoanTransaction(txType) && beforeVault_ &&
        getAccountingMethod(beforeVault_) == kVaultAccountingAccrual)
    {
        Number const cashIn = is.assetsAvailable > was.assetsAvailable
            ? is.assetsAvailable - was.assetsAvailable
            : Number{};
        Asset const asset = beforeVault_->at(sfAsset);
        Number const lastDigit{1, scale(is.assetsTotal, asset)};
        if (is.assetsTotal - was.assetsTotal > loanAccruedChange_ + cashIn + lastDigit)
        {
            JLOG(j.fatal()) << "Invariant failed: loan transaction recognized vault interest "
                               "beyond the clock and the cash received";
            return false;
        }
    }

    return true;
}

}  // namespace xrpl
