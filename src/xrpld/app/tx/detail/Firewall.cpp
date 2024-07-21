//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <xrpld/app/tx/detail/Firewall.h>
#include <xrpld/ledger/Sandbox.h>
#include <xrpld/ledger/View.h>
#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Firewall.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/st.h>

namespace ripple {

NotTEC
FirewallSet::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureFirewall))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    // auto const amount = ctx.tx[~sfAmount];

    // if (amount.issue() == amount2.issue())
    // {
    //     JLOG(ctx.j.debug())
    //         << "Firewall: tokens can not have the same currency/issuer.";
    //     return temBAD_AMM_TOKENS;
    // }

    // if (auto const err = invalidAmount(amount))
    // {
    //     JLOG(ctx.j.debug()) << "Firewall: invalid asset1 amount.";
    //     return err;
    // }

    // Validate Authorize
    if (ctx.tx.isFieldPresent(sfAuthorize))
    {
        auto const authorizeID = ctx.tx.getAccountID(sfAuthorize);
        // Make sure that the passed account is valid.
        if (authorizeID == beast::zero)
        {
            JLOG(ctx.j.debug())
                << "Malformed transaction: Authorized or Unauthorized "
                   "field zeroed.";
            return temINVALID_ACCOUNT_ID;
        }

        // An account may not preauthorize itself.
        if (authorizeID == ctx.tx[sfAccount])
        {
            JLOG(ctx.j.debug())
                << "Malformed transaction: Attempting to FirewallPreauth self.";
            return temCANNOT_PREAUTH_SELF;
        }
    }

    return preflight2(ctx);
}

TER
FirewallSet::preclaim(PreclaimContext const& ctx)
{
    AccountID const accountID = ctx.tx[sfAccount];
    auto const amount = ctx.tx[~sfAmount];

    ripple::Keylet const firewallKeylet = keylet::firewall(accountID);
    auto const sleFirewall = ctx.view.read(firewallKeylet);

    if (!sleFirewall && ctx.tx.isFieldPresent(sfSignature))
    {
        JLOG(ctx.j.debug()) << "Firewall: Set must not contain a sfSignature";
        return temMALFORMED;
    }

    if (sleFirewall && !ctx.tx.isFieldPresent(sfSignature))
    {
        JLOG(ctx.j.debug()) << "Firewall: Update must contain a sfSignature";
        return temMALFORMED;
    }

    if (sleFirewall && ctx.tx.isFieldPresent(sfAuthorize))
    {
        JLOG(ctx.j.debug()) << "Firewall: Update cannot contain a sfAuthorize";
        return temMALFORMED;
    }

    if (sleFirewall && ctx.tx.isFieldPresent(sfPublicKey) &&
        ctx.tx.isFieldPresent(sfAmount))
    {
        JLOG(ctx.j.debug())
            << "Firewall: Update cannot contain both sfPublicKey & sfAmount";
        return temMALFORMED;
    }

    // if (auto const ter = requireAuth(ctx.view, amount.issue(), accountID);
    //     ter != tesSUCCESS)
    // {
    // JLOG(ctx.j.debug())
    //     << "Firewall: account is not authorized, " << amount.issue();
    //     return ter;
    // }

    // // Globally or individually frozen
    // if (isFrozen(ctx.view, accountID, amount.issue()) ||
    //     isFrozen(ctx.view, accountID, amount2.issue()))
    // {
    //     JLOG(ctx.j.debug()) << "Firewall: involves frozen asset.";
    //     return tecFROZEN;
    // }

    // auto noDefaultRipple = [](ReadView const& view, Issue const& issue) {
    //     if (isXRP(issue))
    //         return false;

    //     if (auto const issuerAccount =
    //             view.read(keylet::account(issue.account)))
    //         return (issuerAccount->getFlags() & lsfDefaultRipple) == 0;

    //     return false;
    // };

    // if (noDefaultRipple(ctx.view, amount.issue()))
    // {
    //     JLOG(ctx.j.debug()) << "Firewall: DefaultRipple not set";
    //     return terNO_RIPPLE;
    // }

    if (ctx.tx.isFieldPresent(sfSignature) &&
        ctx.tx.isFieldPresent(sfPublicKey))
    {
        PublicKey const txPK(makeSlice(ctx.tx.getFieldVL(sfPublicKey)));
        auto const sig = ctx.tx.getFieldVL(sfSignature);
        PublicKey const fPK(makeSlice(sleFirewall->getFieldVL(sfPublicKey)));
        Serializer msg;
        serializeFirewallAuthorization(msg, accountID, txPK);
        if (!verify(fPK, msg.slice(), makeSlice(sig), /*canonical*/ true))
        {
            JLOG(ctx.j.debug())
                << "Firewall: Bad Signature for update sfPublicKey";
            return temBAD_SIGNATURE;
        }
    }

    if (ctx.tx.isFieldPresent(sfSignature) && ctx.tx.isFieldPresent(sfAmount))
    {
        auto const amount = ctx.tx.getFieldAmount(sfAmount);
        auto const sig = ctx.tx.getFieldVL(sfSignature);
        PublicKey const pk(makeSlice(sleFirewall->getFieldVL(sfPublicKey)));
        Serializer msg;
        serializeFirewallAuthorization(msg, accountID, amount);
        if (!verify(pk, msg.slice(), makeSlice(sig), /*canonical*/ true))
        {
            JLOG(ctx.j.debug())
                << "Firewall: Bad Signature for update sfAmount";
            return temBAD_SIGNATURE;
        }
    }

    return tesSUCCESS;
}

TER
FirewallSet::doApply()
{
    Sandbox sb(&ctx_.view());

    auto const sleOwner = sb.peek(keylet::account(account_));
    if (!sleOwner)
        return tefINTERNAL;

    ripple::Keylet const firewallKeylet = keylet::firewall(account_);
    auto sleFirewall = sb.peek(firewallKeylet);
    if (!sleFirewall)
    {
        // Set Firewall
        auto const sleFirewall = std::make_shared<SLE>(firewallKeylet);
        (*sleFirewall)[sfOwner] = account_;
        (*sleFirewall)[~sfPublicKey] = ctx_.tx[~sfPublicKey];
        (*sleFirewall)[~sfAmount] = ctx_.tx[~sfAmount];

        // Add owner directory to link the account and Firewall object.
        if (auto const page = sb.dirInsert(
                keylet::ownerDir(account_),
                sleFirewall->key(),
                describeOwnerDir(account_)))
        {
            sleFirewall->setFieldU64(sfOwnerNode, *page);
        }
        else
        {
            JLOG(j_.debug()) << "Firewall: failed to insert owner dir";
            return tecDIR_FULL;
        }
        sb.insert(sleFirewall);
        adjustOwnerCount(sb, sleOwner, 1, j_);

        // Add Preauth
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
        Keylet const preauthKeylet = keylet::firewallPreauth(account_, auth);
        auto slePreauth = std::make_shared<SLE>(preauthKeylet);

        slePreauth->setAccountID(sfAccount, account_);
        slePreauth->setAccountID(sfAuthorize, auth);

        // Add owner directory to link the account and Firewall object.
        if (auto const page = sb.dirInsert(
                keylet::ownerDir(account_),
                slePreauth->key(),
                describeOwnerDir(account_)))
        {
            sleFirewall->setFieldU64(sfOwnerNode, *page);
        }
        else
        {
            JLOG(j_.debug()) << "Firewall: failed to insert owner dir";
            return tecDIR_FULL;
        }
        sb.insert(slePreauth);
        adjustOwnerCount(sb, sleOwner, 1, j_);
    }
    else
    {
        // Update Firewall
        if (ctx_.tx.isFieldPresent(sfPublicKey))
            sleFirewall->setFieldVL(
                sfPublicKey, ctx_.tx.getFieldVL(sfPublicKey));
        if (ctx_.tx.isFieldPresent(sfAmount))
            sleFirewall->setFieldAmount(
                sfAmount, ctx_.tx.getFieldAmount(sfAmount));

        sb.update(sleFirewall);
    }

    sb.apply(ctx_.rawView());
    return tesSUCCESS;
}

}  // namespace ripple
