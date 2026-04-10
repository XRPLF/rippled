#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/transactors/token/ConfidentialMPTSend.h>

namespace xrpl {

NotTEC
ConfidentialMPTSend::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureConfidentialTransfer))
        return temDISABLED;

    auto const account = ctx.tx[sfAccount];
    auto const issuer = MPTIssue(ctx.tx[sfMPTokenIssuanceID]).getIssuer();

    // ConfidentialMPTSend only allows holder to holder, holder to second account,
    // and second account to holder transfers. So issuer cannot be the sender.
    if (account == issuer)
        return temMALFORMED;

    // Can not send to self
    if (account == ctx.tx[sfDestination])
        return temMALFORMED;

    // Check the length of the encrypted amounts
    if (ctx.tx[sfSenderEncryptedAmount].length() != ecGamalEncryptedTotalLength ||
        ctx.tx[sfDestinationEncryptedAmount].length() != ecGamalEncryptedTotalLength ||
        ctx.tx[sfIssuerEncryptedAmount].length() != ecGamalEncryptedTotalLength)
        return temBAD_CIPHERTEXT;

    bool const hasAuditor = ctx.tx.isFieldPresent(sfAuditorEncryptedAmount);
    if (hasAuditor && ctx.tx[sfAuditorEncryptedAmount].length() != ecGamalEncryptedTotalLength)
        return temBAD_CIPHERTEXT;

    // Check the length of the ZKProof (fixed size regardless of recipient count)
    if (ctx.tx[sfZKProof].length() != ecCompactSendProofLength)
        return temMALFORMED;

    // Check the Pedersen commitments are valid
    if (!isValidCompressedECPoint(ctx.tx[sfBalanceCommitment]) ||
        !isValidCompressedECPoint(ctx.tx[sfAmountCommitment]))
        return temMALFORMED;

    // Check the encrypted amount formats, this is more expensive so put it at
    // the end
    if (!isValidCiphertext(ctx.tx[sfSenderEncryptedAmount]) ||
        !isValidCiphertext(ctx.tx[sfDestinationEncryptedAmount]) ||
        !isValidCiphertext(ctx.tx[sfIssuerEncryptedAmount]))
        return temBAD_CIPHERTEXT;

    if (hasAuditor && !isValidCiphertext(ctx.tx[sfAuditorEncryptedAmount]))
        return temBAD_CIPHERTEXT;

    if (auto const err = credentials::checkFields(ctx.tx, ctx.j); !isTesSuccess(err))
        return err;

    return tesSUCCESS;
}

TER
verifySendProofs(
    PreclaimContext const& ctx,
    std::shared_ptr<SLE const> const& sleSenderMPToken,
    std::shared_ptr<SLE const> const& sleDestinationMPToken,
    std::shared_ptr<SLE const> const& sleIssuance)
{
    // Sanity check
    if (!sleSenderMPToken || !sleDestinationMPToken || !sleIssuance)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const hasAuditor = ctx.tx.isFieldPresent(sfAuditorEncryptedAmount);

    std::optional<ConfidentialRecipient> auditor;
    if (hasAuditor)
        auditor.emplace(
            ConfidentialRecipient{
                (*sleIssuance)[sfAuditorEncryptionKey], ctx.tx[sfAuditorEncryptedAmount]});

    auto const contextHash = getSendContextHash(
        ctx.tx[sfAccount],
        ctx.tx[sfMPTokenIssuanceID],
        ctx.tx.getSeqProxy().value(),
        ctx.tx[sfDestination],
        (*sleSenderMPToken)[~sfConfidentialBalanceVersion].value_or(0));

    return verifySendProof(
        ctx.tx[sfZKProof],
        {(*sleSenderMPToken)[sfHolderEncryptionKey], ctx.tx[sfSenderEncryptedAmount]},
        {(*sleDestinationMPToken)[sfHolderEncryptionKey], ctx.tx[sfDestinationEncryptedAmount]},
        {(*sleIssuance)[sfIssuerEncryptionKey], ctx.tx[sfIssuerEncryptedAmount]},
        auditor,
        (*sleSenderMPToken)[sfConfidentialBalanceSpending],
        ctx.tx[sfAmountCommitment],
        ctx.tx[sfBalanceCommitment],
        contextHash);
}

TER
ConfidentialMPTSend::preclaim(PreclaimContext const& ctx)
{
    // Check if sender account exists
    auto const account = ctx.tx[sfAccount];
    if (!ctx.view.exists(keylet::account(account)))
        return terNO_ACCOUNT;

    // Check if destination account exists
    auto const destination = ctx.tx[sfDestination];
    if (!ctx.view.exists(keylet::account(destination)))
        return tecNO_TARGET;

    // Check if MPT issuance exists
    auto const mptIssuanceID = ctx.tx[sfMPTokenIssuanceID];
    auto const sleIssuance = ctx.view.read(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    // Check if the issuance allows transfer
    if (!sleIssuance->isFlag(lsfMPTCanTransfer))
        return tecNO_AUTH;

    // Check if issuance allows confidential transfer
    if (!sleIssuance->isFlag(lsfMPTCanConfidentialAmount))
        return tecNO_PERMISSION;

    // Check if issuance has issuer ElGamal public key
    if (!sleIssuance->isFieldPresent(sfIssuerEncryptionKey))
        return tecNO_PERMISSION;

    bool const hasAuditor = ctx.tx.isFieldPresent(sfAuditorEncryptedAmount);
    bool const requiresAuditor = sleIssuance->isFieldPresent(sfAuditorEncryptionKey);

    // Tx must include auditor ciphertext if the issuance has enabled
    // auditing, and must not include it if auditing is not enabled
    if (requiresAuditor != hasAuditor)
        return tecNO_PERMISSION;

    // Sanity check: issuer isn't the sender
    if (sleIssuance->getAccountID(sfIssuer) == ctx.tx[sfAccount])
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // Check sender's MPToken existence
    auto const sleSenderMPToken = ctx.view.read(keylet::mptoken(mptIssuanceID, account));
    if (!sleSenderMPToken)
        return tecOBJECT_NOT_FOUND;

    // Check sender's MPToken has necessary fields for confidential send
    if (!sleSenderMPToken->isFieldPresent(sfHolderEncryptionKey) ||
        !sleSenderMPToken->isFieldPresent(sfConfidentialBalanceSpending) ||
        !sleSenderMPToken->isFieldPresent(sfIssuerEncryptedBalance))
        return tecNO_PERMISSION;

    // Check destination's MPToken existence
    auto const sleDestinationMPToken = ctx.view.read(keylet::mptoken(mptIssuanceID, destination));
    if (!sleDestinationMPToken)
        return tecOBJECT_NOT_FOUND;

    // Check destination's MPToken has necessary fields for confidential send
    if (!sleDestinationMPToken->isFieldPresent(sfHolderEncryptionKey) ||
        !sleDestinationMPToken->isFieldPresent(sfConfidentialBalanceInbox) ||
        !sleDestinationMPToken->isFieldPresent(sfIssuerEncryptedBalance))
        return tecNO_PERMISSION;

    // Sanity check: Both MPTokens' auditor fields must be present if auditing
    // is enabled
    if (requiresAuditor &&
        (!sleSenderMPToken->isFieldPresent(sfAuditorEncryptedBalance) ||
         !sleDestinationMPToken->isFieldPresent(sfAuditorEncryptedBalance)))
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // Check lock
    MPTIssue const mptIssue(mptIssuanceID);
    if (auto const ter = checkFrozen(ctx.view, account, mptIssue); !isTesSuccess(ter))
        return ter;

    if (auto const ter = checkFrozen(ctx.view, destination, mptIssue); !isTesSuccess(ter))
        return ter;

    // Check auth
    if (auto const ter = requireAuth(ctx.view, mptIssue, account); !isTesSuccess(ter))
        return ter;

    if (auto const ter = requireAuth(ctx.view, mptIssue, destination); !isTesSuccess(ter))
        return ter;

    if (auto const err = credentials::valid(ctx.tx, ctx.view, ctx.tx[sfAccount], ctx.j);
        !isTesSuccess(err))
        return err;

    return verifySendProofs(ctx, sleSenderMPToken, sleDestinationMPToken, sleIssuance);
}

TER
ConfidentialMPTSend::doApply()
{
    auto const mptIssuanceID = ctx_.tx[sfMPTokenIssuanceID];
    auto const destination = ctx_.tx[sfDestination];

    auto sleSenderMPToken = view().peek(keylet::mptoken(mptIssuanceID, account_));
    auto sleDestinationMPToken = view().peek(keylet::mptoken(mptIssuanceID, destination));

    auto sleDestAcct = view().peek(keylet::account(destination));

    if (!sleSenderMPToken || !sleDestinationMPToken || !sleDestAcct)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (auto err = verifyDepositPreauth(
            ctx_.tx, ctx_.view(), account_, destination, sleDestAcct, ctx_.journal);
        !isTesSuccess(err))
        return err;

    Slice const senderEc = ctx_.tx[sfSenderEncryptedAmount];
    Slice const destEc = ctx_.tx[sfDestinationEncryptedAmount];
    Slice const issuerEc = ctx_.tx[sfIssuerEncryptedAmount];

    auto const auditorEc = ctx_.tx[~sfAuditorEncryptedAmount];

    // Subtract from sender's spending balance
    {
        Slice const curSpending = (*sleSenderMPToken)[sfConfidentialBalanceSpending];
        auto newSpending = homomorphicSubtract(curSpending, senderEc);
        if (!newSpending)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        (*sleSenderMPToken)[sfConfidentialBalanceSpending] = std::move(*newSpending);
    }

    // Subtract from issuer's balance
    {
        Slice const curIssuerEnc = (*sleSenderMPToken)[sfIssuerEncryptedBalance];
        auto newIssuerEnc = homomorphicSubtract(curIssuerEnc, issuerEc);
        if (!newIssuerEnc)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        (*sleSenderMPToken)[sfIssuerEncryptedBalance] = std::move(*newIssuerEnc);
    }

    // Subtract from auditor's balance if present
    if (auditorEc)
    {
        Slice const curAuditorEnc = (*sleSenderMPToken)[sfAuditorEncryptedBalance];
        auto newAuditorEnc = homomorphicSubtract(curAuditorEnc, *auditorEc);
        if (!newAuditorEnc)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        (*sleSenderMPToken)[sfAuditorEncryptedBalance] = std::move(*newAuditorEnc);
    }

    // Add to destination's inbox balance
    {
        Slice const curInbox = (*sleDestinationMPToken)[sfConfidentialBalanceInbox];
        auto newInbox = homomorphicAdd(curInbox, destEc);
        if (!newInbox)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        (*sleDestinationMPToken)[sfConfidentialBalanceInbox] = std::move(*newInbox);
    }

    // Add to issuer's balance
    {
        Slice const curIssuerEnc = (*sleDestinationMPToken)[sfIssuerEncryptedBalance];
        auto newIssuerEnc = homomorphicAdd(curIssuerEnc, issuerEc);
        if (!newIssuerEnc)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        (*sleDestinationMPToken)[sfIssuerEncryptedBalance] = std::move(*newIssuerEnc);
    }

    // Add to auditor's balance if present
    if (auditorEc)
    {
        Slice const curAuditorEnc = (*sleDestinationMPToken)[sfAuditorEncryptedBalance];
        auto newAuditorEnc = homomorphicAdd(curAuditorEnc, *auditorEc);
        if (!newAuditorEnc)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        (*sleDestinationMPToken)[sfAuditorEncryptedBalance] = std::move(*newAuditorEnc);
    }

    // increment sender version only; receiver version is not modified by incoming sends
    incrementConfidentialVersion(*sleSenderMPToken);

    view().update(sleSenderMPToken);
    view().update(sleDestinationMPToken);
    return tesSUCCESS;
}

}  // namespace xrpl
