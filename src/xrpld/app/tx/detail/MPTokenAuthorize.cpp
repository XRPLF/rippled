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

#include <xrpld/app/tx/detail/MPTokenAuthorize.h>

#include <xrpl/ledger/View.h>
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

    if (ctx.tx[sfAccount] == ctx.tx[~sfHolder])
        return temMALFORMED;

    return preflight2(ctx);
}

TER
MPTokenAuthorize::preclaim(PreclaimContext const& ctx)
{
    auto const accountID = ctx.tx[sfAccount];
    auto const holderID = ctx.tx[~sfHolder];

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

        // There is an edge case where all holders have zero balance, issuance
        // is legally destroyed, then outstanding MPT(s) are deleted afterwards.
        // Thus, there is no need to check for the existence of the issuance if
        // the MPT is being deleted with a zero balance. Check for unauthorize
        // before fetching the MPTIssuance object.

        // if holder wants to delete/unauthorize a mpt
        if (ctx.tx.getFlags() & tfMPTUnauthorize)
        {
            if (!sleMpt)
                return tecOBJECT_NOT_FOUND;

            if ((*sleMpt)[sfMPTAmount] != 0)
            {
                auto const sleMptIssuance = ctx.view.read(
                    keylet::mptIssuance(ctx.tx[sfMPTokenIssuanceID]));
                if (!sleMptIssuance)
                    return tefINTERNAL;

                return tecHAS_OBLIGATIONS;
            }

            if ((*sleMpt)[~sfLockedAmount].value_or(0) != 0)
            {
                auto const sleMptIssuance = ctx.view.read(
                    keylet::mptIssuance(ctx.tx[sfMPTokenIssuanceID]));
                if (!sleMptIssuance)
                    return tefINTERNAL;  // LCOV_EXCL_LINE

                return tecHAS_OBLIGATIONS;
            }
            if (ctx.view.rules().enabled(featureSingleAssetVault) &&
                sleMpt->isFlag(lsfMPTLocked))
                return tecNO_PERMISSION;

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
            return tecDUPLICATE;

        return tesSUCCESS;
    }

    if (!ctx.view.exists(keylet::account(*holderID)))
        return tecNO_DST;

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

    // The holder must create the MPT before the issuer can authorize it.
    if (!ctx.view.exists(
            keylet::mptoken(ctx.tx[sfMPTokenIssuanceID], *holderID)))
        return tecOBJECT_NOT_FOUND;

    return tesSUCCESS;
}

TER
MPTokenAuthorize::createMPToken(
    ApplyView& view,
    MPTID const& mptIssuanceID,
    AccountID const& account,
    std::uint32_t const flags)
{
    auto const mptokenKey = keylet::mptoken(mptIssuanceID, account);

    auto const ownerNode = view.dirInsert(
        keylet::ownerDir(account), mptokenKey, describeOwnerDir(account));

    if (!ownerNode)
        return tecDIR_FULL;  // LCOV_EXCL_LINE

    auto mptoken = std::make_shared<SLE>(mptokenKey);
    (*mptoken)[sfAccount] = account;
    (*mptoken)[sfMPTokenIssuanceID] = mptIssuanceID;
    (*mptoken)[sfFlags] = flags;
    (*mptoken)[sfOwnerNode] = *ownerNode;

    view.insert(mptoken);

    return tesSUCCESS;
}

TER
MPTokenAuthorize::checkCreateMPT(
    ripple::ApplyView& view,
    ripple::MPTIssue const& mptIssue,
    ripple::AccountID const& holder,
    beast::Journal j)
{
    if (mptIssue.getIssuer() == holder)
        return tesSUCCESS;

    auto const mptIssuanceID = keylet::mptIssuance(mptIssue.getMptID());
    auto const mptokenID = keylet::mptoken(mptIssuanceID.key, holder);
    if (!view.exists(mptokenID))
    {
        if (auto const err =
                createMPToken(view, mptIssue.getMptID(), holder, 0);
            err != tesSUCCESS)
            return err;
        auto const sleAcct = view.peek(keylet::account(holder));
        if (!sleAcct)
            return tecINTERNAL;
        adjustOwnerCount(view, sleAcct, 1, j);
    }
    return tesSUCCESS;
}

TER
MPTokenAuthorize::doApply()
{
    auto const& tx = ctx_.tx;
    return authorizeMPToken(
        ctx_.view(),
        mPriorBalance,
        tx[sfMPTokenIssuanceID],
        account_,
        ctx_.journal,
        tx.getFlags(),
        tx[~sfHolder]);
}

}  // namespace ripple
