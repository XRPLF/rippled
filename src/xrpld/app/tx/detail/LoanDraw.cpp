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

#include <xrpld/app/tx/detail/LoanDraw.h>
//
#include <xrpld/app/misc/LendingHelpers.h>
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
LoanDraw::isEnabled(PreflightContext const& ctx)
{
    return lendingProtocolEnabled(ctx);
}

NotTEC
LoanDraw::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfLoanID] == beast::zero)
        return temINVALID;

    if (ctx.tx[sfAmount] <= beast::zero)
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

TER
LoanDraw::preclaim(PreclaimContext const& ctx)
{
    auto const& tx = ctx.tx;

    auto const account = tx[sfAccount];
    auto const loanID = tx[sfLoanID];
    auto const amount = tx[sfAmount];

    auto const loanSle = ctx.view.read(keylet::loan(loanID));
    if (!loanSle)
    {
        JLOG(ctx.j.warn()) << "Loan does not exist.";
        return tecNO_ENTRY;
    }

    if (loanSle->at(sfBorrower) != account)
    {
        JLOG(ctx.j.warn()) << "Loan does not belong to the account.";
        return tecNO_PERMISSION;
    }

    if (loanSle->isFlag(lsfLoanImpaired) || loanSle->isFlag(lsfLoanDefault))
    {
        JLOG(ctx.j.warn()) << "Loan is impaired or in default.";
        return tecNO_PERMISSION;
    }

    if (!hasExpired(ctx.view, loanSle->at(sfStartDate)))
    {
        JLOG(ctx.j.warn()) << "Loan has not started yet.";
        return tecTOO_SOON;
    }

    auto const loanBrokerID = loanSle->at(sfLoanBrokerID);
    auto const loanBrokerSle = ctx.view.read(keylet::loanbroker(loanBrokerID));
    if (!loanBrokerSle)
    {
        // This should be impossible
        // LCOV_EXCL_START
        JLOG(ctx.j.fatal()) << "LoanBroker does not exist.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }
    auto const brokerPseudoAccount = loanBrokerSle->at(sfAccount);
    auto const vaultID = loanBrokerSle->at(sfVaultID);
    auto const vaultSle = ctx.view.read(keylet::vault(vaultID));
    if (!vaultSle)
    {
        // This should be impossible
        // LCOV_EXCL_START
        JLOG(ctx.j.fatal()) << "Vault does not exist.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }
    auto const asset = vaultSle->at(sfAsset);

    if (amount.asset() != asset)
    {
        JLOG(ctx.j.warn()) << "Loan amount does not match the Vault asset.";
        return tecWRONG_ASSET;
    }

    if (loanSle->at(sfAssetsAvailable) < amount)
    {
        JLOG(ctx.j.warn()) << "Loan does not have enough assets available.";
        return tecINSUFFICIENT_FUNDS;
    }

    if (auto const ret = checkFrozen(ctx.view, brokerPseudoAccount, asset))
    {
        JLOG(ctx.j.warn()) << "Loan Broker pseudo-account is frozen.";
        return ret;
    }
    if (auto const ret = checkDeepFrozen(ctx.view, account, asset))
    {
        JLOG(ctx.j.warn())
            << "Borrower account cannot receive funds (deep frozen).";
        return ret;
    }

    if (hasExpired(ctx.view, loanSle->at(sfNextPaymentDueDate)))
    {
        JLOG(ctx.j.warn()) << "Loan payment is overdue.";
        return tecNO_PERMISSION;
    }

    return tesSUCCESS;
}

TER
LoanDraw::doApply()
{
    auto const& tx = ctx_.tx;
    auto& view = ctx_.view();

    auto const amount = tx[sfAmount];

    auto const loanID = tx[sfLoanID];
    auto const loanSle = view.peek(keylet::loan(loanID));
    if (!loanSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    auto const brokerID = loanSle->at(sfLoanBrokerID);
    auto const brokerSle = view.peek(keylet::loanbroker(brokerID));
    if (!brokerSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    auto const brokerPseudoAccount = brokerSle->at(sfAccount);

    if (auto const ter = accountSend(
            view,
            brokerPseudoAccount,
            account_,
            amount,
            j_,
            WaiveTransferFee::Yes))
        return ter;

    loanSle->at(sfAssetsAvailable) -= amount;
    view.update(loanSle);

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

}  // namespace ripple
