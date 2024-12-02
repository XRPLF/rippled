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
        auto const backupID = ctx.tx.getAccountID(sfAuthorize);
        // Make sure that the passed account is valid.
        if (backupID == beast::zero)
        {
            JLOG(ctx.j.debug())
                << "Malformed transaction: Authorized or Unauthorized "
                   "field zeroed.";
            return temINVALID_ACCOUNT_ID;
        }

        // An account may not preauthorize itself.
        if (backupID == ctx.tx[sfAccount])
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
    ripple::Keylet const firewallKeylet = keylet::firewall(accountID);
    auto const sleFirewall = ctx.view.read(firewallKeylet);

    if (!sleFirewall)
    {
        if (ctx.tx.isFieldPresent(sfSignature))
        {
            JLOG(ctx.j.debug())
                << "Firewall: Set must not contain a sfSignature";
            return temMALFORMED;
        }
        if (!ctx.tx.isFieldPresent(sfAuthorize))
        {
            JLOG(ctx.j.debug()) << "Firewall: Set must contain a sfAuthorize";
            return temMALFORMED;
        }
        if (!ctx.tx.isFieldPresent(sfPublicKey))
        {
            JLOG(ctx.j.debug()) << "Firewall: Set must contain a sfPublicKey";
            return temMALFORMED;
        }
    }
    else
    {
        if (!ctx.tx.isFieldPresent(sfSignature))
        {
            JLOG(ctx.j.debug())
                << "Firewall: Update must contain a sfSignature";
            return temMALFORMED;
        }

        if (ctx.tx.isFieldPresent(sfAuthorize))
        {
            JLOG(ctx.j.debug())
                << "Firewall: Update cannot contain a sfAuthorize";
            return temMALFORMED;
        }

        if (ctx.tx.isFieldPresent(sfPublicKey) &&
            ctx.tx.isFieldPresent(sfAmount))
        {
            JLOG(ctx.j.debug()) << "Firewall: Update cannot contain both "
                                   "sfPublicKey & sfAmount";
            return temMALFORMED;
        }

        if (ctx.tx.isFieldPresent(sfSignature) &&
            ctx.tx.isFieldPresent(sfPublicKey))
        {
            PublicKey const txPK(makeSlice(ctx.tx.getFieldVL(sfPublicKey)));
            auto const sig = ctx.tx.getFieldVL(sfSignature);
            PublicKey const fPK(
                makeSlice(sleFirewall->getFieldVL(sfPublicKey)));
            Serializer msg;
            serializeFirewallAuthorization(msg, accountID, txPK);
            if (!verify(fPK, msg.slice(), makeSlice(sig), /*canonical*/ true))
            {
                JLOG(ctx.j.debug())
                    << "Firewall: Bad Signature for update sfPublicKey";
                return temBAD_SIGNATURE;
            }
        }

        if (ctx.tx.isFieldPresent(sfSignature) &&
            ctx.tx.isFieldPresent(sfAmount))
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
    }

    return tesSUCCESS;
}

TER
FirewallSet::doApply()
{
    Sandbox sb(&ctx_.view());

    auto const sleOwner = sb.peek(keylet::account(account_));
    if (!sleOwner)
    {
        JLOG(j_.debug()) << "Firewall: Owner account not found";
        return tefINTERNAL;
    }

    ripple::Keylet const firewallKeylet = keylet::firewall(account_);
    auto sleFirewall = sb.peek(firewallKeylet);
    if (!sleFirewall)
    {
        auto const sleFirewall = std::make_shared<SLE>(firewallKeylet);
        (*sleFirewall)[sfOwner] = account_;
        sleFirewall->setFieldVL(sfPublicKey, ctx_.tx.getFieldVL(sfPublicKey));
        if (ctx_.tx.isFieldPresent(sfAmount))
            sleFirewall->setFieldAmount(
                sfAmount, ctx_.tx.getFieldAmount(sfAmount));

        if (ctx_.tx.isFieldPresent(sfTimePeriod))
        {
            sleFirewall->setFieldU32(sfTimePeriod, ctx_.tx.getFieldU32(sfTimePeriod));
            sleFirewall->setFieldU32(sfTimePeriodStart, ctx_.view().parentCloseTime().time_since_epoch().count());
            sleFirewall->setFieldAmount(sfTotalOut, STAmount{0});
        }

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

        {
            STAmount const reserve{view().fees().accountReserve(
                sleOwner->getFieldU32(sfOwnerCount) + 1)};

            if (mPriorBalance < reserve)
                return tecINSUFFICIENT_RESERVE;
        }

        AccountID const auth = ctx_.tx.getAccountID(sfAuthorize);
        Keylet const preauthKeylet = keylet::firewallPreauth(account_, auth);
        auto slePreauth = std::make_shared<SLE>(preauthKeylet);

        slePreauth->setAccountID(sfAccount, account_);
        slePreauth->setAccountID(sfAuthorize, auth);

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
