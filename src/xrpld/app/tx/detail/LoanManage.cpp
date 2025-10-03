//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <xrpld/app/tx/detail/LoanManage.h>
//
#include <xrpld/app/misc/LendingHelpers.h>

#include <xrpl/protocol/TxFlags.h>

namespace ripple {

bool
LoanManage::checkExtraFeatures(PreflightContext const& ctx)
{
    return checkLendingProtocolDependencies(ctx);
}

std::uint32_t
LoanManage::getFlagsMask(PreflightContext const& ctx)
{
    return tfLoanManageMask;
}

NotTEC
LoanManage::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfLoanID] == beast::zero)
        return temINVALID;

    // Flags are mutually exclusive
    if (auto const flagField = ctx.tx[~sfFlags]; flagField && *flagField)
    {
        auto const flags = *flagField & tfUniversalMask;
        if ((flags & (flags - 1)) != 0)
        {
            JLOG(ctx.j.warn())
                << "LoanManage: Only one of tfLoanDefault, tfLoanImpair, or "
                   "tfLoanUnimpair can be set.";
            return temINVALID_FLAG;
        }
    }

    return tesSUCCESS;
}

TER
LoanManage::preclaim(PreclaimContext const& ctx)
{
    auto const& tx = ctx.tx;

    auto const account = tx[sfAccount];
    auto const loanID = tx[sfLoanID];

    auto const loanSle = ctx.view.read(keylet::loan(loanID));
    if (!loanSle)
    {
        JLOG(ctx.j.warn()) << "Loan does not exist.";
        return tecNO_ENTRY;
    }
    // Impairment only allows certain transitions.
    // 1. Once it's in default, it can't be changed.
    // 2. It can get worse: unimpaired -> impaired -> default
    //      or unimpaired -> default
    // 3. It can get better: impaired -> unimpaired
    // 4. If it's in a state, it can't be put in that state again.
    if (loanSle->isFlag(lsfLoanDefault))
    {
        JLOG(ctx.j.warn())
            << "Loan is in default. A defaulted loan can not be modified.";
        return tecNO_PERMISSION;
    }
    if (loanSle->isFlag(lsfLoanImpaired) && tx.isFlag(tfLoanImpair))
    {
        JLOG(ctx.j.warn())
            << "Loan is impaired. A loan can not be impaired twice.";
        return tecNO_PERMISSION;
    }
    if (!(loanSle->isFlag(lsfLoanImpaired) ||
          loanSle->isFlag(lsfLoanDefault)) &&
        (tx.isFlag(tfLoanUnimpair)))
    {
        JLOG(ctx.j.warn())
            << "Loan is unimpaired. Can not be unimpaired again.";
        return tecNO_PERMISSION;
    }
    if (loanSle->at(sfPaymentRemaining) == 0)
    {
        JLOG(ctx.j.warn()) << "Loan is fully paid. A loan can not be modified "
                              "after it is fully paid.";
        return tecNO_PERMISSION;
    }
    if (tx.isFlag(tfLoanDefault) &&
        !hasExpired(
            ctx.view,
            loanSle->at(sfNextPaymentDueDate) + loanSle->at(sfGracePeriod)))
    {
        JLOG(ctx.j.warn())
            << "A loan can not be defaulted before the next payment due date.";
        return tecTOO_SOON;
    }

    auto const loanBrokerID = loanSle->at(sfLoanBrokerID);
    auto const loanBrokerSle = ctx.view.read(keylet::loanbroker(loanBrokerID));
    if (!loanBrokerSle)
    {
        // should be impossible
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }
    if (loanBrokerSle->at(sfOwner) != account)
    {
        JLOG(ctx.j.warn())
            << "LoanBroker for Loan does not belong to the account.";
        return tecNO_PERMISSION;
    }

    return tesSUCCESS;
}

TER
LoanManage::defaultLoan(
    ApplyView& view,
    SLE::ref loanSle,
    SLE::ref brokerSle,
    SLE::ref vaultSle,
    Number const& principalOutstanding,
    Number const& interestOutstanding,
    Asset const& vaultAsset,
    beast::Journal j)
{
    // Calculate the amount of the Default that First-Loss Capital covers:

    std::int32_t const loanScale = loanSle->at(sfLoanScale);
    TenthBips32 const managementFeeRate{brokerSle->at(sfManagementFeeRate)};
    auto brokerDebtTotalProxy = brokerSle->at(sfDebtTotal);
    auto const totalDefaultAmount = principalOutstanding + interestOutstanding;

    // Apply the First-Loss Capital to the Default Amount
    TenthBips32 const coverRateMinimum{brokerSle->at(sfCoverRateMinimum)};
    TenthBips32 const coverRateLiquidation{
        brokerSle->at(sfCoverRateLiquidation)};
    auto const defaultCovered = roundToAsset(
        vaultAsset,
        std::min(
            tenthBipsOfValue(
                tenthBipsOfValue(
                    brokerDebtTotalProxy.value(), coverRateMinimum),
                coverRateLiquidation),
            totalDefaultAmount),
        loanScale);

    auto const vaultDefaultAmount = totalDefaultAmount - defaultCovered;

    // Update the Vault object:

    {
        // Decrease the Total Value of the Vault:
        auto vaultAssetsTotalProxy = vaultSle->at(sfAssetsTotal);
        if (vaultAssetsTotalProxy < vaultDefaultAmount)
        {
            // LCOV_EXCL_START
            JLOG(j.warn())
                << "Vault total assets is less than the vault default amount";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }
        vaultAssetsTotalProxy -= vaultDefaultAmount;
        // Increase the Asset Available of the Vault by liquidated First-Loss
        // Capital and any unclaimed funds amount:
        vaultSle->at(sfAssetsAvailable) += defaultCovered;
        // The loss has been realized
        if (loanSle->isFlag(lsfLoanImpaired))
        {
            auto vaultLossUnrealizedProxy = vaultSle->at(sfLossUnrealized);
            if (vaultLossUnrealizedProxy < totalDefaultAmount)
            {
                JLOG(j.warn())
                    << "Vault unrealized loss is less than the default amount";
                return tefBAD_LEDGER;
            }
            vaultLossUnrealizedProxy -= totalDefaultAmount;
        }
        view.update(vaultSle);
    }

    // Update the LoanBroker object:

    {
        // Decrease the Debt of the LoanBroker:
        if (brokerDebtTotalProxy < totalDefaultAmount)
        {
            // LCOV_EXCL_START
            JLOG(j.warn())
                << "LoanBroker debt total is less than the default amount";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }
        brokerDebtTotalProxy -= totalDefaultAmount;
        // Decrease the First-Loss Capital Cover Available:
        auto coverAvailableProxy = brokerSle->at(sfCoverAvailable);
        if (coverAvailableProxy < defaultCovered)
        {
            // LCOV_EXCL_START
            JLOG(j.warn())
                << "LoanBroker cover available is less than amount covered";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }
        coverAvailableProxy -= defaultCovered;
        view.update(brokerSle);
    }

    // Update the Loan object:
    loanSle->setFlag(lsfLoanDefault);
    loanSle->at(sfPaymentRemaining) = 0;
    loanSle->at(sfPrincipalOutstanding) = 0;
    view.update(loanSle);

    // Return funds from the LoanBroker pseudo-account to the
    // Vault pseudo-account:
    return accountSend(
        view,
        brokerSle->at(sfAccount),
        vaultSle->at(sfAccount),
        STAmount{vaultAsset, defaultCovered},
        j,
        WaiveTransferFee::Yes);
}

TER
LoanManage::impairLoan(
    ApplyView& view,
    SLE::ref loanSle,
    SLE::ref vaultSle,
    Number const& principalOutstanding,
    Number const& interestOutstanding,
    beast::Journal j)
{
    // Update the Vault object(set "paper loss")
    auto vaultLossUnrealizedProxy = vaultSle->at(sfLossUnrealized);
    vaultLossUnrealizedProxy += principalOutstanding + interestOutstanding;
    if (vaultLossUnrealizedProxy >
        vaultSle->at(sfAssetsTotal) - vaultSle->at(sfAssetsAvailable))
    {
        // Having a loss greater than the vault's unavailable assets
        // will leave the vault in an invalid / inconsistent state.
        JLOG(j.warn()) << "Vault unrealized loss is too large, and will "
                          "corrupt the vault.";
        return tecLIMIT_EXCEEDED;
    }
    view.update(vaultSle);

    // Update the Loan object
    loanSle->setFlag(lsfLoanImpaired);
    auto loanNextDueProxy = loanSle->at(sfNextPaymentDueDate);
    if (!hasExpired(view, loanNextDueProxy))
    {
        // loan payment is not yet late -
        // move the next payment due date to now
        loanNextDueProxy = view.parentCloseTime().time_since_epoch().count();
    }
    view.update(loanSle);

    return tesSUCCESS;
}

TER
LoanManage::unimpairLoan(
    ApplyView& view,
    SLE::ref loanSle,
    SLE::ref vaultSle,
    Number const& principalOutstanding,
    Number const& interestOutstanding,
    std::uint32_t paymentInterval,
    beast::Journal j)
{
    // Update the Vault object(clear "paper loss")
    auto vaultLossUnrealizedProxy = vaultSle->at(sfLossUnrealized);
    auto const lossReversed = principalOutstanding + interestOutstanding;
    if (vaultLossUnrealizedProxy < lossReversed)
    {
        // LCOV_EXCL_START
        JLOG(j.warn())
            << "Vault unrealized loss is less than the amount to be cleared";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }
    vaultLossUnrealizedProxy -= lossReversed;
    view.update(vaultSle);

    // Update the Loan object
    loanSle->clearFlag(lsfLoanImpaired);
    auto const normalPaymentDueDate =
        std::max(loanSle->at(sfPreviousPaymentDate), loanSle->at(sfStartDate)) +
        paymentInterval;
    if (!hasExpired(view, normalPaymentDueDate))
    {
        // loan was unimpaired within the payment interval
        loanSle->at(sfNextPaymentDueDate) = normalPaymentDueDate;
    }
    else
    {
        // loan was unimpaired after the original payment due date
        loanSle->at(sfNextPaymentDueDate) =
            view.parentCloseTime().time_since_epoch().count() + paymentInterval;
    }
    view.update(loanSle);

    return tesSUCCESS;
}

TER
LoanManage::doApply()
{
    return temDISABLED;
#if LOANCOMPLETE
    auto const& tx = ctx_.tx;
    auto& view = ctx_.view();

    auto const loanID = tx[sfLoanID];
    auto const loanSle = view.peek(keylet::loan(loanID));
    if (!loanSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    auto const brokerID = loanSle->at(sfLoanBrokerID);
    auto const brokerSle = view.peek(keylet::loanbroker(brokerID));
    if (!brokerSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    auto const vaultSle = view.peek(keylet ::vault(brokerSle->at(sfVaultID)));
    if (!vaultSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    auto const vaultAsset = vaultSle->at(sfAsset);

    TenthBips32 const interestRate{loanSle->at(sfInterestRate)};
    std::int32_t const loanScale = loanSle->at(sfLoanScale);
    auto const principalOutstanding = loanSle->at(sfPrincipalOutstanding);

    TenthBips32 const managementFeeRate{brokerSle->at(sfManagementFeeRate)};
    auto const paymentInterval = loanSle->at(sfPaymentInterval);
    auto const paymentsRemaining = loanSle->at(sfPaymentRemaining);
    auto const interestOutstanding = loanInterestOutstandingMinusFee(
        vaultAsset,
        loanScale,
        principalOutstanding.value(),
        interestRate,
        paymentInterval,
        paymentsRemaining,
        managementFeeRate);

    // Valid flag combinations are checked in preflight. No flags is valid -
    // just a noop.
    if (tx.isFlag(tfLoanDefault))
    {
        if (auto const ter = defaultLoan(
                view,
                loanSle,
                brokerSle,
                vaultSle,
                principalOutstanding,
                interestOutstanding,
                vaultAsset,
                j_))
            return ter;
    }
    else if (tx.isFlag(tfLoanImpair))
    {
        if (auto const ter = impairLoan(
                view,
                loanSle,
                vaultSle,
                principalOutstanding,
                interestOutstanding,
                j_))
            return ter;
    }
    else if (tx.isFlag(tfLoanUnimpair))
    {
        if (auto const ter = unimpairLoan(
                view,
                loanSle,
                vaultSle,
                principalOutstanding,
                interestOutstanding,
                paymentInterval,
                j_))
            return ter;
    }

    return tesSUCCESS;
#endif
}

//------------------------------------------------------------------------------

}  // namespace ripple
