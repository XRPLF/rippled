/** @file
 *  Implementation of the `CredentialDelete` transactor.
 *
 *  Handles removal of on-ledger verifiable credential objects.  Three
 *  principals are authorized to delete a credential: the subject (holder),
 *  the issuer (revoker), or any third party once the credential has expired.
 *  The third-party expiry path enables ledger hygiene — anyone may sweep
 *  inert objects to reclaim the reserve they hold.
 *
 *  @see CredentialDelete.h for the full API contract.
 *  @see credentials::deleteSLE() in CredentialHelpers.cpp for the low-level
 *      SLE removal and owner-directory bookkeeping.
 */
#include <xrpl/tx/transactors/credentials/CredentialDelete.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <memory>

namespace xrpl {

using namespace credentials;

std::uint32_t
CredentialDelete::getFlagsMask(PreflightContext const& ctx)
{
    return ctx.rules.enabled(fixInvalidTxFlags) ? tfUniversalMask : 0;
}

NotTEC
CredentialDelete::preflight(PreflightContext const& ctx)
{
    auto const subject = ctx.tx[~sfSubject];
    auto const issuer = ctx.tx[~sfIssuer];

    if (!subject && !issuer)
    {
        JLOG(ctx.j.trace()) << "Malformed transaction: "
                               "No Subject or Issuer fields.";
        return temMALFORMED;
    }

    if ((subject && subject->isZero()) || (issuer && issuer->isZero()))
    {
        JLOG(ctx.j.trace()) << "Malformed transaction: Subject or Issuer "
                               "field zeroed.";
        return temINVALID_ACCOUNT_ID;
    }

    auto const credType = ctx.tx[sfCredentialType];
    if (credType.empty() || (credType.size() > kMAX_CREDENTIAL_TYPE_LENGTH))
    {
        JLOG(ctx.j.trace()) << "Malformed transaction: invalid size of CredentialType.";
        return temMALFORMED;
    }

    return tesSUCCESS;
}

TER
CredentialDelete::preclaim(PreclaimContext const& ctx)
{
    AccountID const account{ctx.tx[sfAccount]};
    auto const subject = ctx.tx[~sfSubject].value_or(account);
    auto const issuer = ctx.tx[~sfIssuer].value_or(account);
    auto const credType(ctx.tx[sfCredentialType]);

    if (!ctx.view.exists(keylet::credential(subject, issuer, credType)))
        return tecNO_ENTRY;

    return tesSUCCESS;
}

TER
CredentialDelete::doApply()
{
    auto const subject = ctx_.tx[~sfSubject].value_or(account_);
    auto const issuer = ctx_.tx[~sfIssuer].value_or(account_);

    auto const credType(ctx_.tx[sfCredentialType]);
    auto const sleCred = view().peek(keylet::credential(subject, issuer, credType));
    if (!sleCred)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    if ((subject != account_) && (issuer != account_) &&
        !checkExpired(*sleCred, ctx_.view().header().parentCloseTime))
    {
        JLOG(j_.trace()) << "Can't delete non-expired credential.";
        return tecNO_PERMISSION;
    }

    return deleteSLE(view(), sleCred, j_);
}

void
CredentialDelete::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
}

bool
CredentialDelete::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}
}  // namespace xrpl
