#include <xrpl/tx/transactors/permissioned_domain/PermissionedDomainSet.h>

#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <memory>
#include <utility>

namespace xrpl {

bool
PermissionedDomainSet::checkExtraFeatures(PreflightContext const& ctx)
{
    return ctx.rules.enabled(featureCredentials);
}

NotTEC
PermissionedDomainSet::preflight(PreflightContext const& ctx)
{
    if (auto err = credentials::checkArray(
            ctx.tx.getFieldArray(sfAcceptedCredentials),
            kMaxPermissionedDomainCredentialsArraySize,
            ctx.j);
        !isTesSuccess(err))
        return err;

    auto const domain = ctx.tx.at(~sfDomainID);
    if (domain && *domain == beast::kZero)
        return temMALFORMED;

    return tesSUCCESS;
}

TER
PermissionedDomainSet::preclaim(PreclaimContext const& ctx)
{
    auto const account = ctx.tx.getAccountID(sfAccount);

    if (!ctx.view.exists(keylet::account(account)))
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const& credentials = ctx.tx.getFieldArray(sfAcceptedCredentials);
    for (auto const& credential : credentials)
    {
        if (!ctx.view.exists(keylet::account(credential.getAccountID(sfIssuer))))
            return tecNO_ISSUER;
    }

    if (ctx.tx.isFieldPresent(sfDomainID))
    {
        auto const sleDomain =
            ctx.view.read(keylet::permissionedDomain(ctx.tx.getFieldH256(sfDomainID)));
        if (!sleDomain)
            return tecNO_ENTRY;
        if (sleDomain->getAccountID(sfOwner) != account)
            return tecNO_PERMISSION;
    }

    return tesSUCCESS;
}

/** Attempt to create the Permissioned Domain. */
TER
PermissionedDomainSet::doApply()
{
    auto const ownerSle = view().peek(keylet::account(accountID_));
    if (!ownerSle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const sortedTxCredentials =
        credentials::makeSorted(ctx_.tx.getFieldArray(sfAcceptedCredentials));
    STArray sortedLE(sfAcceptedCredentials, sortedTxCredentials.size());
    for (auto const& p : sortedTxCredentials)
    {
        auto cred = STObject::makeInnerObject(sfCredential);
        cred.setAccountID(sfIssuer, p.first);
        cred.setFieldVL(sfCredentialType, p.second);
        sortedLE.pushBack(std::move(cred));
    }

    if (ctx_.tx.isFieldPresent(sfDomainID))
    {
        // Modify existing permissioned domain.
        auto slePd = view().peek(keylet::permissionedDomain(ctx_.tx.getFieldH256(sfDomainID)));
        if (!slePd)
            return tefINTERNAL;  // LCOV_EXCL_LINE
        slePd->peekFieldArray(sfAcceptedCredentials) = std::move(sortedLE);
        view().update(slePd);
    }
    else
    {
        // Create new permissioned domain.
        // Check reserve availability for new object creation
        auto const balance = STAmount((*ownerSle)[sfBalance]).xrp();
        auto const reserve = ctx_.view().fees().accountReserve((*ownerSle)[sfOwnerCount] + 1);
        if (balance < reserve)
            return tecINSUFFICIENT_RESERVE;

        bool const fixEnabled = view().rules().enabled(fixCleanup3_1_3);
        auto const seq = fixEnabled ? ctx_.tx.getSeqValue() : ctx_.tx.getFieldU32(sfSequence);
        Keylet const pdKeylet = keylet::permissionedDomain(accountID_, seq);
        auto slePd = std::make_shared<SLE>(pdKeylet);

        slePd->setAccountID(sfOwner, accountID_);
        slePd->setFieldU32(sfSequence, seq);
        slePd->peekFieldArray(sfAcceptedCredentials) = std::move(sortedLE);
        auto const page =
            view().dirInsert(keylet::ownerDir(accountID_), pdKeylet, describeOwnerDir(accountID_));
        if (!page)
            return tecDIR_FULL;  // LCOV_EXCL_LINE

        slePd->setFieldU64(sfOwnerNode, *page);
        // If we succeeded, the new entry counts against the creator's reserve.
        adjustOwnerCount(view(), ownerSle, 1, ctx_.journal);
        view().insert(slePd);
    }

    return tesSUCCESS;
}

void
PermissionedDomainSet::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
PermissionedDomainSet::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
