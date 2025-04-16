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

#include <xrpld/app/tx/detail/LoanBrokerCoverWithdraw.h>
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
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>

namespace ripple {

bool
LoanBrokerCoverWithdraw::isEnabled(PreflightContext const& ctx)
{
    return lendingProtocolEnabled(ctx);
}

std::uint32_t
LoanBrokerCoverWithdraw::getFlagsMask(PreflightContext const& ctx)
{
    return tfUniversalMask;
}

NotTEC
LoanBrokerCoverWithdraw::doPreflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfLoanBrokerID] == beast::zero)
        return temINVALID;

    if (ctx.tx[sfAmount] <= beast::zero)
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

TER
LoanBrokerCoverWithdraw::preclaim(PreclaimContext const& ctx)
{
    auto const& tx = ctx.tx;

    auto const account = tx[sfAccount];
    auto const brokerID = tx[sfLoanBrokerID];
    auto const amount = tx[sfAmount];

    auto const sleBroker = ctx.view.read(keylet::loanbroker(brokerID));
    if (!sleBroker)
    {
        JLOG(ctx.j.warn()) << "LoanBroker does not exist.";
        return tecNO_ENTRY;
    }
    if (account != sleBroker->at(sfOwner))
    {
        JLOG(ctx.j.warn()) << "Account is not the owner of the LoanBroker.";
        return tecNO_PERMISSION;
    }
    auto const vault = ctx.view.read(keylet::vault(sleBroker->at(sfVaultID)));
    auto const vaultAsset = vault->at(sfAsset);

    if (amount.asset() != vaultAsset)
        return tecWRONG_ASSET;

    // Cannot transfer a frozen Asset
    auto const pseudoAccountID = sleBroker->at(sfAccount);

    // Cannot transfer a frozen Asset
    /*
    if (isFrozen(ctx.view, account, vaultAsset))
        return vaultAsset.holds<Issue>() ? tecFROZEN : tecLOCKED;
    */
    if (isFrozen(ctx.view, pseudoAccountID, vaultAsset))
        return vaultAsset.holds<Issue>() ? tecFROZEN : tecLOCKED;
    if (vaultAsset.holds<Issue>())
    {
        auto const issue = vaultAsset.get<Issue>();
        if (isDeepFrozen(ctx.view, account, issue.currency, issue.account))
            return tecFROZEN;
    }

    auto const coverAvail = sleBroker->at(sfCoverAvailable);
    // Cover Rate is in 1/10 bps units
    auto const minimumCover = sleBroker->at(sfDebtTotal) *
        sleBroker->at(sfCoverRateMinimum) / bpsPerOne / 10;
    if (coverAvail < amount)
        return tecINSUFFICIENT_FUNDS;
    if ((coverAvail - amount) < minimumCover)
        return tecINSUFFICIENT_FUNDS;

    if (accountHolds(
            ctx.view,
            account,
            vaultAsset,
            FreezeHandling::fhZERO_IF_FROZEN,
            AuthHandling::ahZERO_IF_UNAUTHORIZED,
            ctx.j) < amount)
        return tecINSUFFICIENT_FUNDS;

    return tesSUCCESS;
}

TER
LoanBrokerCoverWithdraw::doApply()
{
    auto const& tx = ctx_.tx;

    auto const brokerID = tx[sfLoanBrokerID];
    auto const amount = tx[sfAmount];

    auto broker = view().peek(keylet::loanbroker(brokerID));

    auto const brokerPseudoID = broker->at(sfAccount);

    // Transfer assets from pseudo-account to depositor.
    if (auto ter = accountSend(
            view(),
            brokerPseudoID,
            account_,
            amount,
            j_,
            WaiveTransferFee::Yes))
        return ter;

    // Increase the LoanBroker's CoverAvailable by Amount
    broker->at(sfCoverAvailable) -= amount;
    view().update(broker);

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

}  // namespace ripple
