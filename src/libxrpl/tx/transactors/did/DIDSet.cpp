#include <xrpl/tx/transactors/did/DIDSet.h>

#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

#include <cstddef>
#include <memory>

namespace xrpl {

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
    if (!ctx.tx.isFieldPresent(sfURI) && !ctx.tx.isFieldPresent(sfDIDDocument) &&
        !ctx.tx.isFieldPresent(sfData))
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

    if (isTooLong(sfURI, kMaxDidUriLength) || isTooLong(sfDIDDocument, kMaxDidDocumentLength) ||
        isTooLong(sfData, kMaxDidDataLength))
        return temMALFORMED;

    return tesSUCCESS;
}

static TER
addSLE(ApplyContext& ctx, SLE::ref sle, AccountID const& owner)
{
    auto const sleAccount = ctx.view().peek(keylet::account(owner));
    if (!sleAccount)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // Check reserve availability for new object creation
    {
        auto const balance = STAmount((*sleAccount)[sfBalance]).xrp();
        auto const reserve = ctx.view().fees().accountReserve((*sleAccount)[sfOwnerCount] + 1);

        if (balance < reserve)
            return tecINSUFFICIENT_RESERVE;
    }

    // Add ledger object to ledger
    ctx.view().insert(sle);

    // Add ledger object to owner's page
    {
        auto page =
            ctx.view().dirInsert(keylet::ownerDir(owner), sle->key(), describeOwnerDir(owner));
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
    Keylet const didKeylet = keylet::did(accountID_);
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

        if (!sleDID->isFieldPresent(sfURI) && !sleDID->isFieldPresent(sfDIDDocument) &&
            !sleDID->isFieldPresent(sfData))
        {
            return tecEMPTY_DID;
        }
        ctx_.view().update(sleDID);
        return tesSUCCESS;
    }

    // Create new ledger object otherwise
    auto const sleDID = std::make_shared<SLE>(didKeylet);
    (*sleDID)[sfAccount] = accountID_;

    auto set = [&](auto const& sField) {
        if (auto const field = ctx_.tx[~sField]; field && !field->empty())
            (*sleDID)[sField] = *field;
    };

    set(sfURI);
    set(sfDIDDocument);
    set(sfData);
    if (ctx_.view().rules().enabled(fixEmptyDID) && !sleDID->isFieldPresent(sfURI) &&
        !sleDID->isFieldPresent(sfDIDDocument) && !sleDID->isFieldPresent(sfData))
    {
        return tecEMPTY_DID;
    }

    return addSLE(ctx_, sleDID, accountID_);
}

void
DIDSet::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
DIDSet::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
