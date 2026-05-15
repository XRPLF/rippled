#include <xrpl/tx/transactors/credentials/CredentialAccept.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
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
    if (credType.empty() || (credType.size() > kMAX_CREDENTIAL_TYPE_LENGTH))
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
    auto const sleSubject = view().peek(keylet::account(account_));
    auto const sleIssuer = view().peek(keylet::account(issuer));

    if (!sleSubject || !sleIssuer)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    {
        STAmount const reserve{
            view().fees().accountReserve(sleSubject->getFieldU32(sfOwnerCount) + 1)};
        if (preFeeBalance_ < reserve)
            return tecINSUFFICIENT_RESERVE;
    }

    auto const credType(ctx_.tx[sfCredentialType]);
    Keylet const credentialKey = keylet::credential(account_, issuer, credType);
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
    view().update(sleCred);

    adjustOwnerCount(view(), sleIssuer, -1, j_);
    adjustOwnerCount(view(), sleSubject, 1, j_);

    return tesSUCCESS;
}

void
CredentialAccept::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
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
