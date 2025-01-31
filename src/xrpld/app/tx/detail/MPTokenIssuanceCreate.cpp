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
#include <xrpl/protocol/st.h>

namespace ripple {

NotTEC
MPTokenIssuanceCreate::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureMPTokensV1))
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
    AccountID const& account,
    std::uint32_t sequence,
    std::uint32_t flags,
    std::optional<std::uint64_t> maxAmount,
    std::optional<std::uint8_t> assetScale,
    std::optional<std::uint16_t> transferFee,
    std::optional<Slice> const& metadata,
    std::optional<uint256> domainId)
{
    auto const acct = view.peek(keylet::account(account));
    if (!acct)
        return Unexpected(tecINTERNAL);

    auto mptId = makeMptID(sequence, account);
    auto const mptIssuanceKeylet = keylet::mptIssuance(mptId);

    // create the MPTokenIssuance
    {
        auto const ownerNode = view.dirInsert(
            keylet::ownerDir(account),
            mptIssuanceKeylet,
            describeOwnerDir(account));

        if (!ownerNode)
            return Unexpected(tecDIR_FULL);

        auto mptIssuance = std::make_shared<SLE>(mptIssuanceKeylet);
        (*mptIssuance)[sfFlags] = flags & ~tfUniversal;
        (*mptIssuance)[sfIssuer] = account;
        (*mptIssuance)[sfOutstandingAmount] = 0;
        (*mptIssuance)[sfOwnerNode] = *ownerNode;
        (*mptIssuance)[sfSequence] = sequence;

        if (maxAmount)
            (*mptIssuance)[sfMaximumAmount] = *maxAmount;

        if (assetScale)
            (*mptIssuance)[sfAssetScale] = *assetScale;

        if (transferFee)
            (*mptIssuance)[sfTransferFee] = *transferFee;

        if (metadata)
            (*mptIssuance)[sfMPTokenMetadata] = *metadata;

        if (domainId)
            (*mptIssuance)[sfDomainID] = *domainId;

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

    auto const acct = view().peek(keylet::account(account_));
    if (mPriorBalance < view().fees().accountReserve((*acct)[sfOwnerCount] + 1))
        return tecINSUFFICIENT_RESERVE;

    auto result = create(
        view(),
        j_,
        account_,
        tx.getSeqProxy().value(),
        tx.getFlags(),
        tx[~sfMaximumAmount],
        tx[~sfAssetScale],
        tx[~sfTransferFee],
        tx[~sfMPTokenMetadata]);
    return result ? tesSUCCESS : result.error();
}

}  // namespace ripple
