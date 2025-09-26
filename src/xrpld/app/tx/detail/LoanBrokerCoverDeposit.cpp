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

#include <xrpld/app/tx/detail/LoanBrokerCoverDeposit.h>
//
#include <xrpld/app/misc/LendingHelpers.h>

namespace ripple {

bool
LoanBrokerCoverDeposit::checkExtraFeatures(PreflightContext const& ctx)
{
    return checkLendingProtocolDependencies(ctx);
}

NotTEC
LoanBrokerCoverDeposit::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfLoanBrokerID] == beast::zero)
        return temINVALID;

    auto const dstAmount = ctx.tx[sfAmount];
    if (dstAmount <= beast::zero)
        return temBAD_AMOUNT;

    if (!isLegalNet(dstAmount))
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

TER
LoanBrokerCoverDeposit::preclaim(PreclaimContext const& ctx)
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

    auto const pseudoAccountID = sleBroker->at(sfAccount);

    // Cannot transfer a frozen Asset
    if (auto const ret = checkFrozen(ctx.view, account, vaultAsset))
        return ret;
    // Pseudo-account cannot receive if asset is deep frozen
    if (auto const ret = checkDeepFrozen(ctx.view, pseudoAccountID, vaultAsset))
        return ret;

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
LoanBrokerCoverDeposit::doApply()
{
    auto const& tx = ctx_.tx;

    auto const brokerID = tx[sfLoanBrokerID];
    auto const amount = tx[sfAmount];

    auto broker = view().peek(keylet::loanbroker(brokerID));
    if (!broker)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const brokerPseudoID = broker->at(sfAccount);

    // Transfer assets from depositor to pseudo-account.
    if (auto ter = accountSend(
            view(),
            account_,
            brokerPseudoID,
            amount,
            j_,
            WaiveTransferFee::Yes))
        return ter;

    // Increase the LoanBroker's CoverAvailable by Amount
    broker->at(sfCoverAvailable) += amount;
    view().update(broker);

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

}  // namespace ripple
