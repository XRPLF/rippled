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

#include <xrpld/app/tx/detail/LoanBrokerDelete.h>
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
LoanBrokerDelete::isEnabled(PreflightContext const& ctx)
{
    return LendingProtocolEnabled(ctx);
}

NotTEC
LoanBrokerDelete::doPreflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
LoanBrokerDelete::preclaim(PreclaimContext const& ctx)
{
    auto const& tx = ctx.tx;

    auto const account = tx[sfAccount];
    auto const brokerID = tx[sfLoanBrokerID];

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
    if (auto const ownerCount = sleBroker->at(sfOwnerCount); ownerCount != 0)
    {
        JLOG(ctx.j.warn()) << "LoanBrokerDelete: Owner count is " << ownerCount;
        return tecHAS_OBLIGATIONS;
    }

    return tesSUCCESS;
}

TER
LoanBrokerDelete::doApply()
{
    auto const& tx = ctx_.tx;

    auto const brokerID = tx[sfLoanBrokerID];

    // Delete the loan broker
    auto broker = view().peek(keylet::loanbroker(brokerID));
    if (!broker)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    auto const vaultID = broker->at(sfVaultID);
    auto const sleVault = view().read(keylet::vault(vaultID));
    if (!sleVault)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    auto const vaultPseudoID = sleVault->at(sfAccount);
    auto const vaultAsset = sleVault->at(sfAsset);

    auto const brokerPseudoID = broker->at(sfAccount);

    if (!view().dirRemove(
            keylet::ownerDir(account_),
            broker->at(sfOwnerNode),
            broker->key(),
            false))
    {
        return tefBAD_LEDGER;
    }
    if (!view().dirRemove(
            keylet::ownerDir(vaultPseudoID),
            broker->at(sfVaultNode),
            broker->key(),
            false))
    {
        return tefBAD_LEDGER;
    }

    {
        auto const coverAvailable =
            STAmount{vaultAsset, broker->at(sfCoverAvailable)};
        if (auto const ter = accountSend(
                view(),
                brokerPseudoID,
                account_,
                coverAvailable,
                j_,
                WaiveTransferFee::Yes))
            return ter;
    }

    if (auto ter = removeEmptyHolding(view(), brokerPseudoID, vaultAsset, j_))
        return ter;

    auto brokerPseudoSLE = view().peek(keylet::account(brokerPseudoID));
    if (!brokerPseudoSLE)
        return tefBAD_LEDGER;

    // Making the payment and removing the empty holding should have deleted any
    // obligations associated with the broker or broker pseudo-account.
    if (*brokerPseudoSLE->at(sfBalance))
    {
        JLOG(j_.warn()) << "LoanBrokerDelete: Pseudo-account has a balance";
        return tecHAS_OBLIGATIONS;
    }
    if (brokerPseudoSLE->at(sfOwnerCount) != 0)
    {
        JLOG(j_.warn())
            << "LoanBrokerDelete: Pseudo-account still owns objects";
        return tecHAS_OBLIGATIONS;
    }
    if (auto const directory = keylet::ownerDir(brokerPseudoID);
        view().read(directory))
    {
        JLOG(j_.warn()) << "LoanBrokerDelete: Pseudo-account has a directory";
        return tecHAS_OBLIGATIONS;
    }

    view().erase(brokerPseudoSLE);

    view().erase(broker);

    {
        auto owner = view().peek(keylet::account(account_));
        if (!owner)
            return tefBAD_LEDGER;  // LCOV_EXCL_LINE

        adjustOwnerCount(view(), owner, -2, j_);
    }

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

}  // namespace ripple
