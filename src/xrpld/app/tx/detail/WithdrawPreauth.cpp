//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Transia, LLC.

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

#include <xrpld/app/tx/detail/WithdrawPreauth.h>
#include <xrpld/ledger/View.h>
#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/st.h>

namespace ripple {

NotTEC
WithdrawPreauth::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureFirewall))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    auto& tx = ctx.tx;
    auto& j = ctx.j;

    if (tx.getFlags() & tfUniversalMask)
    {
        JLOG(j.trace()) << "Malformed transaction: Invalid flags set.";
        return temINVALID_FLAG;
    }

    auto const optAuth = ctx.tx[~sfAuthorize];
    auto const optUnauth = ctx.tx[~sfUnauthorize];
    if (static_cast<bool>(optAuth) == static_cast<bool>(optUnauth))
    {
        // Either both fields are present or neither field is present.  In
        // either case the transaction is malformed.
        JLOG(j.trace())
            << "Malformed transaction: "
               "Invalid Authorize and Unauthorize field combination.";
        return temMALFORMED;
    }

    // Make sure that the passed account is valid.
    AccountID const target{optAuth ? *optAuth : *optUnauth};
    if (target == beast::zero)
    {
        JLOG(j.trace()) << "Malformed transaction: Authorized or Unauthorized "
                           "field zeroed.";
        return temINVALID_ACCOUNT_ID;
    }

    // An account may not preauthorize itself.
    if (optAuth && (target == ctx.tx[sfAccount]))
    {
        JLOG(j.trace())
            << "Malformed transaction: Attempting to WithdrawPreauth self.";
        return temCANNOT_PREAUTH_SELF;
    }

    return preflight2(ctx);
}

TER
WithdrawPreauth::preclaim(PreclaimContext const& ctx)
{
    Serializer msg;
    AccountID const accountID = ctx.tx[sfAccount];

    // // Determine which operation we're performing: authorizing or unauthorizing.
    // if (ctx.tx.isFieldPresent(sfAuthorize))
    // {
    //     // Verify that the Authorize account is present in the ledger.
    //     AccountID const auth{ctx.tx[sfAuthorize]};
    //     if (!ctx.view.exists(keylet::account(auth)))
    //         return tecNO_TARGET;

    //     // Verify that the Preauth entry they asked to add is not already
    //     // in the ledger.
    //     if (ctx.view.exists(keylet::withdrawPreauth(ctx.tx[sfAccount], auth)))
    //         return tecDUPLICATE;
        
    //     serializeFirewallAuthorization(msg, accountID, auth);
    // }
    // else
    // {
    //     // Verify that the Preauth entry they asked to remove is in the ledger.
    //     AccountID const unauth{ctx.tx[sfUnauthorize]};
    //     if (!ctx.view.exists(
    //             keylet::withdrawPreauth(ctx.tx[sfAccount], unauth)))
    //         return tecNO_ENTRY;
        
    //     serializeFirewallAuthorization(msg, accountID, unauth);
    // }
    
    // Validate Signature
    ripple::Keylet const firewallKeylet = keylet::firewall(accountID);
    auto const sleFirewall = ctx.view.read(firewallKeylet);
    if (!sleFirewall)
    {
        JLOG(ctx.j.debug()) << "WithdrawPreauth: Firewall does not exist.";
        return tecNO_TARGET;
    }
    if (!sleFirewall->isFieldPresent(sfIssuer))
    {
        JLOG(ctx.j.debug()) << "WithdrawPreauth: Missing Firewall Issuer.";
        return tecINTERNAL;
    }
    auto const sig = ctx.tx.getFieldVL(sfSignature);
    PublicKey const pk(makeSlice(ctx.tx.getFieldVL(sfPublicKey)));
    // TODO: Valid PK (AccountID) == sfIssuer
    if (!verify(pk, msg.slice(), makeSlice(sig), /*canonical*/ true))
    {
        JLOG(ctx.j.debug())
            << "WithdrawPreauth: Bad Signature for update.";
        return temBAD_SIGNATURE;
    }
    return tesSUCCESS;
}

TER
WithdrawPreauth::doApply()
{
    if (ctx_.tx.isFieldPresent(sfAuthorize))
    {
        auto const sleOwner = view().peek(keylet::account(account_));
        if (!sleOwner)
            return {tefINTERNAL};

        // A preauth counts against the reserve of the issuing account, but we
        // check the starting balance because we want to allow dipping into the
        // reserve to pay fees.
        {
            STAmount const reserve{view().fees().accountReserve(
                sleOwner->getFieldU32(sfOwnerCount) + 1)};

            if (mPriorBalance < reserve)
                return tecINSUFFICIENT_RESERVE;
        }

        // Preclaim already verified that the Preauth entry does not yet exist.
        // Create and populate the Preauth entry.
        AccountID const auth{ctx_.tx[sfAuthorize]};
        Keylet const preauthKeylet = keylet::withdrawPreauth(account_, auth);
        auto slePreauth = std::make_shared<SLE>(preauthKeylet);

        slePreauth->setAccountID(sfAccount, account_);
        slePreauth->setAccountID(sfAuthorize, auth);
        view().insert(slePreauth);

        auto viewJ = ctx_.app.journal("View");
        auto const page = view().dirInsert(
            keylet::ownerDir(account_),
            preauthKeylet,
            describeOwnerDir(account_));

        JLOG(j_.trace()) << "Adding WithdrawPreauth to owner directory "
                         << to_string(preauthKeylet.key) << ": "
                         << (page ? "success" : "failure");

        if (!page)
            return tecDIR_FULL;

        slePreauth->setFieldU64(sfOwnerNode, *page);

        // If we succeeded, the new entry counts against the creator's reserve.
        adjustOwnerCount(view(), sleOwner, 1, viewJ);
    }
    else
    {
        auto const preauth =
            keylet::withdrawPreauth(account_, ctx_.tx[sfUnauthorize]);

        return WithdrawPreauth::removeFromLedger(
            ctx_.app, view(), preauth.key, j_);
    }
    return tesSUCCESS;
}

TER
WithdrawPreauth::removeFromLedger(
    Application& app,
    ApplyView& view,
    uint256 const& preauthIndex,
    beast::Journal j)
{
    // Verify that the Preauth entry they asked to remove is
    // in the ledger.
    std::shared_ptr<SLE> const slePreauth{
        view.peek(keylet::withdrawPreauth(preauthIndex))};
    if (!slePreauth)
    {
        JLOG(j.warn()) << "Selected WithdrawPreauth does not exist.";
        return tecNO_ENTRY;
    }

    AccountID const account{(*slePreauth)[sfAccount]};
    std::uint64_t const page{(*slePreauth)[sfOwnerNode]};
    if (!view.dirRemove(keylet::ownerDir(account), page, preauthIndex, false))
    {
        JLOG(j.fatal()) << "Unable to delete WithdrawPreauth from owner.";
        return tefBAD_LEDGER;
    }

    // If we succeeded, update the WithdrawPreauth owner's reserve.
    auto const sleOwner = view.peek(keylet::account(account));
    if (!sleOwner)
        return tefINTERNAL;

    adjustOwnerCount(view, sleOwner, -1, app.journal("View"));

    // Remove WithdrawPreauth from ledger.
    view.erase(slePreauth);

    return tesSUCCESS;
}

}  // namespace ripple
