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

#include <ripple/app/tx/impl/CFTokenAuthorize.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/st.h>

namespace ripple {

NotTEC
CFTokenAuthorize::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureCFTokensV1))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfCFTokenAuthorizeMask)
        return temINVALID_FLAG;

    if (ctx.tx[sfAccount] == ctx.tx[~sfCFTokenHolder])
        return temMALFORMED;

    return preflight2(ctx);
}

TER
CFTokenAuthorize::preclaim(PreclaimContext const& ctx)
{
    auto const sleCftIssuance =
        ctx.view.read(keylet::cftIssuance(ctx.tx[sfCFTokenIssuanceID]));
    if (!sleCftIssuance)
        return tecOBJECT_NOT_FOUND;

    auto const accountID = ctx.tx[sfAccount];
    auto const txFlags = ctx.tx.getFlags();
    auto const holderID = ctx.tx[~sfCFTokenHolder];

    if (holderID && !(ctx.view.exists(keylet::account(*holderID))))
        return tecNO_DST;

    std::uint32_t const cftIssuanceFlags = sleCftIssuance->getFieldU32(sfFlags);

    // If tx is submitted by issuer, they would either try to do the following
    // for allow-listing:
    // 1. authorize an account
    // 2. unauthorize an account
    //
    // Note: `accountID` is issuer's account
    //       `holderID` is holder's account
    if (accountID == (*sleCftIssuance)[sfIssuer])
    {
        // If tx is submitted by issuer, it only applies for CFT with
        // lsfCFTRequireAuth set
        if (!(cftIssuanceFlags & lsfCFTRequireAuth))
            return tecNO_AUTH;

        if (!holderID)
            return temMALFORMED;

        if (!ctx.view.exists(
                keylet::cftoken(ctx.tx[sfCFTokenIssuanceID], *holderID)))
            return tecNO_ENTRY;

        return tesSUCCESS;
    }

    // if non-issuer account submits this tx, then they are trying either:
    // 1. Unauthorize/delete CFToken
    // 2. Use/create CFToken
    //
    // Note: `accountID` is holder's account
    //       `holderID` is NOT used
    if (holderID)
        return temMALFORMED;

    std::shared_ptr<SLE const> sleCft =
        ctx.view.read(keylet::cftoken(ctx.tx[sfCFTokenIssuanceID], accountID));

    // if holder wants to delete/unauthorize a cft
    if (txFlags & tfCFTUnauthorize)
    {
        if (!sleCft)
            return tecNO_ENTRY;

        if ((*sleCft)[sfCFTAmount] != 0)
            return tecHAS_OBLIGATIONS;
    }
    // if holder wants to use and create a cft
    else if (sleCft)
            return tecCFTOKEN_EXISTS;

    return tesSUCCESS;
}

TER
CFTokenAuthorize::doApply()
{
    auto const cftIssuanceID = ctx_.tx[sfCFTokenIssuanceID];
    auto const sleCftIssuance = view().read(keylet::cftIssuance(cftIssuanceID));
    if (!sleCftIssuance)
        return tecINTERNAL;

    auto const sleAcct = view().peek(keylet::account(account_));
    if (!sleAcct)
        return tecINTERNAL;

    auto const holderID = ctx_.tx[~sfCFTokenHolder];
    auto const txFlags = ctx_.tx.getFlags();

    // If the account that submitted this tx is the issuer of the CFT
    // Note: `account_` is issuer's account
    //       `holderID` is holder's account
    if (account_ == (*sleCftIssuance)[sfIssuer])
    {
        if (!holderID)
            return tecINTERNAL;

        auto const sleCft =
            view().peek(keylet::cftoken(cftIssuanceID, *holderID));
        if (!sleCft)
            return tecINTERNAL;

        std::uint32_t const flagsIn = sleCft->getFieldU32(sfFlags);
        std::uint32_t flagsOut = flagsIn;

        // Issuer wants to unauthorize the holder, unset lsfCFTAuthorized on
        // their CFToken
        if (txFlags & tfCFTUnauthorize)
            flagsOut &= ~lsfCFTAuthorized;
        // Issuer wants to authorize a holder, set lsfCFTAuthorized on their
        // CFToken
        else
            flagsOut |= lsfCFTAuthorized;

        if (flagsIn != flagsOut)
            sleCft->setFieldU32(sfFlags, flagsOut);

        view().update(sleCft);
        return tesSUCCESS;
    }

    // If the account that submitted the tx is a holder
    // Note: `account_` is holder's account
    //       `holderID` is NOT used
    if (holderID)
        return tecINTERNAL;

    // When a holder wants to unauthorize/delete a CFT, the ledger must
    //      - delete cftokenKey from both owner and cft directories
    //      - delete the CFToken
    if (txFlags & tfCFTUnauthorize)
    {
        auto const cftokenKey = keylet::cftoken(cftIssuanceID, account_);
        auto const sleCft = view().peek(cftokenKey);
        if (!sleCft)
            return tecINTERNAL;

        if (!view().dirRemove(
                keylet::ownerDir(account_),
                (*sleCft)[sfOwnerNode],
                sleCft->key(),
                false))
            return tecINTERNAL;

        if (!view().dirRemove(
                keylet::cft_dir(cftIssuanceID),
                (*sleCft)[sfCFTokenNode],
                sleCft->key(),
                false))
            return tecINTERNAL;

        adjustOwnerCount(
            view(), sleAcct, -1, j_);

        view().erase(sleCft);
        return tesSUCCESS;
    }

    // A potential holder wants to authorize/hold a cft, the ledger must:
    //      - add the new cftokenKey to both the owner and cft directries
    //      - create the CFToken object for the holder
    std::uint32_t const uOwnerCount = sleAcct->getFieldU32(sfOwnerCount);
    XRPAmount const reserveCreate(
        (uOwnerCount < 2) ? XRPAmount(beast::zero)
                          : view().fees().accountReserve(uOwnerCount + 1));

    if (mPriorBalance < reserveCreate)
        return tecINSUFFICIENT_RESERVE;

    auto const cftokenKey = keylet::cftoken(cftIssuanceID, account_);

    auto const ownerNode = view().dirInsert(
        keylet::ownerDir(account_), cftokenKey, describeOwnerDir(account_));

    if (!ownerNode)
        return tecDIR_FULL;

    auto const cftNode = view().dirInsert(
        keylet::cft_dir(cftIssuanceID),
        cftokenKey,
        [&cftIssuanceID](std::shared_ptr<SLE> const& sle) {
            (*sle)[sfCFTokenIssuanceID] = cftIssuanceID;
        });

    if (!cftNode)
        return tecDIR_FULL;

    auto cftoken = std::make_shared<SLE>(cftokenKey);
    (*cftoken)[sfAccount] = account_;
    (*cftoken)[sfCFTokenIssuanceID] = cftIssuanceID;
    (*cftoken)[sfFlags] = 0;
    (*cftoken)[sfOwnerNode] = *ownerNode;
    (*cftoken)[sfCFTokenNode] = *cftNode;
    view().insert(cftoken);

    // Update owner count.
    adjustOwnerCount(view(), sleAcct, 1, j_);

    return tesSUCCESS;
}

}  // namespace ripple
