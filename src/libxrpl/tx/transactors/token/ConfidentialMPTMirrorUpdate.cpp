#include <xrpl/tx/transactors/token/ConfidentialMPTMirrorUpdate.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <memory>

namespace xrpl {

bool
ConfidentialMPTMirrorUpdate::checkExtraFeatures(PreflightContext const& ctx)
{
    // Key rotation makes sense only when featureConfidentialTransfer is enabled.
    return ctx.rules.enabled(featureConfidentialTransfer);
}

NotTEC
ConfidentialMPTMirrorUpdate::preflight(PreflightContext const& ctx)
{
    auto const account = ctx.tx[sfAccount];
    auto const issuer = MPTIssue(ctx.tx[sfMPTokenIssuanceID]).getIssuer();
    auto const holder = ctx.tx[~sfHolder];
    bool const hasHolder = holder.has_value();

    // The rotation mode is determined by the presence of the
    // Holder field: Holder present is issuer mode, Holder absent is
    // holder self-migration.
    if (hasHolder)
    {
        // Issuer mode: account must be the issuer
        if (account != issuer)
            return temMALFORMED;

        if (account == *holder)
            return temMALFORMED;
    }
    else
    {
        // Holder self-migration: the submitter is the holder, account must not be the issuer.
        if (account == issuer)
            return temMALFORMED;
    }

    // At least one ciphertext will be updated.
    bool const hasIssuerAmount = ctx.tx.isFieldPresent(sfIssuerEncryptedAmount);
    bool const hasAuditorAmount = ctx.tx.isFieldPresent(sfAuditorEncryptedAmount);
    if (!hasIssuerAmount && !hasAuditorAmount)
        return temMALFORMED;

    // Check the length of the encrypted amounts. Length check is cheaper than format check so put
    // it before the format check.
    if (hasIssuerAmount && ctx.tx[sfIssuerEncryptedAmount].length() != kEcGamalEncryptedTotalLength)
        return temBAD_CIPHERTEXT;

    if (hasAuditorAmount &&
        ctx.tx[sfAuditorEncryptedAmount].length() != kEcGamalEncryptedTotalLength)
        return temBAD_CIPHERTEXT;

    // Check the encrypted amount formats. It is more expensive so put it at the end of preflight.
    if (hasIssuerAmount && !isValidCiphertext(ctx.tx[sfIssuerEncryptedAmount]))
        return temBAD_CIPHERTEXT;

    if (hasAuditorAmount && !isValidCiphertext(ctx.tx[sfAuditorEncryptedAmount]))
        return temBAD_CIPHERTEXT;

    // todo: check zkproof

    return tesSUCCESS;
}

XRPAmount
ConfidentialMPTMirrorUpdate::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    return Transactor::calculateBaseFee(view, tx, kConfidentialFeeMultiplier);
}

TER
ConfidentialMPTMirrorUpdate::preclaim(PreclaimContext const& ctx)
{
    // Check if account exists
    auto const account = ctx.tx[sfAccount];
    if (!ctx.view.exists(keylet::account(account)))
        return terNO_ACCOUNT;  // LCOV_EXCL_LINE

    // The issuance must exist and have confidential balances enabled with a
    // registered issuer encryption key; otherwise there is no mirror to update.
    auto const mptIssuanceID = ctx.tx[sfMPTokenIssuanceID];
    auto const sleIssuance = ctx.view.read(keylet::mptokenIssuance(mptIssuanceID));
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    // The issuance must have confidential balances enabled with a registered issuer encryption key.
    if (!sleIssuance->isFlag(lsfMPTCanHoldConfidentialBalance) ||
        !sleIssuance->isFieldPresent(sfIssuerEncryptionKey))
        return tecNO_PERMISSION;

    // Sanity check: preflight already enforced the issuer holder combination
    // under different rotation modes.
    auto const holder = ctx.tx[~sfHolder];
    bool const hasHolder = holder.has_value();
    auto const issuer = sleIssuance->getAccountID(sfIssuer);
    if (hasHolder ? (issuer != account) : (issuer == account))
    {
        // LCOV_EXCL_START
        UNREACHABLE(
            "xrpl::ConfidentialMPTMirrorUpdate::preclaim : invalid issuer holder combination");
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    // The holder is sfHolder in issuer mode and is sfAccount in holder mode.
    auto const holderID = hasHolder ? *holder : account;

    // In issuer mode, the holder must exist. In holder mode, the account existence was checked
    // already.
    if (hasHolder && !ctx.view.exists(keylet::account(holderID)))
        return tecNO_TARGET;

    // In either issuer or holder mode, check the existence of the MPToken object.
    auto const sleMptoken = ctx.view.read(keylet::mptoken(mptIssuanceID, holderID));
    if (!sleMptoken)
        return tecOBJECT_NOT_FOUND;

    // The holder must already hold an issuer confidential balance.
    if (!sleMptoken->isFieldPresent(sfIssuerEncryptedBalance))
        return tecNO_PERMISSION;

    bool const hasIssuerAmount = ctx.tx.isFieldPresent(sfIssuerEncryptedAmount);
    bool const hasAuditorAmount = ctx.tx.isFieldPresent(sfAuditorEncryptedAmount);

    // Migrating the auditor mirror requires the issuance to have a registered
    // auditor encryption key.
    if (hasAuditorAmount && !sleIssuance->isFieldPresent(sfAuditorEncryptionKey))
        return tecNO_PERMISSION;

    // Epoch staleness. Mirror epochs count how many times the
    // holder's mirrors have been re-encrypted; the issuance key epochs count
    // how many times the keys have rotated.
    std::uint32_t const issuerKeyEpoch = (*sleIssuance)[~sfIssuerKeyEpoch].value_or(0);
    std::uint32_t const auditorKeyEpoch = (*sleIssuance)[~sfAuditorKeyEpoch].value_or(0);
    std::uint32_t const issuerMirrorEpoch = (*sleMptoken)[~sfIssuerKeyMirrorEpoch].value_or(0);
    std::uint32_t const auditorMirrorEpoch = (*sleMptoken)[~sfAuditorKeyMirrorEpoch].value_or(0);

    // The issuer mirror can only be re-encrypted while it is stale.
    if (hasIssuerAmount && issuerMirrorEpoch >= issuerKeyEpoch)
        return tecNO_PERMISSION;

    if (hasAuditorAmount)
    {
        // An issuer-mode auditor-only migration: the issuer mirror must already be up to date.
        if (hasHolder && !hasIssuerAmount && issuerMirrorEpoch != issuerKeyEpoch)
            return tecNO_PERMISSION;

        // The auditor mirror can only be re-encrypted while it is stale, unless
        // this is its first-time registration (no auditor mirror yet).
        bool const hasAuditorMirror = sleMptoken->isFieldPresent(sfAuditorEncryptedBalance);
        if (hasAuditorMirror && auditorMirrorEpoch >= auditorKeyEpoch)
            return tecNO_PERMISSION;
    }

    // Holder self-migration re-encrypts the mirror from the holder's own
    // spending balance, which reflects the holder's full balance only once the
    // inbox has been merged into it. Require the inbox to be canonical zero,
    // i.e. ConfidentialMPTMergeInbox has already been applied.
    if (!hasHolder)
    {
        // Sanity check: a holder that already carries an issuer mirror
        // necessarily has a holder encryption key and a spending balance
        if (!sleMptoken->isFieldPresent(sfHolderEncryptionKey) ||
            !sleMptoken->isFieldPresent(sfConfidentialBalanceSpending))
        {
            // LCOV_EXCL_START
            UNREACHABLE(
                "xrpl::ConfidentialMPTMirrorUpdate::preclaim : an issuer mirror implies a holder "
                "key and spending balance");
            return tefINTERNAL;
            // LCOV_EXCL_STOP
        }

        auto const expectedZeroInbox = encryptCanonicalZeroAmount(
            (*sleMptoken)[sfHolderEncryptionKey], holderID, mptIssuanceID);
        if (!expectedZeroInbox)
        {
            // LCOV_EXCL_START
            UNREACHABLE(
                "xrpl::ConfidentialMPTMirrorUpdate::preclaim : canonical zero encryption cannot "
                "fail for an already-valid holder public key");
            return tefINTERNAL;
            // LCOV_EXCL_STOP
        }

        bool const inboxIsCanonicalZero = sleMptoken->isFieldPresent(sfConfidentialBalanceInbox) &&
            Slice((*sleMptoken)[sfConfidentialBalanceInbox]) == Slice(*expectedZeroInbox);
        if (!inboxIsCanonicalZero)
            return tecNO_PERMISSION;
    }

    // todo: check zkproof

    return tesSUCCESS;
}

TER
ConfidentialMPTMirrorUpdate::doApply()
{
    auto const mptIssuanceID = ctx_.tx[sfMPTokenIssuanceID];

    auto const sleIssuance = view().read(keylet::mptokenIssuance(mptIssuanceID));
    if (!sleIssuance)
    {
        // LCOV_EXCL_START
        UNREACHABLE(
            "xrpl::ConfidentialMPTMirrorUpdate::doApply : preclaim already validated the "
            "issuance exists");
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    // The holderID is sfHolder in issuer mode and sfAccount in holder mode.
    auto const holder = ctx_.tx[~sfHolder];
    auto const holderID = holder.value_or(accountID_);

    auto sleMptoken = view().peek(keylet::mptoken(mptIssuanceID, holderID));
    if (!sleMptoken)
    {
        // LCOV_EXCL_START
        UNREACHABLE(
            "xrpl::ConfidentialMPTMirrorUpdate::doApply : preclaim already validated the "
            "MPToken exists");
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    // Re-encrypt the requested mirror(s) and advance the corresponding mirror
    // epoch to match the issuance key epoch. Only
    // set the epoch field when it is non-zero, matching the issuance convention
    // that an absent epoch means zero.
    if (ctx_.tx.isFieldPresent(sfIssuerEncryptedAmount))
    {
        (*sleMptoken)[sfIssuerEncryptedBalance] = ctx_.tx[sfIssuerEncryptedAmount];
        std::uint32_t const issuerKeyEpoch = (*sleIssuance)[~sfIssuerKeyEpoch].value_or(0);
        if (issuerKeyEpoch != 0)
            (*sleMptoken)[sfIssuerKeyMirrorEpoch] = issuerKeyEpoch;
    }

    if (ctx_.tx.isFieldPresent(sfAuditorEncryptedAmount))
    {
        (*sleMptoken)[sfAuditorEncryptedBalance] = ctx_.tx[sfAuditorEncryptedAmount];
        std::uint32_t const auditorKeyEpoch = (*sleIssuance)[~sfAuditorKeyEpoch].value_or(0);
        if (auditorKeyEpoch != 0)
            (*sleMptoken)[sfAuditorKeyMirrorEpoch] = auditorKeyEpoch;
    }

    view().update(sleMptoken);
    return tesSUCCESS;
}

void
ConfidentialMPTMirrorUpdate::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
}

bool
ConfidentialMPTMirrorUpdate::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
