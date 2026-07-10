#include <xrpl/tx/transactors/credentials/CredentialAccept.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/protocol/AccountID.h>
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

#include <cstdint>
namespace xrpl {

using namespace credentials;

std::uint32_t
CredentialAccept::getFlagsMask(PreflightContext const& ctx)
{
    // 0 means "Allow any flags"
    return ctx.rules.enabled(fixInvalidTxFlags) ? tfUniversalMask : 0;
}

NotTEC
CredentialAccept::preflight(PreflightContext const& ctx)
{
    if (!ctx.tx[sfIssuer])
    {
        JLOG(ctx.j.trace()) << "Malformed transaction: Issuer field zeroed.";
        return temINVALID_ACCOUNT_ID;
    }

    auto const credType = ctx.tx[sfCredentialType];
    if (credType.empty() || (credType.size() > kMaxCredentialTypeLength))
    {
        JLOG(ctx.j.trace()) << "Malformed transaction: invalid size of CredentialType.";
        return temMALFORMED;
    }

    return tesSUCCESS;
}

TER
CredentialAccept::preclaim(PreclaimContext const& ctx)
{
    AccountID const subject = ctx.tx[sfAccount];
    AccountID const issuer = ctx.tx[sfIssuer];
    auto const credType(ctx.tx[sfCredentialType]);

    if (!ctx.view.exists(keylet::account(issuer)))
    {
        JLOG(ctx.j.warn()) << "No issuer: " << to_string(issuer);
        return tecNO_ISSUER;
    }

    auto const sleCred = ctx.view.read(keylet::credential(subject, issuer, credType));
    if (!sleCred)
    {
        JLOG(ctx.j.warn()) << "No credential: " << to_string(subject) << ", " << to_string(issuer)
                           << ", " << credType;
        return tecNO_ENTRY;
    }

    if (sleCred->isFlag(lsfAccepted))
    {
        JLOG(ctx.j.warn()) << "Credential already accepted: " << to_string(subject) << ", "
                           << to_string(issuer) << ", " << credType;
        return tecDUPLICATE;
    }

    return tesSUCCESS;
}

TER
CredentialAccept::doApply()
{
    AccountID const issuer{ctx_.tx[sfIssuer]};

    // Both exist as credential object exist itself (checked in preclaim)
    auto const sleSubject = view().peek(keylet::account(accountID_));
    auto const sleIssuer = view().peek(keylet::account(issuer));

    if (!sleSubject || !sleIssuer)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto txSponsorSle = getTxReserveSponsor(ctx_.getApplyViewContext());
    if (!txSponsorSle)
        return txSponsorSle.error();  // LCOV_EXCL_LINE

    if (auto const ret = checkReserve(
            ctx_.getApplyViewContext(),
            sleSubject,
            preFeeBalance_,
            *txSponsorSle,
            {.ownerCountDelta = 1},
            j_);
        !isTesSuccess(ret))
        return ret;

    auto const credType(ctx_.tx[sfCredentialType]);
    Keylet const credentialKey = keylet::credential(accountID_, issuer, credType);
    auto const sleCred = view().peek(credentialKey);  // Checked in preclaim()
    if (!sleCred)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    if (checkExpired(*sleCred, view().header().parentCloseTime))
    {
        JLOG(j_.trace()) << "Credential is expired: " << sleCred->getText();
        // delete expired credentials even if the transaction failed
        auto const err = credentials::deleteSLE(view(), sleCred, j_);
        return isTesSuccess(err) ? tecEXPIRED : err;
    }

    sleCred->setFieldU32(sfFlags, lsfAccepted);

    // Release the original creation sponsor from the credential (it covered
    // the issuer's reserve), then assign the accept tx's sponsor (if any) so
    // the credential reflects whoever is now covering the subject's reserve.
    decreaseOwnerCountForObject(view(), sleIssuer, sleCred, 1, j_);
    removeSponsorFromLedgerEntry(sleCred);

    addSponsorToLedgerEntry(ctx_.getApplyViewContext(), sleCred);
    increaseOwnerCount(ctx_.getApplyViewContext(), sleSubject, 1, j_);
    view().update(sleCred);

    return tesSUCCESS;
}

void
CredentialAccept::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
CredentialAccept::finalizeInvariants(
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
