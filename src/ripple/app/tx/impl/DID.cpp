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
        ctx.tx.isFieldPresent(sfDIDDocument) && ctx.tx[sfDIDDocument].empty())
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
DIDSet::preclaim(PreclaimContext const& ctx)
{
    if (!ctx.view.exists(keylet::did(ctx.tx[sfAccount])) &&
        !ctx.tx.isFieldPresent(sfURI) && !ctx.tx.isFieldPresent(sfDIDDocument))
    {
        // Need either the URI or document if the account doesn't already have a
        // DID
        return tecEMPTY_DID;
    }

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
            !sleDID->isFieldPresent(sfDIDDocument))
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

    return addSLE(ctx_.view(), sleDID, account_, ctx_.journal);
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
DIDDelete::doApply()
{
    return deleteSLE(
        ctx_.view(), keylet::did(account_), account_, ctx_.journal);
}

}  // namespace ripple
