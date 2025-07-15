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

#include <xrpld/app/misc/MPTUtils.h>
#include <xrpld/ledger/View.h>

#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>

namespace ripple {

static TER
isMPTAllowed(
    ReadView const& view,
    TxType txType,
    Asset const& asset,
    AccountID const& accountID,
    std::optional<AccountID> const& destAccount)
{
    if (!asset.holds<MPTIssue>())
        return tesSUCCESS;

    auto const& issuanceID = asset.get<MPTIssue>().getMptID();
    auto const isDEX = txType == ttPAYMENT && destAccount;
    auto const validTx = txType == ttAMM_CREATE || txType == ttAMM_DEPOSIT ||
        txType == ttAMM_WITHDRAW || txType == ttOFFER_CREATE ||
        txType == ttCHECK_CREATE || txType == ttCHECK_CASH ||
        txType == ttPAYMENT || isDEX;
    XRPL_ASSERT(validTx, "ripple::isMPTAllowed : all MPT tx or DEX");
    if (!validTx)
        return tefINTERNAL;

    auto const& issuer = asset.getIssuer();
    if (!view.exists(keylet::account(issuer)))
        return tecNO_ISSUER;

    auto const issuanceKey = keylet::mptIssuance(issuanceID);
    auto const issuanceSle = view.read(issuanceKey);
    if (!issuanceSle)
        return tecOBJECT_NOT_FOUND;

    auto const flags = issuanceSle->getFlags();

    if (flags & lsfMPTLocked)
        return tecLOCKED;
    // Offer crossing and Payment
    if ((flags & lsfMPTCanTrade) == 0 && isDEX)
        return tecNO_PERMISSION;
    if ((flags & lsfMPTCanClawback) && txType == ttAMM_CREATE)
        return tecNO_PERMISSION;

    if (accountID != issuer)
    {
        if ((flags & lsfMPTCanTransfer) == 0 &&
            (!destAccount || destAccount != issuer))
            return tecNO_PERMISSION;

        auto const mptSle =
            view.read(keylet::mptoken(issuanceKey.key, accountID));
        // Allow to succeed since some tx create MPToken if it doesn't exist.
        // Tx's have their own check for missing MPToken.
        if (!mptSle)
            return tesSUCCESS;

        if ((mptSle->getFlags() & lsfMPTLocked) &&
            (!destAccount || destAccount != issuer))
            return tecLOCKED;
    }

    return tesSUCCESS;
}

TER
isMPTTxAllowed(
    ReadView const& view,
    TxType txType,
    Asset const& asset,
    AccountID const& accountID,
    std::optional<AccountID> const& destAccount)
{
    // use isDEXAllowed for payment/offer crossing
    XRPL_ASSERT(txType != ttPAYMENT, "ripple::isMPTTxAllowed : not payment");
    return isMPTAllowed(view, txType, asset, accountID, destAccount);
}

TER
isMPTDEXAllowed(
    ReadView const& view,
    Asset const& asset,
    AccountID const& accountID,
    AccountID const& dest)
{
    // use ttPAYMENT for both offer crossing and payment
    return isMPTAllowed(view, ttPAYMENT, asset, accountID, dest);
}

}  // namespace ripple
