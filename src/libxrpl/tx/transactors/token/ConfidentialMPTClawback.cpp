#include <xrpl/tx/transactors/token/ConfidentialMPTClawback.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <memory>
#include <utility>

namespace xrpl {

NotTEC
ConfidentialMPTClawback::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureConfidentialTransfer))
        return temDISABLED;

    auto const account = ctx.tx[sfAccount];

    // Only issuer can clawback
    if (account != MPTIssue(ctx.tx[sfMPTokenIssuanceID]).getIssuer())
        return temMALFORMED;

    // Cannot clawback from self
    if (account == ctx.tx[sfHolder])
        return temMALFORMED;

    // Check invalid claw amount
    auto const clawAmount = ctx.tx[sfMPTAmount];
    if (clawAmount == 0 || clawAmount > kMaxMpTokenAmount)
        return temBAD_AMOUNT;

    // Verify proof length
    if (ctx.tx[sfZKProof].length() != kEcClawbackProofLength)
        return temMALFORMED;

    return tesSUCCESS;
}

XRPAmount
ConfidentialMPTClawback::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    return Transactor::calculateBaseFee(view, tx, kConfidentialFeeMultiplier);
}

TER
ConfidentialMPTClawback::preclaim(PreclaimContext const& ctx)
{
    // Check if sender account exists
    auto const account = ctx.tx[sfAccount];
    if (!ctx.view.exists(keylet::account(account)))
        return terNO_ACCOUNT;

    // Check if holder account exists
    auto const holder = ctx.tx[sfHolder];
    if (!ctx.view.exists(keylet::account(holder)))
        return tecNO_TARGET;

    // Check if MPT issuance exists
    auto const mptIssuanceID = ctx.tx[sfMPTokenIssuanceID];
    auto const sleIssuance = ctx.view.read(keylet::mptokenIssuance(mptIssuanceID));
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    // Sanity check: account must be the same as issuer
    if (sleIssuance->getAccountID(sfIssuer) != account)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // Check if issuance has issuer ElGamal public key
    if (!sleIssuance->isFieldPresent(sfIssuerEncryptionKey))
        return tecNO_PERMISSION;

    // Check if clawback is allowed
    if (!sleIssuance->isFlag(lsfMPTCanClawback))
        return tecNO_PERMISSION;

    // Check if issuance allows confidential transfer
    if (!sleIssuance->isFlag(lsfMPTCanHoldConfidentialBalance))
        return tecNO_PERMISSION;

    // Check holder's MPToken
    auto const sleHolderMPToken = ctx.view.read(keylet::mptoken(mptIssuanceID, holder));
    if (!sleHolderMPToken)
        return tecOBJECT_NOT_FOUND;

    // Check if holder has confidential balances to claw back
    if (!sleHolderMPToken->isFieldPresent(sfIssuerEncryptedBalance))
        return tecNO_PERMISSION;

    // Check if Holder has ElGamal public Key
    if (!sleHolderMPToken->isFieldPresent(sfHolderEncryptionKey))
        return tecNO_PERMISSION;

    // Sanity check: claw amount can not exceed confidential outstanding amount
    // or total outstanding amount (prevents underflow in doApply)
    auto const amount = ctx.tx[sfMPTAmount];
    if (amount > (*sleIssuance)[~sfConfidentialOutstandingAmount].value_or(0) ||
        amount > (*sleIssuance)[sfOutstandingAmount])
        return tecINSUFFICIENT_FUNDS;

    auto const contextHash =
        getClawbackContextHash(account, mptIssuanceID, ctx.tx.getSeqProxy().value(), holder);

    // Verify the revealed confidential amount by the issuer matches the exact
    // confidential balance of the holder.
    return verifyClawbackProof(
        amount,
        ctx.tx[sfZKProof],
        (*sleIssuance)[sfIssuerEncryptionKey],
        (*sleHolderMPToken)[sfIssuerEncryptedBalance],
        contextHash);
}

TER
ConfidentialMPTClawback::doApply()
{
    auto const mptIssuanceID = ctx_.tx[sfMPTokenIssuanceID];
    auto const holder = ctx_.tx[sfHolder];

    auto sleIssuance = view().peek(keylet::mptokenIssuance(mptIssuanceID));
    auto sleHolderMPToken = view().peek(keylet::mptoken(mptIssuanceID, holder));

    if (!sleIssuance || !sleHolderMPToken)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const clawAmount = ctx_.tx[sfMPTAmount];

    auto const holderPubKey = (*sleHolderMPToken)[sfHolderEncryptionKey];
    auto const issuerPubKey = (*sleIssuance)[sfIssuerEncryptionKey];

    // After clawback, the balance should be encrypted zero.
    auto const encZeroForHolder = encryptCanonicalZeroAmount(holderPubKey, holder, mptIssuanceID);
    if (!encZeroForHolder)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto encZeroForIssuer = encryptCanonicalZeroAmount(issuerPubKey, holder, mptIssuanceID);
    if (!encZeroForIssuer)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // Set holder's confidential balances to encrypted zero
    (*sleHolderMPToken)[sfConfidentialBalanceInbox] = *encZeroForHolder;
    (*sleHolderMPToken)[sfConfidentialBalanceSpending] = *encZeroForHolder;
    (*sleHolderMPToken)[sfIssuerEncryptedBalance] = std::move(*encZeroForIssuer);
    incrementConfidentialVersion(*sleHolderMPToken);

    if (sleHolderMPToken->isFieldPresent(sfAuditorEncryptedBalance))
    {
        // Sanity check: the issuance must have an auditor public key if
        // auditing is enabled.
        if (!sleIssuance->isFieldPresent(sfAuditorEncryptionKey))
            return tecINTERNAL;  // LCOV_EXCL_LINE

        auto const auditorPubKey = (*sleIssuance)[sfAuditorEncryptionKey];

        auto encZeroForAuditor = encryptCanonicalZeroAmount(auditorPubKey, holder, mptIssuanceID);

        if (!encZeroForAuditor)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        (*sleHolderMPToken)[sfAuditorEncryptedBalance] = std::move(*encZeroForAuditor);
    }

    // Decrease Global Confidential Outstanding Amount
    auto const oldCOA = (*sleIssuance)[sfConfidentialOutstandingAmount];
    if (clawAmount > oldCOA)
        return tecINTERNAL;  // LCOV_EXCL_LINE
    (*sleIssuance)[sfConfidentialOutstandingAmount] = oldCOA - clawAmount;

    // Decrease Global Total Outstanding Amount
    auto const oldOA = (*sleIssuance)[sfOutstandingAmount];
    if (clawAmount > oldOA)
        return tecINTERNAL;  // LCOV_EXCL_LINE
    (*sleIssuance)[sfOutstandingAmount] = oldOA - clawAmount;

    view().update(sleHolderMPToken);
    view().update(sleIssuance);

    return tesSUCCESS;
}

void
ConfidentialMPTClawback::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
}

bool
ConfidentialMPTClawback::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
