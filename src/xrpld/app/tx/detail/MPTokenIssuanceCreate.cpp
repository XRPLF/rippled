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

#include <xrpld/app/tx/detail/MPTokenIssuanceCreate.h>
#include <xrpld/ledger/View.h>

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
MPTokenIssuanceCreate::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureMPTokensV1))
        return temDISABLED;

    if (ctx.tx.isFieldPresent(sfDomainID) &&
        !(ctx.rules.enabled(featurePermissionedDomains) &&
          ctx.rules.enabled(featureSingleAssetVault)))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfMPTokenIssuanceCreateMask)
        return temINVALID_FLAG;

    if (auto const fee = ctx.tx[~sfTransferFee])
    {
        if (fee > maxTransferFee)
            return temBAD_TRANSFER_FEE;

        // If a non-zero TransferFee is set then the tfTransferable flag
        // must also be set.
        if (fee > 0u && !ctx.tx.isFlag(tfMPTCanTransfer))
            return temMALFORMED;
    }

    if (auto const domain = ctx.tx[~sfDomainID])
    {
        if (*domain == beast::zero)
            return temMALFORMED;

        // Domain present implies that MPTokenIssuance is not public
        if ((ctx.tx.getFlags() & tfMPTRequireAuth) == 0)
            return temMALFORMED;
    }

    if (auto const metadata = ctx.tx[~sfMPTokenMetadata])
    {
        if (metadata->length() == 0 ||
            metadata->length() > maxMPTokenMetadataLength)
            return temMALFORMED;
    }

    // Check if maximumAmount is within unsigned 63 bit range
    if (auto const maxAmt = ctx.tx[~sfMaximumAmount])
    {
        if (maxAmt == 0)
            return temMALFORMED;

        if (maxAmt > maxMPTokenAmount)
            return temMALFORMED;
    }
    return preflight2(ctx);
}

Expected<MPTID, TER>
MPTokenIssuanceCreate::create(
    ApplyView& view,
    beast::Journal journal,
    MPTCreateArgs const& args)
{
    auto const acct = view.peek(keylet::account(args.account));
    if (!acct)
        return Unexpected(tecINTERNAL);  // LCOV_EXCL_LINE

    if (args.priorBalance &&
        *(args.priorBalance) <
            view.fees().accountReserve((*acct)[sfOwnerCount] + 1))
        return Unexpected(tecINSUFFICIENT_RESERVE);

    auto const mptId = makeMptID(args.sequence, args.account);
    auto const mptIssuanceKeylet = keylet::mptIssuance(mptId);

    // create the MPTokenIssuance
    {
        auto const ownerNode = view.dirInsert(
            keylet::ownerDir(args.account),
            mptIssuanceKeylet,
            describeOwnerDir(args.account));

        if (!ownerNode)
            return Unexpected(tecDIR_FULL);  // LCOV_EXCL_LINE

        auto mptIssuance = std::make_shared<SLE>(mptIssuanceKeylet);
        (*mptIssuance)[sfFlags] = args.flags & ~tfUniversal;
        (*mptIssuance)[sfIssuer] = args.account;
        (*mptIssuance)[sfOutstandingAmount] = 0;
        (*mptIssuance)[sfOwnerNode] = *ownerNode;
        (*mptIssuance)[sfSequence] = args.sequence;

        if (args.maxAmount)
            (*mptIssuance)[sfMaximumAmount] = *args.maxAmount;

        if (args.assetScale)
            (*mptIssuance)[sfAssetScale] = *args.assetScale;

        if (args.transferFee)
            (*mptIssuance)[sfTransferFee] = *args.transferFee;

        if (args.metadata)
            (*mptIssuance)[sfMPTokenMetadata] = *args.metadata;

        if (args.domainId)
            (*mptIssuance)[sfDomainID] = *args.domainId;

        view.insert(mptIssuance);
    }

    // Update owner count.
    adjustOwnerCount(view, acct, 1, journal);

    return mptId;
}

TER
MPTokenIssuanceCreate::doApply()
{
    auto const& tx = ctx_.tx;
    auto const result = create(
        view(),
        j_,
        {
            .priorBalance = mPriorBalance,
            .account = account_,
            .sequence = tx.getSeqValue(),
            .flags = tx.getFlags(),
            .maxAmount = tx[~sfMaximumAmount],
            .assetScale = tx[~sfAssetScale],
            .transferFee = tx[~sfTransferFee],
            .metadata = tx[~sfMPTokenMetadata],
            .domainId = tx[~sfDomainID],
        });
    return result ? tesSUCCESS : result.error();
}

}  // namespace ripple
