#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/transactors/token/ConfidentialMPTConvertBack.h>

#include <cstddef>

namespace xrpl {

NotTEC
ConfidentialMPTConvertBack::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureConfidentialTransfer))
        return temDISABLED;

    // issuer cannot convert back
    if (MPTIssue(ctx.tx[sfMPTokenIssuanceID]).getIssuer() == ctx.tx[sfAccount])
        return temMALFORMED;

    if (ctx.tx[sfMPTAmount] == 0 || ctx.tx[sfMPTAmount] > maxMPTokenAmount)
        return temBAD_AMOUNT;

    if (!isValidCompressedECPoint(ctx.tx[sfBalanceCommitment]))
        return temMALFORMED;

    // check encrypted amount format after the above basic checks
    // this check is more expensive so put it at the end
    if (auto const res = checkEncryptedAmountFormat(ctx.tx); !isTesSuccess(res))
        return res;

    // ConvertBack proof = compact sigma proof (128 bytes) + single bulletproof (688 bytes)
    if (ctx.tx[sfZKProof].size() != ecCompactConvertBackProofLength)
        return temMALFORMED;

    return tesSUCCESS;
}

/**
 * Verifies the cryptographic proofs for a ConvertBack transaction.
 *
 * This function verifies three proofs:
 * 1. Revealed amount proof: verifies the encrypted amounts (holder, issuer,
 *    auditor) all encrypt the same revealed amount using the blinding factor.
 * 2. Pedersen linkage proof: verifies the balance commitment is derived from
 *    the holder's encrypted spending balance.
 * 3. Bulletproof (range proof): verifies the remaining balance (balance - amount)
 *    is non-negative, preventing overdrafts.
 *
 * All proofs are verified before returning any error to prevent timing attacks.
 */
static TER
verifyProofs(
    STTx const& tx,
    std::shared_ptr<SLE const> const& issuance,
    std::shared_ptr<SLE const> const& mptoken)
{
    if (!mptoken->isFieldPresent(sfHolderEncryptionKey))
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const mptIssuanceID = tx[sfMPTokenIssuanceID];
    auto const account = tx[sfAccount];
    auto const amount = tx[sfMPTAmount];
    auto const blindingFactor = tx[sfBlindingFactor];
    auto const holderPubKey = (*mptoken)[sfHolderEncryptionKey];

    auto const contextHash = getConvertBackContextHash(
        account,
        mptIssuanceID,
        tx.getSeqProxy().value(),
        (*mptoken)[~sfConfidentialBalanceVersion].value_or(0));

    // Prepare Auditor Info
    std::optional<ConfidentialRecipient> auditor;
    bool const hasAuditor = issuance->isFieldPresent(sfAuditorEncryptionKey);
    if (hasAuditor)
    {
        auditor.emplace(
            ConfidentialRecipient{
                (*issuance)[sfAuditorEncryptionKey], tx[sfAuditorEncryptedAmount]});
    }

    // Run all verifications before returning any error to prevent timing attacks
    // that could reveal which proof failed.
    bool valid = true;

    if (auto const ter = verifyRevealedAmount(
            amount,
            Slice(blindingFactor.data(), blindingFactor.size()),
            {holderPubKey, tx[sfHolderEncryptedAmount]},
            {(*issuance)[sfIssuerEncryptionKey], tx[sfIssuerEncryptedAmount]},
            auditor);
        !isTesSuccess(ter))
    {
        valid = false;
    }

    if (auto const ter = verifyConvertBackProof(
            tx[sfZKProof],
            holderPubKey,
            (*mptoken)[sfConfidentialBalanceSpending],
            tx[sfBalanceCommitment],
            amount,
            contextHash);
        !isTesSuccess(ter))
    {
        valid = false;
    }

    if (!valid)
        return tecBAD_PROOF;

    return tesSUCCESS;
}

TER
ConfidentialMPTConvertBack::preclaim(PreclaimContext const& ctx)
{
    auto const mptIssuanceID = ctx.tx[sfMPTokenIssuanceID];
    auto const account = ctx.tx[sfAccount];
    auto const amount = ctx.tx[sfMPTAmount];

    // ensure that issuance exists
    auto const sleIssuance = ctx.view.read(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    if (!sleIssuance->isFlag(lsfMPTCanConfidentialAmount))
        return tecNO_PERMISSION;

    bool const hasAuditor = ctx.tx.isFieldPresent(sfAuditorEncryptedAmount);
    bool const requiresAuditor = sleIssuance->isFieldPresent(sfAuditorEncryptionKey);

    // tx must include auditor ciphertext if the issuance has enabled
    // auditing
    if (requiresAuditor && !hasAuditor)
        return tecNO_PERMISSION;

    // if auditing is not supported then user should not upload auditor
    // ciphertext
    if (!requiresAuditor && hasAuditor)
        return tecNO_PERMISSION;

    // already checked in preflight, but should also check that issuer on
    // the issuance isn't the account either
    if (sleIssuance->getAccountID(sfIssuer) == account)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const sleMptoken = ctx.view.read(keylet::mptoken(mptIssuanceID, account));
    if (!sleMptoken)
        return tecOBJECT_NOT_FOUND;

    if (!sleMptoken->isFieldPresent(sfConfidentialBalanceSpending) ||
        !sleMptoken->isFieldPresent(sfHolderEncryptionKey))
    {
        return tecNO_PERMISSION;
    }

    // if the total circulating confidential balance is smaller than what the
    // holder is trying to convert back, we know for sure this txn should
    // fail
    if ((*sleIssuance)[~sfConfidentialOutstandingAmount].value_or(0) < amount)
    {
        return tecINSUFFICIENT_FUNDS;
    }

    // Check lock
    MPTIssue const mptIssue(mptIssuanceID);
    if (auto const ter = checkFrozen(ctx.view, account, mptIssue); !isTesSuccess(ter))
        return ter;

    // Check auth
    if (auto const ter = requireAuth(ctx.view, mptIssue, account); !isTesSuccess(ter))
        return ter;

    if (TER const res = verifyProofs(ctx.tx, sleIssuance, sleMptoken); !isTesSuccess(res))
        return res;

    return tesSUCCESS;
}

TER
ConfidentialMPTConvertBack::doApply()
{
    auto const mptIssuanceID = ctx_.tx[sfMPTokenIssuanceID];

    auto sleMptoken = view().peek(keylet::mptoken(mptIssuanceID, account_));
    if (!sleMptoken)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto sleIssuance = view().peek(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const amtToConvertBack = ctx_.tx[sfMPTAmount];
    auto const amt = (*sleMptoken)[~sfMPTAmount].value_or(0);

    // Converting back increases regular balance and decreases confidential
    // outstanding. This is the inverse of Convert.
    (*sleMptoken)[sfMPTAmount] = amt + amtToConvertBack;
    (*sleIssuance)[sfConfidentialOutstandingAmount] =
        (*sleIssuance)[sfConfidentialOutstandingAmount] - amtToConvertBack;

    std::optional<Slice> const auditorEc = ctx_.tx[~sfAuditorEncryptedAmount];

    // homomorphically subtract holder's encrypted balance
    {
        auto res = homomorphicSubtract(
            (*sleMptoken)[sfConfidentialBalanceSpending], ctx_.tx[sfHolderEncryptedAmount]);
        if (!res)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        (*sleMptoken)[sfConfidentialBalanceSpending] = std::move(*res);
    }

    // homomorphically subtract issuer's encrypted balance
    {
        auto res = homomorphicSubtract(
            (*sleMptoken)[sfIssuerEncryptedBalance], ctx_.tx[sfIssuerEncryptedAmount]);
        if (!res)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        (*sleMptoken)[sfIssuerEncryptedBalance] = std::move(*res);
    }

    if (auditorEc)
    {
        auto res = homomorphicSubtract(
            (*sleMptoken)[sfAuditorEncryptedBalance], ctx_.tx[sfAuditorEncryptedAmount]);
        if (!res)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        (*sleMptoken)[sfAuditorEncryptedBalance] = std::move(*res);
    }

    incrementConfidentialVersion(*sleMptoken);

    view().update(sleIssuance);
    view().update(sleMptoken);
    return tesSUCCESS;
}

}  // namespace xrpl
