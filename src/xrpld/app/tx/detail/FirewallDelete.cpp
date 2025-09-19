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
#include <xrpld/app/tx/detail/FirewallDelete.h>
#include <xrpld/app/tx/detail/WithdrawPreauth.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/st.h>

namespace ripple {

XRPAmount
FirewallDelete::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    std::int32_t signerCount = tx.isFieldPresent(sfFirewallSigners)
        ? tx.getFieldArray(sfFirewallSigners).size()
        : 0;
    return ((signerCount + 2) * view.fees().base);
}

NotTEC
FirewallDelete::preflight(PreflightContext const& ctx)
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

    if (auto const ter = firewall::checkFirewallSigners(ctx);
        !isTesSuccess(ter))
        return ter;

    return preflight2(ctx);
}

NotTEC
FirewallDelete::checkSign(PreclaimContext const& ctx)
{
    if (auto ret = Transactor::checkSign(ctx); !isTesSuccess(ret))
        return ret;

    if (auto ret = Transactor::checkFirewallSign(ctx); !isTesSuccess(ret))
        return ret;

    return tesSUCCESS;
}

TER
FirewallDelete::preclaim(PreclaimContext const& ctx)
{
    AccountID const account = ctx.tx.getAccountID(sfAccount);

    uint256 const firewallID = ctx.tx.getFieldH256(sfFirewallID);
    auto const sleFirewall = ctx.view.read(keylet::firewall(firewallID));

    if (!sleFirewall)
    {
        JLOG(ctx.j.trace()) << "FirewallSet: Firewall not found";
        return tecNO_TARGET;
    }

    if (sleFirewall->getAccountID(sfOwner) != account)
    {
        JLOG(ctx.j.trace()) << "FirewallSet: Account is not the firewall owner";
        return tecNO_PERMISSION;
    }

    return tesSUCCESS;
}

TER
FirewallDelete::doApply()
{
    auto const sleOwner = ctx_.view().peek(keylet::account(account_));
    if (!sleOwner)
    {  // LCOV_EXCL_START
        JLOG(j_.trace()) << "FirewallSet: Owner account not found";
        return tefINTERNAL;
    }  // LCOV_EXCL_STOP

    uint256 const firewallID = ctx_.tx.getFieldH256(sfFirewallID);
    auto sleFirewall = ctx_.view().peek(keylet::firewall(firewallID));
    if (!sleFirewall)
    {  // LCOV_EXCL_START
        JLOG(j_.trace()) << "FirewallSet: Firewall not found during apply";
        return tefINTERNAL;
    }  // LCOV_EXCL_STOP

    Keylet const ownerDirKeylet{keylet::ownerDir(account_)};
    auto const ter = cleanupOnAccountDelete(
        view(),
        ownerDirKeylet,
        [&](LedgerEntryType nodeType,
            uint256 const& dirEntry,
            std::shared_ptr<SLE>& sleItem) -> std::pair<TER, SkipEntry> {
            if (nodeType == ltWITHDRAW_PREAUTH)
            {
                TER const result{
                    WithdrawPreauth::removeFromLedger(view(), dirEntry, j_)};
                return {result, SkipEntry::No};
            }
            return {tesSUCCESS, SkipEntry::Yes};
        },
        ctx_.journal);
    if (ter != tesSUCCESS)
        return ter;  // LCOV_EXCL_LINE

    std::uint64_t const page{(*sleFirewall)[sfOwnerNode]};
    if (!ctx_.view().dirRemove(
            keylet::ownerDir(account_), page, firewallID, false))
    {  // LCOV_EXCL_START
        JLOG(j_.fatal()) << "Unable to delete Firewall from owner.";
        return tefBAD_LEDGER;
    }  // LCOV_EXCL_STOP

    adjustOwnerCount(ctx_.view(), sleOwner, -1, j_);
    ctx_.view().erase(sleFirewall);
    return tesSUCCESS;
}

}  // namespace ripple
