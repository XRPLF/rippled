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

#include <xrpld/app/misc/FirewallUtils.h>
#include <xrpld/app/tx/detail/Firewall.h>
#include <xrpld/ledger/View.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/STData.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/st.h>

namespace ripple {

namespace {

NotTEC
validateFirewallRules(STArray const& rules, beast::Journal const& j)
{
    if (rules.empty())
    {
        JLOG(j.error())
            << "FirewallSet: sfFirewallRules must not be empty if present";
        return temMALFORMED;
    }

    if (rules.size() > 8)
    {
        JLOG(j.error())
            << "FirewallSet: sfFirewallRules must not be larger than 8";
        return temMALFORMED;
    }

    for (auto const& rule : rules)
    {
        LedgerEntryType ledgerEntryType =
            static_cast<LedgerEntryType>(rule.getFieldU16(sfLedgerEntryType));
        SField const& sField = SField::getField(rule.getFieldU32(sfFieldCode));
        auto const& operatorCode = rule.getFieldU16(sfComparisonOperator);

        if (!hasFirewallProtection(ledgerEntryType))
        {
            JLOG(j.error()) << "FirewallSet: sfLedgerEntryType "
                            << ledgerEntryType << " is not supported";
            return temMALFORMED;
        }

        if (!isFieldProtected(ledgerEntryType, sField))
        {
            JLOG(j.error()) << "FirewallSet: sfFieldCode " << sField.getName()
                            << " is not supported";
            return temMALFORMED;
        }

        if (operatorCode < 1 || operatorCode > 5)
        {
            JLOG(j.error()) << "FirewallSet: sfComparisonOperator "
                            << operatorCode << " is not supported";
            return temMALFORMED;
        }
    }

    return tesSUCCESS;
}

}  // namespace

XRPAmount
FirewallSet::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    bool const isCreate = !tx.isFieldPresent(sfFirewallID);
    if (isCreate)
    {
        return view.fees().base;
    }
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
        JLOG(ctx.j.error()) << "FirewallSet: sfFlags are invalid for this tx";
        return temINVALID_FLAG;
    }

    bool const isCreate = !ctx.tx.isFieldPresent(sfFirewallID);
    AccountID const account = ctx.tx.getAccountID(sfAccount);
    if (isCreate)
    {
        // CREATE: Validate required fields and restrictions
        if (!ctx.tx.isFieldPresent(sfCounterParty))
        {
            JLOG(ctx.j.error())
                << "FirewallSet: sfCounterParty is required for creation";
            return temMALFORMED;
        }

        if (account == ctx.tx.getAccountID(sfCounterParty))
        {
            JLOG(ctx.j.error()) << "FirewallSet: sfCounterParty must not be "
                                   "the same as account";
            return temMALFORMED;
        }

        if (!ctx.tx.isFieldPresent(sfBackup))
        {
            JLOG(ctx.j.error())
                << "FirewallSet: sfBackup is required for creation";
            return temMALFORMED;
        }

        if (account == ctx.tx.getAccountID(sfBackup))
        {
            JLOG(ctx.j.error())
                << "FirewallSet: sfBackup must not be the same as account";
            return temMALFORMED;
        }

        if (ctx.tx.isFieldPresent(sfFirewallSigners))
        {
            JLOG(ctx.j.error())
                << "FirewallSet: sfFirewallSigners not allowed for creation";
            return temMALFORMED;
        }
    }
    else
    {
        // UPDATE: Validate required fields and restrictions
        if (!ctx.tx.isFieldPresent(sfFirewallSigners))
        {
            JLOG(ctx.j.error())
                << "FirewallSet: sfFirewallSigners required for updates";
            return temMALFORMED;
        }

        if (ctx.tx.isFieldPresent(sfBackup))
        {
            JLOG(ctx.j.error())
                << "FirewallSet: sfBackup not allowed for updates";
            return temMALFORMED;
        }

        if (ctx.tx.isFieldPresent(sfCounterParty) &&
            account == ctx.tx.getAccountID(sfCounterParty))
        {
            JLOG(ctx.j.error()) << "FirewallSet: sfCounterParty must not be "
                                   "the same as account";
            return temMALFORMED;
        }

        // Validate signers structure - similar to Batch validation
        auto const& signers = ctx.tx.getFieldArray(sfFirewallSigners);
        if (signers.empty())
        {
            JLOG(ctx.j.error())
                << "FirewallSet: sfFirewallSigners cannot be empty";
            return temMALFORMED;
        }

        // None of the signers can be the outer account
        for (auto const& signer : signers)
        {
            if (signer.getAccountID(sfAccount) ==
                ctx.tx.getAccountID(sfAccount))
            {
                JLOG(ctx.j.error())
                    << "FirewallSet: sfFirewallSigners cannot include the "
                       "outer account";
                return temMALFORMED;
            }
        }

        auto const sigResult = ctx.tx.checkFirewallSign(
            STTx::RequireFullyCanonicalSig::yes, ctx.rules);
        if (!sigResult)
        {
            JLOG(ctx.j.error()) << "FirewallSet: invalid firewall signature: "
                                << sigResult.error();
            return temBAD_SIGNATURE;
        }
    }

    // Validate firewall rules if present (common for both create and update)
    if (ctx.tx.isFieldPresent(sfFirewallRules))
    {
        if (auto const ret = validateFirewallRules(
                ctx.tx.getFieldArray(sfFirewallRules), ctx.j);
            !isTesSuccess(ret))
        {
            return ret;
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
            JLOG(ctx.j.error())
                << "FirewallSet: Firewall already exists for account";
            return tecDUPLICATE;
        }

        // Verify CounterParty account exists
        AccountID const counterParty = ctx.tx.getAccountID(sfCounterParty);
        if (!ctx.view.exists(keylet::account(counterParty)))
        {
            JLOG(ctx.j.error())
                << "FirewallSet: CounterParty account does not exist";
            return tecNO_DST;
        }

        // Verify Backup account exists
        AccountID const backup = ctx.tx.getAccountID(sfBackup);
        if (!ctx.view.exists(keylet::account(backup)))
        {
            JLOG(ctx.j.error()) << "FirewallSet: Backup account does not exist";
            return tecNO_DST;
        }

        // Check reserve requirements for both Firewall and WithdrawPreauth
        // entries
        auto const sleOwner = ctx.view.read(keylet::account(account));
        if (!sleOwner)
            return tefINTERNAL;

        auto const balance = sleOwner->getFieldAmount(sfBalance);
        auto const reserve = ctx.view.fees().accountReserve(
            sleOwner->getFieldU32(sfOwnerCount) +
            2);  // +2 for Firewall + WithdrawPreauth

        if (balance < reserve)
            return tecINSUFFICIENT_RESERVE;
    }
    else
    {
        // UPDATE: Verify firewall exists and validate authorization
        uint256 const firewallID = ctx.tx.getFieldH256(sfFirewallID);
        auto const sleFirewall = ctx.view.read(keylet::firewall(firewallID));

        if (!sleFirewall)
        {
            JLOG(ctx.j.error()) << "FirewallSet: Firewall not found";
            return tecNO_TARGET;
        }

        if (sleFirewall->getAccountID(sfOwner) != account)
        {
            JLOG(ctx.j.error())
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
                JLOG(ctx.j.error())
                    << "FirewallSet: sfCounterParty must not be the same as "
                       "existing CounterParty";
                return tecDUPLICATE;
            }

            if (!ctx.view.exists(keylet::account(newCounterParty)))
            {
                JLOG(ctx.j.error())
                    << "FirewallSet: New CounterParty account does not exist";
                return tecNO_DST;
            }
        }
    }

    return tesSUCCESS;
}

TER
FirewallSet::createFirewall(Sandbox& sb, std::shared_ptr<SLE> const& sleOwner)
{
    // Create Firewall entry
    auto const sleFirewall = std::make_shared<SLE>(keylet::firewall(account_));
    sleFirewall->setAccountID(sfOwner, account_);
    sleFirewall->setAccountID(
        sfCounterParty, ctx_.tx.getAccountID(sfCounterParty));

    if (ctx_.tx.isFieldPresent(sfFirewallRules))
    {
        sleFirewall->setFieldArray(
            sfFirewallRules, ctx_.tx.getFieldArray(sfFirewallRules));
    }

    // Insert firewall into owner directory
    if (auto const page = sb.dirInsert(
            keylet::ownerDir(account_),
            sleFirewall->key(),
            describeOwnerDir(account_)))
    {
        sleFirewall->setFieldU64(sfOwnerNode, *page);
    }
    else
    {
        JLOG(j_.error())
            << "FirewallSet: failed to insert firewall into owner dir";
        return tecDIR_FULL;
    }

    sb.insert(sleFirewall);
    adjustOwnerCount(sb, sleOwner, 1, j_);

    // Create initial WithdrawPreauth entry for backup account
    AccountID const backup = ctx_.tx.getAccountID(sfBackup);
    auto slePreauth =
        std::make_shared<SLE>(keylet::withdrawPreauth(account_, backup));
    slePreauth->setAccountID(sfAccount, account_);
    slePreauth->setAccountID(sfAuthorize, backup);

    // Insert preauth into owner directory
    if (auto const page = sb.dirInsert(
            keylet::ownerDir(account_),
            slePreauth->key(),
            describeOwnerDir(account_)))
    {
        slePreauth->setFieldU64(sfOwnerNode, *page);
    }
    else
    {
        JLOG(j_.error())
            << "FirewallSet: failed to insert preauth into owner dir";
        return tecDIR_FULL;
    }

    sb.insert(slePreauth);
    adjustOwnerCount(sb, sleOwner, 1, j_);

    // Final reserve check
    STAmount const reserve{
        view().fees().accountReserve(sleOwner->getFieldU32(sfOwnerCount))};
    if (mPriorBalance < reserve)
        return tecINSUFFICIENT_RESERVE;

    sb.apply(ctx_.rawView());
    return tesSUCCESS;
}

TER
FirewallSet::updateFirewall(Sandbox& sb)
{
    uint256 const firewallID = ctx_.tx.getFieldH256(sfFirewallID);
    auto sleFirewall = sb.peek(keylet::firewall(firewallID));

    if (!sleFirewall)
    {
        JLOG(j_.error()) << "FirewallSet: Firewall not found during apply";
        return tefINTERNAL;
    }

    // Update CounterParty if provided
    if (ctx_.tx.isFieldPresent(sfCounterParty))
    {
        sleFirewall->setAccountID(
            sfCounterParty, ctx_.tx.getAccountID(sfCounterParty));
    }

    // Update FirewallRules if provided
    if (ctx_.tx.isFieldPresent(sfFirewallRules))
    {
        auto const& newRules = ctx_.tx.getFieldArray(sfFirewallRules);
        if (newRules.empty())
        {
            sleFirewall->makeFieldAbsent(sfFirewallRules);
        }
        else
        {
            sleFirewall->setFieldArray(sfFirewallRules, newRules);
        }
    }

    sb.update(sleFirewall);
    sb.apply(ctx_.rawView());
    return tesSUCCESS;
}

TER
FirewallSet::doApply()
{
    Sandbox sb(&ctx_.view());

    auto const sleOwner = sb.peek(keylet::account(account_));
    if (!sleOwner)
    {
        JLOG(j_.error()) << "FirewallSet: Owner account not found";
        return tefINTERNAL;
    }

    bool const isCreate = !ctx_.tx.isFieldPresent(sfFirewallID);
    if (isCreate)
    {
        // CREATE: Set up new firewall and initial preauth
        return createFirewall(sb, sleOwner);
    }
    else
    {
        // UPDATE: Modify existing firewall
        return updateFirewall(sb);
    }
}

}  // namespace ripple