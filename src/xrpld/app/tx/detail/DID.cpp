#include <xrpld/app/tx/detail/DID.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TxFlags.h>

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

    return tesSUCCESS;
}

TER
addSLE(
    ApplyContext& ctx,
    std::shared_ptr<SLE> const& sle,
    AccountID const& owner)
{
    auto const sleAccount = ctx.view().peek(keylet::account(owner));
    if (!sleAccount)
        return tefINTERNAL;  // LCOV_EXCL_LINE

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
            return tecDIR_FULL;  // LCOV_EXCL_LINE
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
    if (ctx_.view().rules().enabled(fixEmptyDID) &&
        !sleDID->isFieldPresent(sfURI) &&
        !sleDID->isFieldPresent(sfDIDDocument) &&
        !sleDID->isFieldPresent(sfData))
    {
        return tecEMPTY_DID;
    }

    return addSLE(ctx_, sleDID, account_);
}

NotTEC
DIDDelete::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
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
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Unable to delete DID Token from owner.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    auto const sleOwner = view.peek(keylet::account(owner));
    if (!sleOwner)
        return tecINTERNAL;  // LCOV_EXCL_LINE

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
