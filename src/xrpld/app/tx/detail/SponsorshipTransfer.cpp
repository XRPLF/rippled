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

#include <xrpld/app/tx/detail/SponsorshipTransfer.h>
#include <xrpld/ledger/ReadView.h>
#include <xrpld/ledger/View.h>

#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
SponsorshipTransfer::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureSponsor))
        return temDISABLED;

    if (auto const ter = preflight1(ctx))
        return ter;

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    return preflight2(ctx);
}

template <typename T>
inline std::optional<AccountID>
getLedgerEntryOwner(
    ReadView const& view,
    T const& sle,
    AccountID const& account)
{
    switch (sle->getType())
    {
        case ltNFTOKEN_OFFER:
        case ltORACLE:
        case ltPERMISSIONED_DOMAIN:
        case ltVAULT:
            return sle->getAccountID(sfOwner);
        case ltCHECK:
        case ltDID:
        case ltTICKET:
        case ltOFFER:
        case ltXCHAIN_OWNED_CLAIM_ID:
        case ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID:
        case ltESCROW:
        case ltPAYCHAN:
        case ltMPTOKEN:
        case ltDELEGATE:
        case ltBRIDGE:
        case ltDEPOSIT_PREAUTH:
            return sle->getAccountID(sfAccount);
        case ltMPTOKEN_ISSUANCE:
            return sle->getAccountID(sfIssuer);
        case ltSIGNER_LIST: {
            auto const signerList = view.read(keylet::signers(account));
            if (!signerList)
                return std::nullopt;
            if (signerList->getFieldH256(sfLedgerIndex) ==
                sle->getFieldH256(sfLedgerIndex))
                return account;
            return std::nullopt;
        }
        case ltCREDENTIAL: {
            if (sle->isFlag(lsfAccepted))
                return sle->getAccountID(sfSubject);
            return sle->getAccountID(sfIssuer);
        }
        case ltNFTOKEN_PAGE: {
            // the upper 20 bytes of the index of ltNFTokenPage are the Owner's
            // AccountID
            uint256 const& key = sle->key();
            return AccountID::fromVoid(key.data());
        }
            // case ltRIPPLE_STATE:
        case ltACCOUNT_ROOT: {
            // AccountRoot is not supported for object sponsorship
            return std::nullopt;
        }
        case ltNEGATIVE_UNL:
        case ltDIR_NODE:
        case ltAMENDMENTS:
        case ltLEDGER_HASHES:
        case ltFEE_SETTINGS:
        case ltAMM:
            return std::nullopt;
        default:
            return std::nullopt;
    };
}

TER
SponsorshipTransfer::preclaim(PreclaimContext const& ctx)
{
    auto const index = ctx.tx[~sfObjectID];
    auto const newSponsor = getTxReserveSponsor(ctx.view, ctx.tx);

    bool const isObjectSponsor = index != std::nullopt;

    auto const accSle = ctx.view.read(keylet::account(ctx.tx[sfAccount]));
    if (!accSle)
        return tecINTERNAL;

    if (isObjectSponsor)
    {
        auto const sle = ctx.view.read(keylet::unchecked(*index));
        if (!sle)
            return tecNO_ENTRY;

        auto const owner =
            getLedgerEntryOwner(ctx.view, sle, ctx.tx[sfAccount]);
        if (!owner)
            return tecNO_PERMISSION;

        if (newSponsor)
        {
            if (sle->isFieldPresent(sfSponsorAccount))
            {
                // transfer sponsor
                if ((*newSponsor)->getAccountID(sfAccount) == owner)
                    return tecNO_PERMISSION;
            }
        }
        else
        {
            // dissolve sponsor
            // check object is sponsored
            if (!sle->isFieldPresent(sfSponsorAccount))
                return tecNO_PERMISSION;
        }

        // check account have sufficient balance
        if (auto const ter = checkInsufficientReserve(
                ctx.view,
                accSle,
                accSle->getFieldAmount(sfBalance),
                newSponsor,
                // TODO: address variable ownerCount like PriceOracle
                1);
            !isTesSuccess(ter))
            return ter;
    }
    else
    {
        if (newSponsor)
        {
            if (accSle->isFieldPresent(sfSponsorAccount))
            {
                // check not same account
                if ((*newSponsor)->getAccountID(sfAccount) ==
                    accSle->getAccountID(sfAccount))
                    return tecNO_PERMISSION;
            }
        }
        else
        {
            // dissolve sponsor
            // check account is sponsored
            if (!accSle->isFieldPresent(sfSponsorAccount))
                return tecNO_PERMISSION;
        }

        // check account have sufficient balance
        if (auto const ter = checkInsufficientReserve(
                ctx.view,
                accSle,
                accSle->getFieldAmount(sfBalance),
                newSponsor,
                0,
                1);
            !isTesSuccess(ter))
            return ter;
    }

    return tesSUCCESS;
}

TER
SponsorshipTransfer::doApply()
{
    auto const& tx = ctx_.tx;

    auto const index = tx[~sfObjectID];
    bool const isObjectSponsor = index != std::nullopt;

    auto const accSle = view().peek(keylet::account(account_));
    if (!accSle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    if (isObjectSponsor)
    {
        // transfer object sponsor
        auto const objSle = view().peek(keylet::unchecked(*index));
        if (!objSle)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        auto const owner = getLedgerEntryOwner(view(), objSle, account_);
        if (!owner)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        auto const ownerSle = view().peek(keylet::account(*owner));
        if (!ownerSle)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        if (tx.isFieldPresent(sfSponsor))
        {
            auto const sponsorObj = tx.getFieldObject(sfSponsor);
            auto const oldSponsor = objSle->getAccountID(sfSponsorAccount);
            auto const newSponsor = sponsorObj[sfAccount];
            // decrement old sponsoring count if exists
            if (auto const oldSponsorSle =
                    view().peek(keylet::account(oldSponsor)))
            {
                auto const newCount =
                    oldSponsorSle->getFieldU32(sfSponsoringOwnerCount) - 1;
                if (newCount == 0)
                    oldSponsorSle->makeFieldAbsent(sfSponsoringOwnerCount);
                else
                    oldSponsorSle->setFieldU32(
                        sfSponsoringOwnerCount, newCount);

                view().update(oldSponsorSle);
            }
            else
            {
                // update owner's sponsored count
                ownerSle->setFieldU32(
                    sfSponsoredOwnerCount,
                    ownerSle->getFieldU32(sfSponsoredOwnerCount) + 1);
                view().update(ownerSle);
            }
            // increment new sponsoring count
            auto const newSponsorSle = view().peek(keylet::account(newSponsor));
            newSponsorSle->setFieldU32(
                sfSponsoringOwnerCount,
                newSponsorSle->getFieldU32(sfSponsoringOwnerCount) + 1);
            view().update(newSponsorSle);

            objSle->setAccountID(sfSponsorAccount, newSponsor);
            view().update(objSle);
        }
        else
        {
            // dissolve object sponsor
            auto const oldSponsor = objSle->getAccountID(sfSponsorAccount);
            // decrement sponsored count
            auto const newCount =
                accSle->getFieldU32(sfSponsoredOwnerCount) - 1;
            if (newCount == 0)
                accSle->makeFieldAbsent(sfSponsoredOwnerCount);
            else
                accSle->setFieldU32(sfSponsoredOwnerCount, newCount);

            view().update(accSle);
            // decrement old sponsoring count
            if (auto const oldSponsorSle =
                    view().peek(keylet::account(oldSponsor)))
            {
                oldSponsorSle->setFieldU32(
                    sfSponsoringOwnerCount,
                    oldSponsorSle->getFieldU32(sfSponsoringOwnerCount) - 1);
                view().update(oldSponsorSle);
            }

            // remove sponsor from object
            objSle->makeFieldAbsent(sfSponsorAccount);
            view().update(objSle);
        }
    }
    else
    {
        // Transfer Account sponsor
        if (tx.isFieldPresent(sfSponsor))
        {
            // transfer account sponsor
            auto const sponsorObj = tx.getFieldObject(sfSponsor);
            // increment new sponsoring count
            auto const newSponsor = sponsorObj[sfAccount];
            auto const newSponsorSle = view().peek(keylet::account(newSponsor));
            newSponsorSle->setFieldU32(
                sfSponsoringAccountCount,
                newSponsorSle->getFieldU32(sfSponsoringAccountCount) + 1);
            view().update(newSponsorSle);
            // decrement old sponsoring count
            if (accSle->isFieldPresent(sfSponsorAccount))
            {
                auto const oldSponsor = accSle->getAccountID(sfSponsorAccount);
                auto const oldSponsorSle =
                    view().peek(keylet::account(oldSponsor));
                oldSponsorSle->setFieldU32(
                    sfSponsoringAccountCount,
                    oldSponsorSle->getFieldU32(sfSponsoringAccountCount) - 1);
                view().update(oldSponsorSle);
            }
            accSle->setAccountID(sfSponsorAccount, newSponsor);
            view().update(accSle);
        }
        else
        {
            // dissolve account sponsor
            auto const oldSponsor = accSle->getAccountID(sfSponsorAccount);
            accSle->makeFieldAbsent(sfSponsorAccount);
            // decrement account sponsoring count
            auto const oldSponsorSle = view().peek(keylet::account(oldSponsor));
            oldSponsorSle->setFieldU32(
                sfSponsoringAccountCount,
                oldSponsorSle->getFieldU32(sfSponsoringAccountCount) - 1);
            view().update(oldSponsorSle);
        }
    }

    return tesSUCCESS;
}

}  // namespace ripple
