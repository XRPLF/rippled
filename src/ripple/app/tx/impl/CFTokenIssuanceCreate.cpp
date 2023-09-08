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
    // if already a CFT with this asset code - error
    if (ctx.view.exists(
            keylet::cftIssuance(ctx.tx[sfAccount], ctx.tx[sfAssetCode])))
        return tecDUPLICATE;

    return tesSUCCESS;
}

TER
CFTokenIssuanceCreate::doApply()
{
    if (auto const acct = view().read(keylet::account(account_));
        mPriorBalance < view().fees().accountReserve((*acct)[sfOwnerCount] + 1))
        return tecINSUFFICIENT_RESERVE;

    auto const cftID = keylet::cftIssuance(account_, ctx_.tx[sfAssetCode]);

    // create the CFT
    {
        auto const ownerNode = view().dirInsert(
            keylet::ownerDir(account_), cftID, describeOwnerDir(account_));

        if (!ownerNode)
            return tecDIR_FULL;

        auto cft = std::make_shared<SLE>(cftID);
        (*cft)[sfFlags] = ctx_.tx.getFlags();
        (*cft)[sfIssuer] = account_;
        (*cft)[sfAssetCode] = ctx_.tx[sfAssetCode];
        (*cft)[sfOutstandingAmount] = 0;
        (*cft)[sfLockedAmount] = 0;
        (*cft)[sfOwnerNode] = *ownerNode;

        if (auto const max = ctx_.tx[~sfMaximumAmount])
            (*cft)[sfMaximumAmount] = *max;

        if (auto const scale = ctx_.tx[~sfAssetScale])
            (*cft)[sfAssetScale] = *scale;

        if (auto const fee = ctx_.tx[~sfTransferFee])
            (*cft)[sfTransferFee] = *fee;

        if (auto const metadata = ctx_.tx[~sfCFTokenMetadata])
            (*cft)[sfCFTokenMetadata] = *metadata;

        view().insert(cft);
    }

    // Update owner count.
    adjustOwnerCount(view(), view().peek(keylet::account(account_)), 1, j_);

    return tesSUCCESS;
}

}  // namespace ripple
