//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2023 Ripple Labs Inc.

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

#include <ripple/app/tx/impl/CFTokenIssuanceCreate.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/st.h>

namespace ripple {

NotTEC
CFTokenIssuanceCreate::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureCFTokensV1))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfCFTokenIssuanceCreateMask)
        return temINVALID_FLAG;

    if (auto const fee = ctx.tx[~sfTransferFee])
    {
        if (fee > maxTransferFee)
            return temBAD_CFTOKEN_TRANSFER_FEE;

        // If a non-zero TransferFee is set then the tfTransferable flag
        // must also be set.
        if (fee > 0u && !ctx.tx.isFlag(tfCFTCanTransfer))
            return temMALFORMED;
    }

    if (auto const metadata = ctx.tx[~sfCFTokenMetadata])
    {
        if (metadata->length() == 0 ||
            metadata->length() > maxCFTokenMetadataLength)
            return temMALFORMED;
    }

    return preflight2(ctx);
}

TER
CFTokenIssuanceCreate::preclaim(PreclaimContext const& ctx)
{
    return tesSUCCESS;
}

TER
CFTokenIssuanceCreate::doApply()
{
    auto const acct = view().peek(keylet::account(account_));
    if (!acct)
        return tecINTERNAL;

    if (mPriorBalance < view().fees().accountReserve((*acct)[sfOwnerCount] + 1))
        return tecINSUFFICIENT_RESERVE;

    auto const cftIssuanceKeylet =
        keylet::cftIssuance(account_, ctx_.tx.getSeqProxy().value());

    // create the CFTokenIssuance
    {
        auto const ownerNode = view().dirInsert(
            keylet::ownerDir(account_),
            cftIssuanceKeylet,
            describeOwnerDir(account_));

        if (!ownerNode)
            return tecDIR_FULL;

        auto cftIssuance = std::make_shared<SLE>(cftIssuanceKeylet);
        (*cftIssuance)[sfFlags] = ctx_.tx.getFlags() & ~tfUniversal;
        (*cftIssuance)[sfIssuer] = account_;
        (*cftIssuance)[sfOutstandingAmount] = 0;
        (*cftIssuance)[sfOwnerNode] = *ownerNode;
        (*cftIssuance)[sfSequence] = ctx_.tx.getSeqProxy().value();
        
        if (auto const max = ctx_.tx[~sfMaximumAmount])
            (*cftIssuance)[sfMaximumAmount] = *max;

        if (auto const scale = ctx_.tx[~sfAssetScale])
            (*cftIssuance)[sfAssetScale] = *scale;

        if (auto const fee = ctx_.tx[~sfTransferFee])
            (*cftIssuance)[sfTransferFee] = *fee;

        if (auto const metadata = ctx_.tx[~sfCFTokenMetadata])
            (*cftIssuance)[sfCFTokenMetadata] = *metadata;

        view().insert(cftIssuance);
    }

    // Update owner count.
    adjustOwnerCount(view(), acct, 1, j_);

    return tesSUCCESS;
}

}  // namespace ripple
