#include <xrpld/app/tx/detail/ConfidentialClawback.h>

#include <xrpl/ledger/View.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace xrpl {

NotTEC
ConfidentialClawback::preflight(PreflightContext const& ctx)
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
    if (clawAmount == 0 || clawAmount > maxMPTokenAmount)
        return temBAD_AMOUNT;

    // Verify proof length
    if (ctx.tx[sfZKProof].length() != ecEqualityProofLength)
        return temMALFORMED;

    return tesSUCCESS;
}

TER
ConfidentialClawback::preclaim(PreclaimContext const& ctx)
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
    auto const sleIssuance = ctx.view.read(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    // Sanity check: issuer must be the same as account
    if (sleIssuance->getAccountID(sfIssuer) != account)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // Check if issuance has issuer ElGamal public key
    if (!sleIssuance->isFieldPresent(sfIssuerElGamalPublicKey))
        return tecNO_PERMISSION;

    // Check if clawback is allowed
    if (!sleIssuance->isFlag(lsfMPTCanClawback))
        return tecNO_PERMISSION;

    // Check holder's MPToken
    auto const sleHolderMPToken = ctx.view.read(keylet::mptoken(mptIssuanceID, holder));
    if (!sleHolderMPToken)
        return tecOBJECT_NOT_FOUND;

    // Check if holder has confidential balances to claw back
    if (!sleHolderMPToken->isFieldPresent(sfIssuerEncryptedBalance))
        return tecNO_PERMISSION;

    // Sanity check: claw amount can not exceed confidential outstanding amount
    auto const amount = ctx.tx[sfMPTAmount];
    if (amount > (*sleIssuance)[~sfConfidentialOutstandingAmount].value_or(0))
        return tecINSUFFICIENT_FUNDS;

    auto const contextHash = getClawbackContextHash(account, ctx.tx[sfSequence], mptIssuanceID, amount, holder);

    // Verify the revealed confidential amount by the issuer matches the exact
    // confidential balance of the holder.
    return verifyClawbackEqualityProof(
        amount,
        ctx.tx[sfZKProof],
        (*sleIssuance)[sfIssuerElGamalPublicKey],
        (*sleHolderMPToken)[sfIssuerEncryptedBalance],
        contextHash);
}

TER
ConfidentialClawback::doApply()
{
    auto const mptIssuanceID = ctx_.tx[sfMPTokenIssuanceID];
    auto const holder = ctx_.tx[sfHolder];

    auto sleIssuance = view().peek(keylet::mptIssuance(mptIssuanceID));
    auto sleHolderMPToken = view().peek(keylet::mptoken(mptIssuanceID, holder));

    if (!sleIssuance || !sleHolderMPToken)
        return tecINTERNAL;

    auto const clawAmount = ctx_.tx[sfMPTAmount];

    Slice const holderPubKey = (*sleHolderMPToken)[sfHolderElGamalPublicKey];
    Slice const issuerPubKey = (*sleIssuance)[sfIssuerElGamalPublicKey];

    // After clawback, the balance should be encrypted zero.
    auto const encZeroForHolder = encryptCanonicalZeroAmount(holderPubKey, holder, mptIssuanceID);
    if (!encZeroForHolder)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const encZeroForIssuer = encryptCanonicalZeroAmount(issuerPubKey, holder, mptIssuanceID);
    if (!encZeroForIssuer)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // Set holder's confidential balances to encrypted zero
    (*sleHolderMPToken)[sfConfidentialBalanceInbox] = *encZeroForHolder;
    (*sleHolderMPToken)[sfConfidentialBalanceSpending] = *encZeroForHolder;
    (*sleHolderMPToken)[sfIssuerEncryptedBalance] = *encZeroForIssuer;
    (*sleHolderMPToken)[sfConfidentialBalanceVersion] = 0;

    if (sleHolderMPToken->isFieldPresent(sfAuditorEncryptedBalance))
    {
        // Sanity check: the issuance must have an auditor public key if
        // auditing is enabled.
        if (!sleIssuance->isFieldPresent(sfAuditorElGamalPublicKey))
            return tecINTERNAL;  // LCOV_EXCL_LINE

        Slice const auditorPubKey = (*sleIssuance)[sfAuditorElGamalPublicKey];

        auto const encZeroForAuditor = encryptCanonicalZeroAmount(auditorPubKey, holder, mptIssuanceID);

        if (!encZeroForAuditor)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        (*sleHolderMPToken)[sfAuditorEncryptedBalance] = *encZeroForAuditor;
    }

    // Decrease Global Confidential Outstanding Amount
    auto const oldCOA = (*sleIssuance)[sfConfidentialOutstandingAmount];
    (*sleIssuance)[sfConfidentialOutstandingAmount] = oldCOA - clawAmount;

    // Decrease Global Total Outstanding Amount
    auto const oldOA = (*sleIssuance)[sfOutstandingAmount];
    (*sleIssuance)[sfOutstandingAmount] = oldOA - clawAmount;

    view().update(sleHolderMPToken);
    view().update(sleIssuance);

    return tesSUCCESS;
}
}  // namespace xrpl
