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

#include <xrpld/app/tx/detail/SetFirewall.h>
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
SetFirewall::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureFirewall))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    auto const amount = ctx.tx[~sfAmount];
    auto const amount2 = ctx.tx[~sfAmount2];

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

    // if (auto const err = invalidAmount(amount2))
    // {
    //     JLOG(ctx.j.debug()) << "Firewall: invalid asset2 amount.";
    //     return err;
    // }

    // Validate AuthAccounts Max 8
    if (ctx.tx.isFieldPresent(sfAuthAccounts))
    {
        if (auto const authAccounts = ctx.tx.getFieldArray(sfAuthAccounts);
            authAccounts.size() > 8)
        {
            JLOG(ctx.j.debug()) << "Firewall: Invalid number of AuthAccounts.";
            return temMALFORMED;
        }
    }

    return preflight2(ctx);
}

TER
SetFirewall::preclaim(PreclaimContext const& ctx)
{
    AccountID const accountID = ctx.tx[sfAccount];
    // auto const sequence = ctx.tx[sfSequence];
    auto const amount = ctx.tx[~sfAmount];
    auto const amount2 = ctx.tx[~sfAmount2];

    // Check if Firewall already exists
    ripple::Keylet const firewallKeylet = keylet::firewall(accountID);
    auto const sleFirewall = ctx.view.read(firewallKeylet);

    // if (auto const ter = requireAuth(ctx.view, amount.issue(), accountID);
    //     ter != tesSUCCESS)
    // {
    //     JLOG(ctx.j.debug())
    //         << "Firewall: account is not authorized, " << amount.issue();
    //     return ter;
    // }

    // if (auto const ter = requireAuth(ctx.view, amount2.issue(), accountID);
    //     ter != tesSUCCESS)
    // {
    //     JLOG(ctx.j.debug())
    //         << "Firewall: account is not authorized, " << amount2.issue();
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

    // if (noDefaultRipple(ctx.view, amount.issue()) ||
    //     noDefaultRipple(ctx.view, amount2.issue()))
    // {
    //     JLOG(ctx.j.debug()) << "Firewall: DefaultRipple not set";
    //     return terNO_RIPPLE;
    // }

    // Validate AuthAccounts (Has Account Has Amount Amount > 0)
    if (ctx.tx.isFieldPresent(sfSignature) && ctx.tx.isFieldPresent(sfAuthAccounts))
    {
        auto const authAccounts = ctx.tx.getFieldArray(sfAuthAccounts);
        auto const sig = ctx.tx.getFieldVL(sfSignature);
        PublicKey const pk(makeSlice(sleFirewall->getFieldVL(sfPublicKey)));
        Serializer msg;
        serializeFirewallAuthorization(msg, authAccounts);
        if (!verify(pk, msg.slice(), makeSlice(sig), /*canonical*/ true))
            return temBAD_SIGNATURE;
    }

    return tesSUCCESS;
}

TER
SetFirewall::doApply()
{
    Sandbox sb(&ctx_.view());
    
    ripple::Keylet const firewallKeylet = keylet::firewall(account_);
    auto firewallSle = sb.peek(firewallKeylet);
    if (!firewallSle)
    {
        // Set Firewall
        auto const firewallSle = std::make_shared<SLE>(firewallKeylet);
        (*firewallSle)[sfOwner] = account_;
        (*firewallSle)[~sfAuthorize] = ctx_.tx[~sfAuthorize];
        (*firewallSle)[~sfPublicKey] = ctx_.tx[~sfPublicKey];
        (*firewallSle)[~sfAmount] = ctx_.tx[~sfAmount];
        (*firewallSle)[~sfAmount2] = ctx_.tx[~sfAmount2];
        if (ctx_.tx.isFieldPresent(sfAuthAccounts))
        {
            auto const authAccounts = ctx_.tx.getFieldArray(sfAuthAccounts);
            firewallSle->setFieldArray(sfAuthAccounts, authAccounts);
        }

        // Add owner directory to link the account and Firewall object.
        if (auto const page = sb.dirInsert(
                keylet::ownerDir(account_),
                firewallSle->key(),
                describeOwnerDir(account_)))
        {
            firewallSle->setFieldU64(sfOwnerNode, *page);
        }
        else
        {
            JLOG(j_.error()) << "Firewall: failed to insert owner dir";
            return tecDIR_FULL;
        }

        sb.insert(firewallSle);
    }
    else
    {
        // Update Firewall
        JLOG(j_.error()) << "Firewall: Update Firewall";

        if (ctx_.tx.isFieldPresent(sfAuthorize))
            firewallSle->setAccountID(sfAuthorize, ctx_.tx.getAccountID(sfAuthorize));
        if (ctx_.tx.isFieldPresent(sfPublicKey))
            firewallSle->setFieldH256(sfPublicKey, ctx_.tx.getFieldH256(sfPublicKey));
        if (ctx_.tx.isFieldPresent(sfAmount))
            firewallSle->setFieldAmount(sfAmount, ctx_.tx.getFieldAmount(sfAmount));
        if (ctx_.tx.isFieldPresent(sfAmount2))
            firewallSle->setFieldAmount(sfAmount2, ctx_.tx.getFieldAmount(sfAmount2));

        if (ctx_.tx.isFieldPresent(sfAuthAccounts))
        {
            auto const authAccounts = ctx_.tx.getFieldArray(sfAuthAccounts);
            firewallSle->setFieldArray(sfAuthAccounts, authAccounts);
        }
        if (ctx_.tx.isFieldPresent(sfAuthAccounts))
        {
            auto const authAccounts = ctx_.tx.getFieldArray(sfAuthAccounts);
            firewallSle->setFieldArray(sfAuthAccounts, authAccounts);
        }
        sb.update(firewallSle);
    }

    sb.apply(ctx_.rawView());
    return tesSUCCESS;
}

}  // namespace ripple
