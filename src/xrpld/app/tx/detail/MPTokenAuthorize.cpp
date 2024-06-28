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

#include <xrpld/app/tx/detail/MPTokenAuthorize.h>
#include <xrpld/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/st.h>

namespace ripple {

NotTEC
MPTokenAuthorize::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureMPTokensV1))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfMPTokenAuthorizeMask)
        return temINVALID_FLAG;

    if (ctx.tx[sfAccount] == ctx.tx[~sfMPTokenHolder])
        return temMALFORMED;

    return preflight2(ctx);
}

TER
MPTokenAuthorize::preclaim(PreclaimContext const& ctx)
{
    auto const accountID = ctx.tx[sfAccount];
    auto const holderID = ctx.tx[~sfMPTokenHolder];

    if (holderID && !(ctx.view.exists(keylet::account(*holderID))))
        return tecNO_DST;

    // if non-issuer account submits this tx, then they are trying either:
    // 1. Unauthorize/delete MPToken
    // 2. Use/create MPToken
    //
    // Note: `accountID` is holder's account
    //       `holderID` is NOT used
    if (!holderID)
    {
        std::shared_ptr<SLE const> sleMpt = ctx.view.read(
            keylet::mptoken(ctx.tx[sfMPTokenIssuanceID], accountID));

        // There is an edge case where holder deletes MPT after issuance has
        // already been destroyed. So we must check for unauthorize before
        // fetching the MPTIssuance object(since it doesn't exist)

        // if holder wants to delete/unauthorize a mpt
        if (ctx.tx.getFlags() & tfMPTUnauthorize)
        {
            if (!sleMpt)
                return tecOBJECT_NOT_FOUND;

            if ((*sleMpt)[sfMPTAmount] != 0)
                return tecHAS_OBLIGATIONS;

            return tesSUCCESS;
        }

        // Now test when the holder wants to hold/create/authorize a new MPT
        auto const sleMptIssuance =
            ctx.view.read(keylet::mptIssuance(ctx.tx[sfMPTokenIssuanceID]));

        if (!sleMptIssuance)
            return tecOBJECT_NOT_FOUND;

        if (accountID == (*sleMptIssuance)[sfIssuer])
            return tecNO_PERMISSION;

        // if holder wants to use and create a mpt
        if (sleMpt)
            return tecMPTOKEN_EXISTS;

        return tesSUCCESS;
    }

    auto const sleMptIssuance =
        ctx.view.read(keylet::mptIssuance(ctx.tx[sfMPTokenIssuanceID]));
    if (!sleMptIssuance)
        return tecOBJECT_NOT_FOUND;

    std::uint32_t const mptIssuanceFlags = sleMptIssuance->getFieldU32(sfFlags);

    // If tx is submitted by issuer, they would either try to do the following
    // for allowlisting:
    // 1. authorize an account
    // 2. unauthorize an account
    //
    // Note: `accountID` is issuer's account
    //       `holderID` is holder's account
    if (accountID != (*sleMptIssuance)[sfIssuer])
        return tecNO_PERMISSION;

    // If tx is submitted by issuer, it only applies for MPT with
    // lsfMPTRequireAuth set
    if (!(mptIssuanceFlags & lsfMPTRequireAuth))
        return tecNO_AUTH;

    if (!ctx.view.exists(
            keylet::mptoken(ctx.tx[sfMPTokenIssuanceID], *holderID)))
        return tecOBJECT_NOT_FOUND;

    return tesSUCCESS;
}

TER
MPTokenAuthorize::authorize(
    ApplyView& view,
    beast::Journal journal,
    MPTAuthorizeArgs const& args)
{
    auto const sleAcct = view.peek(keylet::account(args.account));
    if (!sleAcct)
        return tecINTERNAL;

    // If the account that submitted the tx is a holder
    // Note: `account_` is holder's account
    //       `holderID` is NOT used
    if (!args.holderID)
    {
        // When a holder wants to unauthorize/delete a MPT, the ledger must
        //      - delete mptokenKey from owner directory
        //      - delete the MPToken
        if (args.flags & tfMPTUnauthorize)
        {
            auto const mptokenKey =
                keylet::mptoken(args.mptIssuanceID, args.account);
            auto const sleMpt = view.peek(mptokenKey);
            if (!sleMpt)
                return tecINTERNAL;

            if (!view.dirRemove(
                    keylet::ownerDir(args.account),
                    (*sleMpt)[sfOwnerNode],
                    sleMpt->key(),
                    false))
                return tecINTERNAL;

            adjustOwnerCount(view, sleAcct, -1, journal);

            view.erase(sleMpt);
            return tesSUCCESS;
        }

        // A potential holder wants to authorize/hold a mpt, the ledger must:
        //      - add the new mptokenKey to the owner directory
        //      - create the MPToken object for the holder
        std::uint32_t const uOwnerCount = sleAcct->getFieldU32(sfOwnerCount);
        XRPAmount const reserveCreate(
            (uOwnerCount < 2) ? XRPAmount(beast::zero)
                              : view.fees().accountReserve(uOwnerCount + 1));

        if (args.priorBalance < reserveCreate)
            return tecINSUFFICIENT_RESERVE;

        auto const mptokenKey =
            keylet::mptoken(args.mptIssuanceID, args.account);

        auto const ownerNode = view.dirInsert(
            keylet::ownerDir(args.account),
            mptokenKey,
            describeOwnerDir(args.account));

        if (!ownerNode)
            return tecDIR_FULL;

        auto mptoken = std::make_shared<SLE>(mptokenKey);
        (*mptoken)[sfAccount] = args.account;
        (*mptoken)[sfMPTokenIssuanceID] = args.mptIssuanceID;
        (*mptoken)[sfFlags] = 0;
        (*mptoken)[sfOwnerNode] = *ownerNode;
        view.insert(mptoken);

        // Update owner count.
        adjustOwnerCount(view, sleAcct, 1, journal);

        return tesSUCCESS;
    }

    auto const sleMptIssuance =
        view.read(keylet::mptIssuance(args.mptIssuanceID));
    if (!sleMptIssuance)
        return tecINTERNAL;

    // If the account that submitted this tx is the issuer of the MPT
    // Note: `account_` is issuer's account
    //       `holderID` is holder's account
    if (args.account != (*sleMptIssuance)[sfIssuer])
        return tecINTERNAL;

    auto const sleMpt =
        view.peek(keylet::mptoken(args.mptIssuanceID, *args.holderID));
    if (!sleMpt)
        return tecINTERNAL;

    std::uint32_t const flagsIn = sleMpt->getFieldU32(sfFlags);
    std::uint32_t flagsOut = flagsIn;

    // Issuer wants to unauthorize the holder, unset lsfMPTAuthorized on
    // their MPToken
    if (args.flags & tfMPTUnauthorize)
        flagsOut &= ~lsfMPTAuthorized;
    // Issuer wants to authorize a holder, set lsfMPTAuthorized on their
    // MPToken
    else
        flagsOut |= lsfMPTAuthorized;

    if (flagsIn != flagsOut)
        sleMpt->setFieldU32(sfFlags, flagsOut);

    view.update(sleMpt);
    return tesSUCCESS;
}

TER
MPTokenAuthorize::doApply()
{
    auto const& tx = ctx_.tx;
    return authorize(
        ctx_.view(),
        ctx_.journal,
        {.priorBalance = mPriorBalance,
         .mptIssuanceID = tx[sfMPTokenIssuanceID],
         .account = account_,
         .flags = tx.getFlags(),
         .holderID = tx[~sfMPTokenHolder]});
}

}  // namespace ripple
