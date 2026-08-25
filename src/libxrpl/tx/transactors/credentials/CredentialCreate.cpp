#include <xrpl/tx/transactors/credentials/CredentialCreate.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>  // IWYU pragma: keep
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <chrono>
#include <cstdint>
#include <memory>

namespace xrpl {

/*
    Credentials
    ======

   A verifiable credentials (VC
   https://en.wikipedia.org/wiki/Verifiable_credentials), as defined by the W3C
   specification (https://www.w3.org/TR/vc-data-model-2.0/), is a
   secure and tamper-evident way to represent information about a subject, such
   as an individual, organization, or even an IoT device. These credentials are
   issued by a trusted entity and can be verified by third parties without
   directly involving the issuer at all.
*/

using namespace credentials;

std::uint32_t
CredentialCreate::getFlagsMask(PreflightContext const& ctx)
{
    // 0 means "Allow any flags"
    return ctx.rules.enabled(fixInvalidTxFlags) ? tfUniversalMask : 0;
}

NotTEC
CredentialCreate::preflight(PreflightContext const& ctx)
{
    auto const& tx = ctx.tx;

    if (!tx[sfSubject])
    {
        return {temMALFORMED, "Malformed transaction: Invalid Subject"};
    }

    auto const uri = tx[~sfURI];
    if (uri && (uri->empty() || (uri->size() > kMaxCredentialUriLength)))
    {
        return {temMALFORMED, "Malformed transaction: invalid size of URI."};
    }

    auto const credType = tx[sfCredentialType];
    if (credType.empty() || (credType.size() > kMaxCredentialTypeLength))
    {
        return {temMALFORMED, "Malformed transaction: invalid size of CredentialType."};
    }

    return tesSUCCESS;
}

TER
CredentialCreate::preclaim(PreclaimContext const& ctx)
{
    auto const credType(ctx.tx[sfCredentialType]);
    auto const subject = ctx.tx[sfSubject];

    auto const subjectSle = ctx.view.read(keylet::account(subject));

    if (!subjectSle)
    {
        return {tecNO_TARGET, "Subject doesn't exist."};
    }

    if (ctx.view.exists(keylet::credential(subject, ctx.tx[sfAccount], credType)))
    {
        return {tecDUPLICATE, "Credential already exists."};
    }

    if (ctx.view.rules().enabled(fixCleanup3_3_0) && isPseudoAccount(subjectSle))
    {
        JLOG(ctx.j.trace()) << "Subject is a pseudo-account.";
        return tecPSEUDO_ACCOUNT;
    }

    return tesSUCCESS;
}

TER
CredentialCreate::doApply()
{
    auto const subject = ctx_.tx[sfSubject];
    auto const credType(ctx_.tx[sfCredentialType]);
    Keylet const credentialKey = keylet::credential(subject, accountID_, credType);

    auto const sleCred = std::make_shared<SLE>(credentialKey);
    if (!sleCred)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const optExp = ctx_.tx[~sfExpiration];
    if (optExp)
    {
        std::uint32_t const closeTime =
            ctx_.view().header().parentCloseTime.time_since_epoch().count();

        if (closeTime > *optExp)
        {
            return {tecEXPIRED, "Malformed transaction: Expiration time is in the past."};
        }

        sleCred->setFieldU32(sfExpiration, *optExp);
    }

    auto const sleIssuer = view().peek(keylet::account(accountID_));
    if (!sleIssuer)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    if (auto const ret = checkReserve(
            ctx_.getApplyViewContext(), sleIssuer, preFeeBalance_, {.ownerCountDelta = 1}, j_);
        !isTesSuccess(ret))
        return ret;

    sleCred->setAccountID(sfSubject, subject);
    sleCred->setAccountID(sfIssuer, accountID_);
    sleCred->setFieldVL(sfCredentialType, credType);

    if (ctx_.tx.isFieldPresent(sfURI))
        sleCred->setFieldVL(sfURI, ctx_.tx.getFieldVL(sfURI));

    {
        auto const page = view().dirInsert(
            keylet::ownerDir(accountID_), credentialKey, describeOwnerDir(accountID_));
        JLOG(j_.trace()) << "Adding Credential to owner directory " << to_string(credentialKey.key)
                         << ": " << (page ? "success" : "failure");
        if (!page)
            return tecDIR_FULL;
        sleCred->setFieldU64(sfIssuerNode, *page);

        increaseOwnerCount(ctx_.getApplyViewContext(), sleIssuer, 1, j_);
        addSponsorToLedgerEntry(ctx_.getApplyViewContext(), sleCred);
    }

    if (subject == accountID_)
    {
        sleCred->setFieldU32(sfFlags, lsfAccepted);
    }
    else
    {
        // Added to both dirs, owned only by issuer. CredentialAccept will transfer ownership to
        // subject. CredentialDelete will remove from both dirs and decrement 1 ownerCount.
        auto const page =
            view().dirInsert(keylet::ownerDir(subject), credentialKey, describeOwnerDir(subject));
        JLOG(j_.trace()) << "Adding Credential to subject directory "
                         << to_string(credentialKey.key) << ": " << (page ? "success" : "failure");
        if (!page)
            return tecDIR_FULL;
        sleCred->setFieldU64(sfSubjectNode, *page);
    }

    view().insert(sleCred);

    return tesSUCCESS;
}

void
CredentialCreate::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
CredentialCreate::finalizeInvariants(
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
