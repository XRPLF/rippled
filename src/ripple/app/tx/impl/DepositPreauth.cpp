//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

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

#include <ripple/app/tx/impl/DepositPreauth.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/st.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/ledger/View.h>

namespace ripple {

NotTEC
DepositPreauth::preflight (PreflightContext const& ctx)
{
    if (! ctx.rules.enabled (featureDepositPreauth))
        return temDISABLED;

    auto const ret = preflight1 (ctx);
    if (!isTesSuccess (ret))
        return ret;

    auto& tx = ctx.tx;
    auto& j = ctx.j;

    if (tx.getFlags() & tfUniversalMask)
    {
        JLOG(j.trace()) <<
            "Malformed transaction: Invalid flags set.";
        return temINVALID_FLAG;
    }

    auto const optAuth = ctx.tx[~sfAuthorize];
    auto const optUnauth = ctx.tx[~sfUnauthorize];
    if (static_cast<bool>(optAuth) == static_cast<bool>(optUnauth))
    {
        // Either both fields are present or neither field is present.  In
        // either case the transaction is malformed.
        JLOG(j.trace()) <<
            "Malformed transaction: "
            "Invalid Authorize and Unauthorize field combination.";
        return temMALFORMED;
    }

    // Make sure that the passed account is valid.
    AccountID const target {optAuth ? *optAuth : *optUnauth};
    if (target == beast::zero)
    {
        JLOG(j.trace()) <<
            "Malformed transaction: Authorized or Unauthorized field zeroed.";
        return temINVALID_ACCOUNT_ID;
    }

    // An account may not preauthorize itself.
    if (optAuth && (target == ctx.tx[sfAccount]))
    {
        JLOG(j.trace()) <<
            "Malformed transaction: Attempting to DepositPreauth self.";
        return temCANNOT_PREAUTH_SELF;
    }

    return preflight2 (ctx);
}

TER
DepositPreauth::preclaim(PreclaimContext const& ctx)
{
    // Determine which operation we're performing: authorizing or unauthorizing.
    if (ctx.tx.isFieldPresent (sfAuthorize))
    {
        // Verify that the Authorize account is present in the ledger.
        AccountID const auth {ctx.tx[sfAuthorize]};
        if (! ctx.view.exists (keylet::account (auth)))
            return tecNO_TARGET;

        // Verify that the Preauth entry they asked to add is not already
        // in the ledger.
        if (ctx.view.exists (
            keylet::depositPreauth (ctx.tx[sfAccount], auth)))
            return tecDUPLICATE;
    }
    else
    {
        // Verify that the Preauth entry they asked to remove is in the ledger.
        AccountID const unauth {ctx.tx[sfUnauthorize]};
        if (! ctx.view.exists (
            keylet::depositPreauth (ctx.tx[sfAccount], unauth)))
            return tecNO_ENTRY;
    }
    return tesSUCCESS;
}

TER
DepositPreauth::doApply ()
{
    if (ctx_.tx.isFieldPresent (sfAuthorize))
    {
        auto const sleOwner = view().peek (keylet::account (account_));
        if (! sleOwner)
            return {tefINTERNAL};

        // A preauth counts against the reserve of the issuing account, but we
        // check the starting balance because we want to allow dipping into the
        // reserve to pay fees.
        {
            STAmount const reserve {view().fees().accountReserve (
                sleOwner->getFieldU32 (sfOwnerCount) + 1)};

            if (mPriorBalance < reserve)
                return tecINSUFFICIENT_RESERVE;
        }

        // Preclaim already verified that the Preauth entry does not yet exist.
        // Create and populate the Preauth entry.
        AccountID const auth {ctx_.tx[sfAuthorize]};
        auto slePreauth =
            std::make_shared<SLE>(keylet::depositPreauth (account_, auth));

        slePreauth->setAccountID (sfAccount, account_);
        slePreauth->setAccountID (sfAuthorize, auth);
        view().insert (slePreauth);

        auto viewJ = ctx_.app.journal ("View");
        auto const page = view().dirInsert (keylet::ownerDir (account_),
            slePreauth->key(), describeOwnerDir (account_));

        JLOG(j_.trace())
            << "Adding DepositPreauth to owner directory "
            << to_string (slePreauth->key())
            << ": " << (page ? "success" : "failure");

        if (! page)
            return tecDIR_FULL;

        slePreauth->setFieldU64 (sfOwnerNode, *page);

        // If we succeeded, the new entry counts against the creator's reserve.
        adjustOwnerCount (view(), sleOwner, 1, viewJ);
    }
    else
    {
        AccountID const unauth {ctx_.tx[sfUnauthorize]};
        uint256 const preauthIndex {getDepositPreauthIndex (account_, unauth)};

        return DepositPreauth::removeFromLedger (
            ctx_.app, view(), preauthIndex, j_);
    }
    return tesSUCCESS;
}

TER
DepositPreauth::removeFromLedger (Application& app,
    ApplyView& view, uint256 const& preauthIndex, beast::Journal j)
{
    // Verify that the Preauth entry they asked to remove is
    // in the ledger.
    std::shared_ptr<SLE> const slePreauth {
        view.peek (keylet::depositPreauth (preauthIndex))};
    if (! slePreauth)
    {
        JLOG(j.warn()) << "Selected DepositPreauth does not exist.";
        return tecNO_ENTRY;
    }

    AccountID const account {(*slePreauth)[sfAccount]};
    std::uint64_t const page {(*slePreauth)[sfOwnerNode]};
    if (! view.dirRemove (
        keylet::ownerDir (account), page, preauthIndex, false))
    {
        JLOG(j.fatal()) << "Unable to delete DepositPreauth from owner.";
        return tefBAD_LEDGER;
    }

    // If we succeeded, update the DepositPreauth owner's reserve.
    auto const sleOwner = view.peek (keylet::account (account));
    if (! sleOwner)
        return tefINTERNAL;

    adjustOwnerCount (view, sleOwner, -1, app.journal ("View"));

    // Remove DepositPreauth from ledger.
    view.erase (slePreauth);

    return tesSUCCESS;
}

} // namespace ripple
