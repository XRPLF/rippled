#include <xrpl/tx/transactors/credentials/CredentialCreate.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>  // IWYU pragma: keep
#include <xrpl/ledger/helpers/SLEWrappers.h>
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
    auto& j = ctx.j;

    if (!tx[sfSubject])
    {
        JLOG(j.trace()) << "Malformed transaction: Invalid Subject";
        return temMALFORMED;
    }

    auto const uri = tx[~sfURI];
    if (uri && (uri->empty() || (uri->size() > kMaxCredentialUriLength)))
    {
        JLOG(j.trace()) << "Malformed transaction: invalid size of URI.";
        return temMALFORMED;
    }

    auto const credType = tx[sfCredentialType];
    if (credType.empty() || (credType.size() > kMaxCredentialTypeLength))
    {
        JLOG(j.trace()) << "Malformed transaction: invalid size of CredentialType.";
        return temMALFORMED;
    }

    return tesSUCCESS;
}

TER
CredentialCreate::preclaim(PreclaimContext const& ctx)
{
    auto const credType(ctx.tx[sfCredentialType]);
    auto const subject = ctx.tx[sfSubject];

    if (!ctx.view.exists(keylet::account(subject)))
    {
        JLOG(ctx.j.trace()) << "Subject doesn't exist.";
        return tecNO_TARGET;
    }

    if (ctx.view.exists(keylet::credential(subject, ctx.tx[sfAccount], credType)))
    {
        JLOG(ctx.j.trace()) << "Credential already exists.";
        return tecDUPLICATE;
    }

    return tesSUCCESS;
}

TER
CredentialCreate::doApply()
{
    auto const subject = ctx_.tx[sfSubject];
    auto const credType(ctx_.tx[sfCredentialType]);
    Keylet const credentialKey = keylet::credential(subject, accountID_, credType);

    // Build with the ApplyViewContext so create() honors reserve sponsorship.
    CredentialEntry<ApplyView> sleCred{credentialKey, ctx_.getApplyViewContext()};
    sleCred.newSLE();
    if (!sleCred)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const optExp = ctx_.tx[~sfExpiration];
    if (optExp)
    {
        std::uint32_t const closeTime =
            ctx_.view().header().parentCloseTime.time_since_epoch().count();

        if (closeTime > *optExp)
        {
            JLOG(j_.trace()) << "Malformed transaction: "
                                "Expiration time is in the past.";
            return tecEXPIRED;
        }

        sleCred->setFieldU32(sfExpiration, *optExp);
    }

    sleCred->setAccountID(sfSubject, subject);
    sleCred->setAccountID(sfIssuer, accountID_);
    sleCred->setFieldVL(sfCredentialType, credType);

    if (ctx_.tx.isFieldPresent(sfURI))
        sleCred->setFieldVL(sfURI, ctx_.tx.getFieldVL(sfURI));

    // A self-issued credential is accepted immediately.
    if (subject == accountID_)
        sleCred->setFieldU32(sfFlags, lsfAccepted);

    // Reserve check (issuer's pre-fee balance) + link into the issuer's owner
    // directory (counted) and, for a third-party subject, the subject's tracking
    // directory + bump the issuer's OwnerCount + insert. Ownership transfers to
    // the subject on CredentialAccept. See CredentialEntry::ownerDirs().
    return sleCred.create(preFeeBalance_);
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
