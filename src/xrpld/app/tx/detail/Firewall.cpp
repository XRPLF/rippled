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

#include <xrpld/app/tx/detail/Firewall.h>
#include <xrpld/ledger/Sandbox.h>
#include <xrpld/ledger/View.h>
#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/st.h>

namespace ripple {

XRPAmount
FirewallSet::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // Calculate the FirewallSigners Fees
    // std::int32_t signerCount = tx.isFieldPresent(sfFirewallSigners)
    //     ? tx.getFieldArray(sfFirewallSigners).size()
    //     : 0;

    // return ((signerCount + 2) * view.fees().base);
    return view.fees().base;
}

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
    //         << "FirewallSet: tokens can not have the same currency/issuer.";
    //     return temBAD_AMM_TOKENS;
    // }

    // if (auto const err = invalidAmount(amount))
    // {
    //     JLOG(ctx.j.debug()) << "FirewallSet: invalid asset1 amount.";
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
                << "Malformed transaction: Attempting to WithdrawPreauth self.";
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
        if (ctx.tx.isFieldPresent(sfFirewallSigners))
        {
            JLOG(ctx.j.debug())
                << "FirewallSet: Set must not contain a sfFirewallSigners";
            return temMALFORMED;
        }
        if (!ctx.tx.isFieldPresent(sfAuthorize))
        {
            JLOG(ctx.j.debug()) << "FirewallSet: Set must contain a sfAuthorize";
            return temMALFORMED;
        }
        if (!ctx.tx.isFieldPresent(sfIssuer))
        {
            JLOG(ctx.j.debug()) << "FirewallSet: Set must contain a sfIssuer";
            return temMALFORMED;
        }
    }
    else
    {
        if (ctx.tx.isFieldPresent(sfAuthorize))
        {
            JLOG(ctx.j.debug())
                << "FirewallSet: Update cannot contain a sfAuthorize";
            return temMALFORMED;
        }

        if (!ctx.tx.isFieldPresent(sfFirewallSigners))
        {
            JLOG(ctx.j.debug()) << "FirewallSet: Update must contain sfFirewallSigners";
            return temMALFORMED;
        }

        std::set<AccountID> firewallSignersSet;
        if (ctx.tx.isFieldPresent(sfFirewallSigners))
        {
            STArray const signers = ctx.tx.getFieldArray(sfFirewallSigners);

            // Check that the firewall signers array is not too large.
            if (signers.size() > 8)
            {
                JLOG(ctx.j.trace()) << "FirewallSet: signers array exceeds 8 entries.";
                return temARRAY_TOO_LARGE;
            }

            // Add the batch signers to the set.
            for (auto const& signer : signers)
            {
                AccountID const innerAccount = signer.getAccountID(sfAccount);
                if (!firewallSignersSet.insert(innerAccount).second)
                {
                    JLOG(ctx.j.trace())
                        << "FirewallSet: Duplicate signer found: " << innerAccount;
                    return temBAD_SIGNER;
                }
            }

            // Check the batch signers signatures.
            auto const requireCanonicalSig =
                ctx.view.rules().enabled(featureRequireFullyCanonicalSig)
                ? STTx::RequireFullyCanonicalSig::yes
                : STTx::RequireFullyCanonicalSig::no;
            auto const sigResult =
                ctx.tx.checkFirewallSign(requireCanonicalSig, ctx.view.rules());

            if (!sigResult)
            {
                JLOG(ctx.j.trace()) << "FirewallSet: invalid batch txn signature.";
                return temBAD_SIGNATURE;
            }
        }

        if (ctx.tx.isFieldPresent(sfFirewallSigners) &&
            firewallSignersSet.size() != ctx.tx.getFieldArray(sfFirewallSigners).size())
        {
            JLOG(ctx.j.trace())
                << "FirewallSet: unique signers does not match firewall signers.";
            return temBAD_SIGNER;
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
        JLOG(j_.debug()) << "FirewallSet: Owner account not found";
        return tefINTERNAL;
    }

    ripple::Keylet const firewallKeylet = keylet::firewall(account_);
    auto sleFirewall = sb.peek(firewallKeylet);
    if (!sleFirewall)
    {
        auto const sleFirewall = std::make_shared<SLE>(firewallKeylet);
        (*sleFirewall)[sfOwner] = account_;
        sleFirewall->setAccountID(sfIssuer, ctx_.tx.getAccountID(sfIssuer));
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
            JLOG(j_.debug()) << "FirewallSet: failed to insert owner dir";
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
        Keylet const preauthKeylet = keylet::withdrawPreauth(account_, auth);
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
            JLOG(j_.debug()) << "FirewallSet: failed to insert owner dir";
            return tecDIR_FULL;
        }
        sb.insert(slePreauth);
        adjustOwnerCount(sb, sleOwner, 1, j_);
    }
    else
    {
        if (ctx_.tx.isFieldPresent(sfIssuer))
            sleFirewall->setAccountID(
                sfIssuer, ctx_.tx.getAccountID(sfIssuer));
        if (ctx_.tx.isFieldPresent(sfAmount))
            sleFirewall->setFieldAmount(
                sfAmount, ctx_.tx.getFieldAmount(sfAmount));

        sb.update(sleFirewall);
    }

    sb.apply(ctx_.rawView());
    return tesSUCCESS;
}

}  // namespace ripple
