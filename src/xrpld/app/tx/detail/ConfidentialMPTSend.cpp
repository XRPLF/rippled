#include <xrpld/app/tx/detail/ConfidentialMPTSend.h>

#include <xrpl/ledger/CredentialHelpers.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

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

    // Check the length of the ZKProof
    auto const recipientCount = getConfidentialRecipientCount(hasAuditor);
    auto const sizeEquality = getMultiCiphertextEqualityProofSize(recipientCount);
    auto const sizePedersenLinkage = 2 * ecPedersenProofLength;

    if (ctx.tx[sfZKProof].length() != sizeEquality + sizePedersenLinkage)
        return temMALFORMED;

    // Check the Pedersen commitments are valid
    if (!isValidCompressedECPoint(ctx.tx[sfBalanceCommitment]) || !isValidCompressedECPoint(ctx.tx[sfAmountCommitment]))
        return temMALFORMED;

    // Check the encrypted amount formats, this is more expensive so put it at
    // the end
    if (!isValidCiphertext(ctx.tx[sfSenderEncryptedAmount]) ||
        !isValidCiphertext(ctx.tx[sfDestinationEncryptedAmount]) || !isValidCiphertext(ctx.tx[sfIssuerEncryptedAmount]))
        return temBAD_CIPHERTEXT;

    if (hasAuditor && !isValidCiphertext(ctx.tx[sfAuditorEncryptedAmount]))
        return temBAD_CIPHERTEXT;

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
    auto const recipientCount = getConfidentialRecipientCount(hasAuditor);
    auto const proof = ctx.tx[sfZKProof];
    size_t remainingLength = proof.size();
    size_t currentOffset = 0;

    // Extract equality proof
    auto const sizeEquality = getMultiCiphertextEqualityProofSize(recipientCount);
    if (remainingLength < sizeEquality)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const equalityProof = proof.substr(currentOffset, sizeEquality);
    currentOffset += sizeEquality;
    remainingLength -= sizeEquality;

    // Extract Pedersen linkage proof for amount commitment
    if (remainingLength < ecPedersenProofLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const amountLinkageProof = proof.substr(currentOffset, ecPedersenProofLength);
    currentOffset += ecPedersenProofLength;
    remainingLength -= ecPedersenProofLength;

    // Extract Pedersen linkage proof for balance commitment
    if (remainingLength < ecPedersenProofLength)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const balanceLinkageProof = proof.substr(currentOffset, ecPedersenProofLength);
    currentOffset += ecPedersenProofLength;
    remainingLength -= ecPedersenProofLength;

    // todo: Extract range proof once the lib is ready
    if (remainingLength != 0)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // Prepare recipient list
    std::vector<ConfidentialRecipient> recipients;
    recipients.reserve(recipientCount);

    recipients.push_back({(*sleSenderMPToken)[sfHolderElGamalPublicKey], ctx.tx[sfSenderEncryptedAmount]});

    recipients.push_back({(*sleDestinationMPToken)[sfHolderElGamalPublicKey], ctx.tx[sfDestinationEncryptedAmount]});

    recipients.push_back({(*sleIssuance)[sfIssuerElGamalPublicKey], ctx.tx[sfIssuerEncryptedAmount]});

    if (hasAuditor)
    {
        recipients.push_back({(*sleIssuance)[sfAuditorElGamalPublicKey], ctx.tx[sfAuditorEncryptedAmount]});
    }

    // Prepare the context hash
    auto const contextHash = getSendContextHash(
        ctx.tx[sfAccount],
        ctx.tx[sfSequence],
        ctx.tx[sfMPTokenIssuanceID],
        ctx.tx[sfDestination],
        (*sleSenderMPToken)[~sfConfidentialBalanceVersion].value_or(0));

    // Verify the multi-ciphertext equality proof
    if (auto const ter = verifyMultiCiphertextEqualityProof(equalityProof, recipients, recipientCount, contextHash);
        !isTesSuccess(ter))
    {
        JLOG(ctx.j.trace()) << "ConfidentialMPTSend: Equality proof failed.";
        return ter;
    }

    // Verify amount linkage
    if (auto const ter = verifyAmountPcmLinkage(
            amountLinkageProof,
            ctx.tx[sfSenderEncryptedAmount],
            (*sleSenderMPToken)[sfHolderElGamalPublicKey],
            ctx.tx[sfAmountCommitment],
            contextHash);
        !isTesSuccess(ter))
    {
        JLOG(ctx.j.trace()) << "ConfidentialMPTSend: Amount linkage proof failed.";
        return ter;
    }

    // Verify balance linkage
    if (auto const ter = verifyBalancePcmLinkage(
            balanceLinkageProof,
            (*sleSenderMPToken)[sfConfidentialBalanceSpending],
            (*sleSenderMPToken)[sfHolderElGamalPublicKey],
            ctx.tx[sfBalanceCommitment],
            contextHash);
        !isTesSuccess(ter))
    {
        JLOG(ctx.j.trace()) << "ConfidentialMPTSend: Balance linkage proof failed.";
        return ter;
    }

    return tesSUCCESS;
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
    if (!sleIssuance->isFlag(lsfMPTCanPrivacy))
        return tecNO_PERMISSION;

    // Check if issuance has issuer ElGamal public key
    if (!sleIssuance->isFieldPresent(sfIssuerElGamalPublicKey))
        return tecNO_PERMISSION;

    bool const hasAuditor = ctx.tx.isFieldPresent(sfAuditorEncryptedAmount);
    bool const requiresAuditor = sleIssuance->isFieldPresent(sfAuditorElGamalPublicKey);

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
    if (!sleSenderMPToken->isFieldPresent(sfHolderElGamalPublicKey) ||
        !sleSenderMPToken->isFieldPresent(sfConfidentialBalanceSpending) ||
        !sleSenderMPToken->isFieldPresent(sfIssuerEncryptedBalance))
        return tecNO_PERMISSION;

    // Sanity check: MPToken's auditor field must be present if auditing is
    // enabled
    if (requiresAuditor && !sleSenderMPToken->isFieldPresent(sfAuditorEncryptedBalance))
        return tefINTERNAL;

    // Check destination's MPToken existence
    auto const sleDestinationMPToken = ctx.view.read(keylet::mptoken(mptIssuanceID, destination));
    if (!sleDestinationMPToken)
        return tecOBJECT_NOT_FOUND;

    // Check destination's MPToken has necessary fields for confidential send
    if (!sleDestinationMPToken->isFieldPresent(sfHolderElGamalPublicKey) ||
        !sleDestinationMPToken->isFieldPresent(sfConfidentialBalanceInbox) ||
        !sleDestinationMPToken->isFieldPresent(sfIssuerEncryptedBalance))
        return tecNO_PERMISSION;

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

    if (auto err = verifyDepositPreauth(ctx_.tx, ctx_.view(), account_, destination, sleDestAcct, ctx_.journal);
        !isTesSuccess(err))
        return err;

    Slice const senderEc = ctx_.tx[sfSenderEncryptedAmount];
    Slice const destEc = ctx_.tx[sfDestinationEncryptedAmount];
    Slice const issuerEc = ctx_.tx[sfIssuerEncryptedAmount];

    auto const auditorEc = ctx_.tx[~sfAuditorEncryptedAmount];

    // Subtract from sender's spending balance
    {
        Slice const curSpending = (*sleSenderMPToken)[sfConfidentialBalanceSpending];
        Buffer newSpending(ecGamalEncryptedTotalLength);

        if (TER const ter = homomorphicSubtract(curSpending, senderEc, newSpending); !isTesSuccess(ter))
            return tecINTERNAL;  // LCOV_EXCL_LINE

        (*sleSenderMPToken)[sfConfidentialBalanceSpending] = newSpending;
    }

    // Subtract from issuer's balance
    {
        Slice const curIssuerEnc = (*sleSenderMPToken)[sfIssuerEncryptedBalance];
        Buffer newIssuerEnc(ecGamalEncryptedTotalLength);

        if (TER const ter = homomorphicSubtract(curIssuerEnc, issuerEc, newIssuerEnc); !isTesSuccess(ter))
            return tecINTERNAL;  // LCOV_EXCL_LINE

        (*sleSenderMPToken)[sfIssuerEncryptedBalance] = newIssuerEnc;
    }

    // Subtract from auditor's balance if present
    if (auditorEc)
    {
        Slice const curAuditorEnc = (*sleSenderMPToken)[sfAuditorEncryptedBalance];
        Buffer newAuditorEnc(ecGamalEncryptedTotalLength);

        if (TER const ter = homomorphicSubtract(curAuditorEnc, *auditorEc, newAuditorEnc); !isTesSuccess(ter))
            return tecINTERNAL;  // LCOV_EXCL_LINE

        (*sleSenderMPToken)[sfAuditorEncryptedBalance] = newAuditorEnc;
    }

    // Add to destination's inbox balance
    {
        Slice const curInbox = (*sleDestinationMPToken)[sfConfidentialBalanceInbox];
        Buffer newInbox(ecGamalEncryptedTotalLength);

        if (TER const ter = homomorphicAdd(curInbox, destEc, newInbox); !isTesSuccess(ter))
            return tecINTERNAL;  // LCOV_EXCL_LINE

        (*sleDestinationMPToken)[sfConfidentialBalanceInbox] = newInbox;
    }

    // Add to issuer's balance
    {
        Slice const curIssuerEnc = (*sleDestinationMPToken)[sfIssuerEncryptedBalance];
        Buffer newIssuerEnc(ecGamalEncryptedTotalLength);

        if (TER const ter = homomorphicAdd(curIssuerEnc, issuerEc, newIssuerEnc); !isTesSuccess(ter))
            return tecINTERNAL;  // LCOV_EXCL_LINE

        (*sleDestinationMPToken)[sfIssuerEncryptedBalance] = newIssuerEnc;
    }

    // Add to auditor's balance if present
    if (auditorEc)
    {
        Slice const curAuditorEnc = (*sleDestinationMPToken)[sfAuditorEncryptedBalance];
        Buffer newAuditorEnc(ecGamalEncryptedTotalLength);

        if (TER const ter = homomorphicAdd(curAuditorEnc, *auditorEc, newAuditorEnc); !isTesSuccess(ter))
            return tecINTERNAL;  // LCOV_EXCL_LINE

        (*sleDestinationMPToken)[sfAuditorEncryptedBalance] = newAuditorEnc;
    }

    // increment version
    incrementConfidentialVersion(*sleSenderMPToken);
    incrementConfidentialVersion(*sleDestinationMPToken);

    view().update(sleSenderMPToken);
    view().update(sleDestinationMPToken);
    return tesSUCCESS;
}
}  // namespace xrpl
