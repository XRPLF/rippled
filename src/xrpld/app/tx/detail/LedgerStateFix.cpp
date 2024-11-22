//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#include <xrpld/app/tx/detail/LedgerStateFix.h>

#include <xrpld/app/tx/detail/NFTokenUtils.h>
#include <xrpld/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
LedgerStateFix::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(fixNFTokenPageLinks))
        return temDISABLED;

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    switch (ctx.tx[sfLedgerFixType])
    {
        case FixType::nfTokenPageLink:
            if (!ctx.tx.isFieldPresent(sfOwner))
                return temINVALID;
            break;

        default:
            return tefINVALID_LEDGER_FIX_TYPE;
    }

    return preflight2(ctx);
}

XRPAmount
LedgerStateFix::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // The fee required for LedgerStateFix is one owner reserve, just like
    // the fee for AccountDelete.
    return view.fees().increment;
}

TER
LedgerStateFix::preclaim(PreclaimContext const& ctx)
{
    switch (ctx.tx[sfLedgerFixType])
    {
        case FixType::nfTokenPageLink: {
            AccountID const owner{ctx.tx[sfOwner]};
            if (!ctx.view.read(keylet::account(owner)))
                return tecOBJECT_NOT_FOUND;

            return tesSUCCESS;
        }
    }

    // preflight is supposed to verify that only valid FixTypes get to preclaim.
    return tecINTERNAL;
}

TER
LedgerStateFix::doApply()
{
    switch (ctx_.tx[sfLedgerFixType])
    {
        case FixType::nfTokenPageLink:
            if (!nft::repairNFTokenDirectoryLinks(view(), ctx_.tx[sfOwner]))
                return tecFAILED_PROCESSING;

            return tesSUCCESS;
    }

    // preflight is supposed to verify that only valid FixTypes get to doApply.
    return tecINTERNAL;
}

}  // namespace ripple
