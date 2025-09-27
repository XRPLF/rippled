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

#include <xrpld/app/misc/FirewallHelpers.h>
#include <xrpld/app/tx/detail/FirewallSet.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/st.h>

namespace ripple {

XRPAmount
FirewallSet::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    bool const isCreate = !tx.isFieldPresent(sfFirewallID);
    if (isCreate)
        return view.fees().base;
    else
    {
        std::int32_t signerCount = tx.isFieldPresent(sfFirewallSigners)
            ? tx.getFieldArray(sfFirewallSigners).size()
            : 0;
        return ((signerCount + 2) * view.fees().base);
    }
}

NotTEC
FirewallSet::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureFirewall))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
    {
        JLOG(ctx.j.trace()) << "FirewallSet: sfFlags are invalid for this tx";
        return temINVALID_FLAG;
    }

    bool const isCreate = !ctx.tx.isFieldPresent(sfFirewallID);
    AccountID const account = ctx.tx.getAccountID(sfAccount);
    if (isCreate)
    {
        // CREATE: Validate required fields and restrictions
        if (!ctx.tx.isFieldPresent(sfCounterParty))
        {
            JLOG(ctx.j.trace())
                << "FirewallSet: sfCounterParty is required for creation";
            return temMALFORMED;
        }

        if (account == ctx.tx.getAccountID(sfCounterParty))
        {
            JLOG(ctx.j.trace()) << "FirewallSet: sfCounterParty must not be "
                                   "the same as account";
            return temMALFORMED;
        }

        if (!ctx.tx.isFieldPresent(sfBackup))
        {
            JLOG(ctx.j.trace())
                << "FirewallSet: sfBackup is required for creation";
            return temMALFORMED;
        }

        if (account == ctx.tx.getAccountID(sfBackup))
        {
            JLOG(ctx.j.trace())
                << "FirewallSet: sfBackup must not be the same as account";
            return temMALFORMED;
        }

        if (ctx.tx.isFieldPresent(sfFirewallSigners))
        {
            JLOG(ctx.j.trace())
                << "FirewallSet: sfFirewallSigners not allowed for creation";
            return temMALFORMED;
        }
    }
    else
    {
        // UPDATE: Validate required fields and restrictions
        if (ctx.tx.isFieldPresent(sfBackup))
        {
            JLOG(ctx.j.trace())
                << "FirewallSet: sfBackup not allowed for updates";
            return temMALFORMED;
        }

        if (ctx.tx.isFieldPresent(sfCounterParty) &&
            account == ctx.tx.getAccountID(sfCounterParty))
        {
            JLOG(ctx.j.trace()) << "FirewallSet: sfCounterParty must not be "
                                   "the same as account";
            return temMALFORMED;
        }

        if (auto const ter = firewall::checkFirewallSigners(ctx);
            !isTesSuccess(ter))
            return ter;
    }

    if (ctx.tx.isFieldPresent(sfMaxFee))
    {
        auto const maxFee = ctx.tx.getFieldAmount(sfMaxFee);
        if (!maxFee.native() || maxFee.negative() || !isLegalNet(maxFee))
        {
            JLOG(ctx.j.trace()) << "FirewallSet: sfMaxFee is invalid";
            return temBAD_AMOUNT;
        }
    }

    return preflight2(ctx);
}

NotTEC
FirewallSet::checkSign(PreclaimContext const& ctx)
{
    if (auto ret = Transactor::checkSign(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.isFieldPresent(sfFirewallSigners))
    {
        if (auto ret = Transactor::checkFirewallSign(ctx); !isTesSuccess(ret))
            return ret;
    }

    return tesSUCCESS;
}

TER
FirewallSet::preclaim(PreclaimContext const& ctx)
{
    AccountID const account = ctx.tx.getAccountID(sfAccount);
    bool const isCreate = !ctx.tx.isFieldPresent(sfFirewallID);

    if (isCreate)
    {
        // CREATE: Verify firewall doesn't already exist
        if (ctx.view.exists(keylet::firewall(account)))
        {
            JLOG(ctx.j.trace())
                << "FirewallSet: Firewall already exists for account";
            return tecDUPLICATE;
        }

        // Verify CounterParty account exists
        AccountID const counterParty = ctx.tx.getAccountID(sfCounterParty);
        if (!ctx.view.exists(keylet::account(counterParty)))
        {
            JLOG(ctx.j.trace())
                << "FirewallSet: CounterParty account does not exist";
            return tecNO_DST;
        }

        // Verify Backup account exists
        AccountID const backup = ctx.tx.getAccountID(sfBackup);
        if (!ctx.view.exists(keylet::account(backup)))
        {
            JLOG(ctx.j.trace()) << "FirewallSet: Backup account does not exist";
            return tecNO_DST;
        }

        // Check reserve requirements for both Firewall and WithdrawPreauth
        // entries
        auto const sleOwner = ctx.view.read(keylet::account(account));
        if (!sleOwner)
        {  // LCOV_EXCL_START
            JLOG(ctx.j.trace()) << "FirewallSet: Owner account not found";
            return tecINTERNAL;
        }  // LCOV_EXCL_STOP

        auto const balance = sleOwner->getFieldAmount(sfBalance);
        auto const reserve = ctx.view.fees().accountReserve(
            sleOwner->getFieldU32(sfOwnerCount) +
            2);  // +2 for Firewall + WithdrawPreauth

        if (balance < reserve)
        {
            JLOG(ctx.j.trace())
                << "FirewallSet: Insufficient reserve to create firewall";
            return tecINSUFFICIENT_RESERVE;
        }
    }
    else
    {
        // UPDATE: Verify firewall exists and validate authorization
        uint256 const firewallID = ctx.tx.getFieldH256(sfFirewallID);
        auto const sleFirewall = ctx.view.read(keylet::firewall(firewallID));

        if (!sleFirewall)
        {
            JLOG(ctx.j.trace()) << "FirewallSet: Firewall not found";
            return tecNO_TARGET;
        }

        if (sleFirewall->getAccountID(sfOwner) != account)
        {
            JLOG(ctx.j.trace())
                << "FirewallSet: Account is not the firewall owner";
            return tecNO_PERMISSION;
        }

        // If updating counterparty, verify new account exists
        if (ctx.tx.isFieldPresent(sfCounterParty))
        {
            AccountID const newCounterParty =
                ctx.tx.getAccountID(sfCounterParty);
            if (sleFirewall->getAccountID(sfCounterParty) == newCounterParty)
            {
                JLOG(ctx.j.trace())
                    << "FirewallSet: sfCounterParty must not be the same as "
                       "existing CounterParty";
                return tecDUPLICATE;
            }

            if (!ctx.view.exists(keylet::account(newCounterParty)))
            {
                JLOG(ctx.j.trace())
                    << "FirewallSet: New CounterParty account does not exist";
                return tecNO_DST;
            }
        }
    }

    return tesSUCCESS;
}

TER
FirewallSet::createFirewall(std::shared_ptr<SLE> const& sleOwner)
{
    // Create Firewall entry
    auto const sleFirewall = std::make_shared<SLE>(keylet::firewall(account_));
    sleFirewall->setAccountID(sfOwner, account_);
    sleFirewall->setAccountID(
        sfCounterParty, ctx_.tx.getAccountID(sfCounterParty));
    if (ctx_.tx.isFieldPresent(sfMaxFee))
        sleFirewall->setFieldAmount(sfMaxFee, ctx_.tx.getFieldAmount(sfMaxFee));

    // Insert firewall into owner directory
    if (auto const page = ctx_.view().dirInsert(
            keylet::ownerDir(account_),
            sleFirewall->key(),
            describeOwnerDir(account_)))
    {
        sleFirewall->setFieldU64(sfOwnerNode, *page);
    }
    else
    {  // LCOV_EXCL_START
        JLOG(j_.trace())
            << "FirewallSet: failed to insert firewall into owner dir";
        return tecDIR_FULL;
    }  // LCOV_EXCL_STOP

    ctx_.view().insert(sleFirewall);
    adjustOwnerCount(ctx_.view(), sleOwner, 1, j_);

    // Create initial WithdrawPreauth entry for backup account
    AccountID const backup = ctx_.tx.getAccountID(sfBackup);
    std::uint32_t dtag = ctx_.tx.isFieldPresent(sfDestinationTag)
        ? ctx_.tx.getFieldU32(sfDestinationTag)
        : 0;
    auto slePreauth =
        std::make_shared<SLE>(keylet::withdrawPreauth(account_, backup, dtag));
    slePreauth->setAccountID(sfAccount, account_);
    slePreauth->setAccountID(sfAuthorize, backup);
    slePreauth->setFieldU32(sfDestinationTag, dtag);

    // Insert preauth into owner directory
    if (auto const page = ctx_.view().dirInsert(
            keylet::ownerDir(account_),
            slePreauth->key(),
            describeOwnerDir(account_)))
    {
        slePreauth->setFieldU64(sfOwnerNode, *page);
    }
    else
    {  // LCOV_EXCL_START
        JLOG(j_.trace())
            << "FirewallSet: failed to insert preauth into owner dir";
        return tecDIR_FULL;
    }  // LCOV_EXCL_STOP

    ctx_.view().insert(slePreauth);
    adjustOwnerCount(ctx_.view(), sleOwner, 1, j_);

    // Final reserve check
    STAmount const reserve{
        ctx_.view().fees().accountReserve(sleOwner->getFieldU32(sfOwnerCount))};
    if (mPriorBalance < reserve)
        return tecINSUFFICIENT_RESERVE;

    return tesSUCCESS;
}

TER
FirewallSet::updateFirewall()
{
    uint256 const firewallID = ctx_.tx.getFieldH256(sfFirewallID);
    auto sleFirewall = ctx_.view().peek(keylet::firewall(firewallID));

    if (!sleFirewall)
    {  // LCOV_EXCL_START
        JLOG(j_.trace()) << "FirewallSet: Firewall not found during apply";
        return tefINTERNAL;
    }  // LCOV_EXCL_STOP

    // Update CounterParty if provided
    if (ctx_.tx.isFieldPresent(sfCounterParty))
    {
        sleFirewall->setAccountID(
            sfCounterParty, ctx_.tx.getAccountID(sfCounterParty));
    }

    // Update MaxFee if provided
    if (ctx_.tx.isFieldPresent(sfMaxFee))
    {
        if (ctx_.tx.getFieldAmount(sfMaxFee) == beast::zero)
            sleFirewall->makeFieldAbsent(sfMaxFee);
        else
            sleFirewall->setFieldAmount(
                sfMaxFee, ctx_.tx.getFieldAmount(sfMaxFee));
    }

    ctx_.view().update(sleFirewall);
    return tesSUCCESS;
}

TER
FirewallSet::doApply()
{
    auto const sleOwner = ctx_.view().peek(keylet::account(account_));
    if (!sleOwner)
    {  // LCOV_EXCL_START
        JLOG(j_.trace()) << "FirewallSet: Owner account not found";
        return tefINTERNAL;
    }  // LCOV_EXCL_STOP

    bool const isCreate = !ctx_.tx.isFieldPresent(sfFirewallID);
    if (isCreate)
    {
        // CREATE: Set up new firewall and initial preauth
        return createFirewall(sleOwner);
    }
    else
    {
        // UPDATE: Modify existing firewall
        return updateFirewall();
    }
}

}  // namespace ripple
