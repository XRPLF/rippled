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
#include <xrpld/app/tx/detail/LoanBrokerSet.h>
#include <xrpld/ledger/ApplyView.h>
#include <xrpld/ledger/View.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>

namespace ripple {

bool
LoanManage::isEnabled(PreflightContext const& ctx)
{
    return lendingProtocolEnabled(ctx);
}

std::uint32_t
LoanManage::getFlagsMask(PreflightContext const& ctx)
{
    return tfLoanManageMask;
}

NotTEC
LoanManage::doPreflight(PreflightContext const& ctx)
{
    // Is combining flags legal?
    int numFlags = 0;
    for (auto const flag : {
             tfLoanDefault,
             tfLoanImpair,
             tfLoanUnimpair,
         })
    {
        if (ctx.tx.isFlag(flag))
            ++numFlags;
    }
    if (numFlags > 1)
    {
        JLOG(ctx.j.warn())
            << "LoanManage: Only one of tfLoanDefault, tfLoanImpair, or "
               "tfLoanUnimpair can be set.";
        return temINVALID_FLAG;
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
        (tx.isFlag(tfLoanDefault) || tx.isFlag(tfLoanUnimpair)))
    {
        JLOG(ctx.j.warn())
            << "Loan is unimpaired. Only valid modification is to impair";
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
            << "Loan is not in default. A loan can not be defaulted before the "
               "next payment due date.";
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
defaultLoan(
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

    // The default Amount equals the outstanding principal and interest,
    // excluding any funds unclaimed by the Borrower.
    auto assetsAvailableProxy = loanSle->at(sfAssetsAvailable);
    auto defaultAmount =
        (principalOutstanding + interestOutstanding) - assetsAvailableProxy;
    // Apply the First-Loss Capital to the Default Amount
    auto debtTotalProxy = brokerSle->at(sfDebtTotal);
    TenthBips32 const coverRateMinimum{brokerSle->at(sfCoverRateMinimum)};
    TenthBips32 const coverRateLiquidation{
        brokerSle->at(sfCoverRateLiquidation)};
    auto const defaultCovered = std::min(
        tenthBipsOfValue(
            tenthBipsOfValue(debtTotalProxy.value(), coverRateMinimum),
            coverRateLiquidation),
        defaultAmount);
    defaultAmount -= defaultCovered;

    // Update the Vault object:

    // Decrease the Total Value of the Vault:
    vaultSle->at(sfAssetsTotal) -= defaultAmount;
    // Increase the Asset Available of the Vault by liquidated First-Loss
    // Capital and any unclaimed funds amount:
    vaultSle->at(sfAssetsAvailable) += defaultCovered + assetsAvailableProxy;
    view.update(vaultSle);

    // Update the LoanBroker object:

    // Decrease the Debt of the LoanBroker:
    debtTotalProxy -=
        principalOutstanding + interestOutstanding + assetsAvailableProxy;
    // Decrease the First-Loss Capital Cover Available:
    brokerSle->at(sfCoverAvailable) -= defaultCovered;
    view.update(brokerSle);

    // Update the Loan object:
    loanSle->setFlag(lsfLoanDefault);
    loanSle->clearFlag(lsfLoanImpaired);
    loanSle->at(sfPaymentRemaining) = 0;
    assetsAvailableProxy = 0;
    loanSle->at(sfPrincipalOutstanding) = 0;
    view.update(loanSle);

    // Move the First-Loss Capital from the LoanBroker pseudo-account to the
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
impairLoan(
    ApplyView& view,
    SLE::ref loanSle,
    SLE::ref brokerSle,
    SLE::ref vaultSle,
    Number const& principalOutstanding,
    Number const& interestOutstanding,
    Asset const& vaultAsset,
    beast::Journal j)
{
    // Update the Vault object(set "paper loss")
    vaultSle->at(sfLossUnrealized) +=
        principalOutstanding + interestOutstanding;
    view.update(vaultSle);

    // Update the Loan object
    loanSle->setFlag(lsfLoanImpaired);
    auto nextDueProxy = loanSle->at(sfNextPaymentDueDate);
    if (!hasExpired(view, nextDueProxy))
    {
        // loan payment is not yet late -
        // move the next payment due date to now
        nextDueProxy = view.parentCloseTime().time_since_epoch().count();
    }
    view.update(loanSle);

    return tesSUCCESS;
}

TER
unimpairLoan(
    ApplyView& view,
    SLE::ref loanSle,
    SLE::ref brokerSle,
    SLE::ref vaultSle,
    Number const& principalOutstanding,
    Number const& interestOutstanding,
    Asset const& vaultAsset,
    beast::Journal j)
{
    // Update the Vault object(clear "paper loss")
    vaultSle->at(sfLossUnrealized) -=
        principalOutstanding + interestOutstanding;
    view.update(vaultSle);

    // Update the Loan object
    loanSle->clearFlag(lsfLoanImpaired);
    auto const paymentInterval = loanSle->at(sfPaymentInterval);
    auto const normalPaymentDueDate =
        loanSle->at(sfPreviousPaymentDate) + paymentInterval;
    if (!hasExpired(view, normalPaymentDueDate))
    {
        // loan was unimpaired within the payment interval
        loanSle->at(sfNextPaymentDueDate) = normalPaymentDueDate;
    }
    else
    {
        // loan was unimpaired after the original payment due date
        loanSle->at(sfNextPaymentDueDate) =
            view.parentCloseTime().time_since_epoch().count() +
            loanSle->at(sfPaymentInterval);
    }
    view.update(loanSle);

    return tesSUCCESS;
}

TER
LoanManage::doApply()
{
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
    auto const principalOutstanding = loanSle->at(sfPrincipalOutstanding);
    auto const interestOutstanding =
        tenthBipsOfValue(principalOutstanding.value(), interestRate);

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
    if (tx.isFlag(tfLoanImpair))
    {
        if (auto const ter = impairLoan(
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
    if (tx.isFlag(tfLoanUnimpair))
    {
        if (auto const ter = unimpairLoan(
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

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

}  // namespace ripple
