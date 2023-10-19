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

#include <ripple/app/tx/impl/DID.h>

#include <ripple/basics/Log.h>
#include <ripple/ledger/ApplyView.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/st.h>

namespace ripple {

/*
    DID
    ======

    Decentralized Identifiers (DIDs) are a new type of identifier that enable
    verifiable, self-sovereign digital identity and are designed to be
    compatible with any distributed ledger or network. This implementation
    conforms to the requirements specified in the DID v1.0 specification
    currently recommended by the W3C Credentials Community Group
    (https://www.w3.org/TR/did-core/).
*/

//------------------------------------------------------------------------------

NotTEC
DIDSet::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureDID))
        return temDISABLED;

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (!ctx.tx.isFieldPresent(sfURI) &&
        !ctx.tx.isFieldPresent(sfDIDDocument) && !ctx.tx.isFieldPresent(sfData))
        return temEMPTY_DID;

    if (ctx.tx.isFieldPresent(sfURI) && ctx.tx[sfURI].empty() &&
        ctx.tx.isFieldPresent(sfDIDDocument) && ctx.tx[sfDIDDocument].empty() &&
        ctx.tx.isFieldPresent(sfData) && ctx.tx[sfData].empty())
        return temEMPTY_DID;

    auto isTooLong = [&](auto const& sField, std::size_t length) -> bool {
        if (auto field = ctx.tx[~sField])
            return field->length() > length;
        return false;
    };

    if (isTooLong(sfURI, maxDIDURILength) ||
        isTooLong(sfDIDDocument, maxDIDDocumentLength) ||
        isTooLong(sfData, maxDIDAttestationLength))
        return temMALFORMED;

    return preflight2(ctx);
}

TER
addSLE(
    ApplyContext& ctx,
    std::shared_ptr<SLE> const& sle,
    AccountID const& owner)
{
    auto const sleAccount = ctx.view().peek(keylet::account(owner));
    if (!sleAccount)
        return tefINTERNAL;

    // Check reserve availability for new object creation
    {
        auto const balance = STAmount((*sleAccount)[sfBalance]).xrp();
        auto const reserve =
            ctx.view().fees().accountReserve((*sleAccount)[sfOwnerCount] + 1);

        if (balance < reserve)
            return tecINSUFFICIENT_RESERVE;
    }

    // Add ledger object to ledger
    ctx.view().insert(sle);

    // Add ledger object to owner's page
    {
        auto page = ctx.view().dirInsert(
            keylet::ownerDir(owner), sle->key(), describeOwnerDir(owner));
        if (!page)
            return tecDIR_FULL;
        (*sle)[sfOwnerNode] = *page;
    }
    adjustOwnerCount(ctx.view(), sleAccount, 1, ctx.journal);
    ctx.view().update(sleAccount);

    return tesSUCCESS;
}

TER
DIDSet::doApply()
{
    // Edit ledger object if it already exists
    Keylet const didKeylet = keylet::did(account_);
    if (auto const sleDID = ctx_.view().peek(didKeylet))
    {
        auto update = [&](auto const& sField) {
            if (auto const field = ctx_.tx[~sField])
            {
                if (field->empty())
                {
                    sleDID->makeFieldAbsent(sField);
                }
                else
                {
                    (*sleDID)[sField] = *field;
                }
            }
        };
        update(sfURI);
        update(sfDIDDocument);
        update(sfData);

        if (!sleDID->isFieldPresent(sfURI) &&
            !sleDID->isFieldPresent(sfDIDDocument) &&
            !sleDID->isFieldPresent(sfData))
        {
            return tecEMPTY_DID;
        }
        ctx_.view().update(sleDID);
        return tesSUCCESS;
    }

    // Create new ledger object otherwise
    auto const sleDID = std::make_shared<SLE>(didKeylet);
    (*sleDID)[sfAccount] = account_;

    auto set = [&](auto const& sField) {
        if (auto const field = ctx_.tx[~sField]; field && !field->empty())
            (*sleDID)[sField] = *field;
    };

    set(sfURI);
    set(sfDIDDocument);
    set(sfData);

    return addSLE(ctx_, sleDID, account_);
}

NotTEC
DIDDelete::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureDID))
        return temDISABLED;

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    return preflight2(ctx);
}

TER
DIDDelete::deleteSLE(ApplyContext& ctx, Keylet sleKeylet, AccountID const owner)
{
    auto const sle = ctx.view().peek(sleKeylet);
    if (!sle)
        return tecNO_ENTRY;

    return DIDDelete::deleteSLE(ctx.view(), sle, owner, ctx.journal);
}

TER
DIDDelete::deleteSLE(
    ApplyView& view,
    std::shared_ptr<SLE> sle,
    AccountID const owner,
    beast::Journal j)
{
    // Remove object from owner directory
    if (!view.dirRemove(
            keylet::ownerDir(owner), (*sle)[sfOwnerNode], sle->key(), true))
    {
        JLOG(j.fatal()) << "Unable to delete DID Token from owner.";
        return tefBAD_LEDGER;
    }

    auto const sleOwner = view.peek(keylet::account(owner));
    if (!sleOwner)
        return tecINTERNAL;

    adjustOwnerCount(view, sleOwner, -1, j);
    view.update(sleOwner);

    // Remove object from ledger
    view.erase(sle);
    return tesSUCCESS;
}

TER
DIDDelete::doApply()
{
    return deleteSLE(ctx_, keylet::did(account_), account_);
}

}  // namespace ripple
