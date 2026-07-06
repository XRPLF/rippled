#include <xrpl/tx/transactors/token/ConfidentialMPTSend.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <memory>
#include <optional>
#include <utility>

namespace xrpl {

bool
ConfidentialMPTSend::checkExtraFeatures(PreflightContext const& ctx)
{
    return !ctx.tx.isFieldPresent(sfCredentialIDs) || ctx.rules.enabled(featureCredentials);
}

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

    // Issuer cannot be the destination
    if (ctx.tx[sfDestination] == issuer)
        return temMALFORMED;

    // Check the length of the encrypted amounts
    if (ctx.tx[sfSenderEncryptedAmount].length() != kEcGamalEncryptedTotalLength ||
        ctx.tx[sfDestinationEncryptedAmount].length() != kEcGamalEncryptedTotalLength ||
        ctx.tx[sfIssuerEncryptedAmount].length() != kEcGamalEncryptedTotalLength)
    {
        return temBAD_CIPHERTEXT;
    }

    bool const hasAuditor = ctx.tx.isFieldPresent(sfAuditorEncryptedAmount);
    if (hasAuditor && ctx.tx[sfAuditorEncryptedAmount].length() != kEcGamalEncryptedTotalLength)
        return temBAD_CIPHERTEXT;

    // Check the length of the ZKProof (fixed size regardless of recipient count)
    if (ctx.tx[sfZKProof].length() != kEcSendProofLength)
        return temMALFORMED;

    // Check the Pedersen commitments are valid
    if (!isValidCompressedECPoint(ctx.tx[sfBalanceCommitment]) ||
        !isValidCompressedECPoint(ctx.tx[sfAmountCommitment]))
    {
        return temMALFORMED;
    }

    // Check the encrypted amount formats, this is more expensive so put it at
    // the end
    if (!isValidCiphertext(ctx.tx[sfSenderEncryptedAmount]) ||
        !isValidCiphertext(ctx.tx[sfDestinationEncryptedAmount]) ||
        !isValidCiphertext(ctx.tx[sfIssuerEncryptedAmount]))
    {
        return temBAD_CIPHERTEXT;
    }

    if (hasAuditor && !isValidCiphertext(ctx.tx[sfAuditorEncryptedAmount]))
        return temBAD_CIPHERTEXT;

    if (auto const err = credentials::checkFields(ctx.tx, ctx.j); !isTesSuccess(err))
        return err;

    return tesSUCCESS;
}

XRPAmount
ConfidentialMPTSend::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    return Transactor::calculateBaseFee(view, tx, kConfidentialFeeMultiplier);
}

namespace detail {

static TER
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
    {
        auditor.emplace(
            ConfidentialRecipient{
                .publicKey = (*sleIssuance)[sfAuditorEncryptionKey],
                .encryptedAmount = ctx.tx[sfAuditorEncryptedAmount],
            });
    }

    auto const contextHash = getSendContextHash(
        ctx.tx[sfAccount],
        ctx.tx[sfMPTokenIssuanceID],
        ctx.tx.getSeqProxy().value(),
        ctx.tx[sfDestination],
        (*sleSenderMPToken)[~sfConfidentialBalanceVersion].value_or(0));

    return verifySendProof(
        ctx.tx[sfZKProof],
        {
            .publicKey = (*sleSenderMPToken)[sfHolderEncryptionKey],
            .encryptedAmount = ctx.tx[sfSenderEncryptedAmount],
        },
        {
            .publicKey = (*sleDestinationMPToken)[sfHolderEncryptionKey],
            .encryptedAmount = ctx.tx[sfDestinationEncryptedAmount],
        },
        {
            .publicKey = (*sleIssuance)[sfIssuerEncryptionKey],
            .encryptedAmount = ctx.tx[sfIssuerEncryptedAmount],
        },
        auditor,
        (*sleSenderMPToken)[sfConfidentialBalanceSpending],
        ctx.tx[sfAmountCommitment],
        ctx.tx[sfBalanceCommitment],
        contextHash);
}

}  // namespace detail

TER
ConfidentialMPTSend::preclaim(PreclaimContext const& ctx)
{
    // Check if sender account exists
    auto const account = ctx.tx[sfAccount];
    if (!ctx.view.exists(keylet::account(account)))
        return terNO_ACCOUNT;

    // Check if destination account exists
    auto const destination = ctx.tx[sfDestination];
    auto const sleDst = ctx.view.read(keylet::account(destination));
    if (!sleDst)
        return tecNO_TARGET;

    // Check destination tag
    if (((sleDst->getFlags() & lsfRequireDestTag) != 0u) &&
        !ctx.tx.isFieldPresent(sfDestinationTag))
    {
        return tecDST_TAG_NEEDED;
    }

    // Check if MPT issuance exists
    auto const mptIssuanceID = ctx.tx[sfMPTokenIssuanceID];
    auto const sleIssuance = ctx.view.read(keylet::mptokenIssuance(mptIssuanceID));
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    // Check if the issuance allows transfer
    if (!sleIssuance->isFlag(lsfMPTCanTransfer))
        return tecNO_AUTH;

    // Check if issuance allows confidential transfer
    if (!sleIssuance->isFlag(lsfMPTCanHoldConfidentialBalance))
        return tecNO_PERMISSION;

    // Sanity check: transfer fee must be 0 for confidential MPTs. This should
    // be unreachable in valid ledger state because MPTokenIssuanceCreate and
    // MPTokenIssuanceSet enforce it.
    if ((*sleIssuance)[~sfTransferFee].value_or(0) > 0)
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
    {
        return tecNO_PERMISSION;
    }

    // Check destination's MPToken existence
    auto const sleDestinationMPToken = ctx.view.read(keylet::mptoken(mptIssuanceID, destination));
    if (!sleDestinationMPToken)
        return tecOBJECT_NOT_FOUND;

    // Check destination's MPToken has necessary fields for confidential send
    if (!sleDestinationMPToken->isFieldPresent(sfHolderEncryptionKey) ||
        !sleDestinationMPToken->isFieldPresent(sfConfidentialBalanceInbox) ||
        !sleDestinationMPToken->isFieldPresent(sfIssuerEncryptedBalance))
    {
        return tecNO_PERMISSION;
    }

    // Sanity check: Both MPTokens' auditor fields must be present if auditing
    // is enabled
    if (requiresAuditor &&
        (!sleSenderMPToken->isFieldPresent(sfAuditorEncryptedBalance) ||
         !sleDestinationMPToken->isFieldPresent(sfAuditorEncryptedBalance)))
    {
        return tefINTERNAL;  // LCOV_EXCL_LINE
    }

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

    // Check deposit preauth before the expensive ZK proof verification.
    // Uses read-only view.
    auto const preauthErr =
        checkDepositPreauth(ctx.tx, ctx.view, account, destination, sleDst, ctx.j);
    if (!isTesSuccess(preauthErr))
        return preauthErr;

    return detail::verifySendProofs(ctx, sleSenderMPToken, sleDestinationMPToken, sleIssuance);
}

TER
ConfidentialMPTSend::doApply()
{
    auto const mptIssuanceID = ctx_.tx[sfMPTokenIssuanceID];
    auto const destination = ctx_.tx[sfDestination];

    auto sleSenderMPToken = view().peek(keylet::mptoken(mptIssuanceID, accountID_));
    auto sleDestinationMPToken = view().peek(keylet::mptoken(mptIssuanceID, destination));
    auto const sleIssuance = view().read(keylet::mptokenIssuance(mptIssuanceID));

    auto const sleDestAcct = view().read(keylet::account(destination));

    if (!sleSenderMPToken || !sleDestinationMPToken || !sleIssuance || !sleDestAcct)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // Deposit preauth authorization was already verified in preclaim.
    // Remove any expired credentials.
    if (auto err = cleanupExpiredCredentials(ctx_.tx, ctx_.view(), ctx_.journal);
        !isTesSuccess(err))
        return err;

    auto const senderEc = ctx_.tx[sfSenderEncryptedAmount];
    auto const destEc = ctx_.tx[sfDestinationEncryptedAmount];
    auto const issuerEc = ctx_.tx[sfIssuerEncryptedAmount];
    auto const proof = ctx_.tx[sfZKProof];
    Slice const sendChallenge{proof.data(), kEcBlindingFactorLength};

    auto const auditorEc = ctx_.tx[~sfAuditorEncryptedAmount];

    // Subtract from sender's spending balance
    {
        auto const curSpending = (*sleSenderMPToken)[sfConfidentialBalanceSpending];
        auto newSpending = homomorphicSubtract(curSpending, senderEc);
        if (!newSpending)
        {
            // LCOV_EXCL_START
            JLOG(ctx_.journal.error())
                << "ConfidentialMPTSend failed homomorphic subtract for sender spending balance.";
            return tecINTERNAL;
            // LCOV_EXCL_STOP
        }

        (*sleSenderMPToken)[sfConfidentialBalanceSpending] = std::move(*newSpending);
    }

    // Subtract from issuer's balance
    {
        auto const curIssuerEnc = (*sleSenderMPToken)[sfIssuerEncryptedBalance];
        auto newIssuerEnc = homomorphicSubtract(curIssuerEnc, issuerEc);
        if (!newIssuerEnc)
        {
            // LCOV_EXCL_START
            JLOG(ctx_.journal.error())
                << "ConfidentialMPTSend failed homomorphic subtract for sender issuer balance.";
            return tecINTERNAL;
            // LCOV_EXCL_STOP
        }

        (*sleSenderMPToken)[sfIssuerEncryptedBalance] = std::move(*newIssuerEnc);
    }

    // Subtract from auditor's balance if present
    if (auditorEc)
    {
        auto const curAuditorEnc = (*sleSenderMPToken)[sfAuditorEncryptedBalance];
        auto newAuditorEnc = homomorphicSubtract(curAuditorEnc, *auditorEc);
        if (!newAuditorEnc)
        {
            // LCOV_EXCL_START
            JLOG(ctx_.journal.error())
                << "ConfidentialMPTSend failed homomorphic subtract for sender auditor balance.";
            return tecINTERNAL;
            // LCOV_EXCL_STOP
        }

        (*sleSenderMPToken)[sfAuditorEncryptedBalance] = std::move(*newAuditorEnc);
    }

    // Add to destination's inbox balance
    {
        auto rerandomizedDestEc = rerandomizeCiphertext(
            destEc, (*sleDestinationMPToken)[sfHolderEncryptionKey], sendChallenge);
        if (!rerandomizedDestEc)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        auto const curInbox = (*sleDestinationMPToken)[sfConfidentialBalanceInbox];
        auto newInbox = homomorphicAdd(curInbox, *rerandomizedDestEc);
        if (!newInbox)
        {
            // LCOV_EXCL_START
            JLOG(ctx_.journal.error())
                << "ConfidentialMPTSend failed homomorphic add for destination inbox.";
            return tecINTERNAL;
            // LCOV_EXCL_STOP
        }

        (*sleDestinationMPToken)[sfConfidentialBalanceInbox] = std::move(*newInbox);
    }

    // Add to issuer's balance
    {
        auto rerandomizedIssuerEc =
            rerandomizeCiphertext(issuerEc, (*sleIssuance)[sfIssuerEncryptionKey], sendChallenge);
        if (!rerandomizedIssuerEc)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        auto const curIssuerEnc = (*sleDestinationMPToken)[sfIssuerEncryptedBalance];
        auto newIssuerEnc = homomorphicAdd(curIssuerEnc, *rerandomizedIssuerEc);
        if (!newIssuerEnc)
        {
            // LCOV_EXCL_START
            JLOG(ctx_.journal.error())
                << "ConfidentialMPTSend failed homomorphic add for destination issuer balance.";
            return tecINTERNAL;
            // LCOV_EXCL_STOP
        }

        (*sleDestinationMPToken)[sfIssuerEncryptedBalance] = std::move(*newIssuerEnc);
    }

    // Add to auditor's balance if present
    if (auditorEc)
    {
        auto rerandomizedAuditorEc = rerandomizeCiphertext(
            *auditorEc, (*sleIssuance)[sfAuditorEncryptionKey], sendChallenge);
        if (!rerandomizedAuditorEc)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        auto const curAuditorEnc = (*sleDestinationMPToken)[sfAuditorEncryptedBalance];
        auto newAuditorEnc = homomorphicAdd(curAuditorEnc, *rerandomizedAuditorEc);
        if (!newAuditorEnc)
        {
            // LCOV_EXCL_START
            JLOG(ctx_.journal.error())
                << "ConfidentialMPTSend failed homomorphic add for destination auditor balance.";
            return tecINTERNAL;
            // LCOV_EXCL_STOP
        }

        (*sleDestinationMPToken)[sfAuditorEncryptedBalance] = std::move(*newAuditorEnc);
    }

    // increment sender version only; receiver version is not modified by incoming sends
    incrementConfidentialVersion(*sleSenderMPToken);

    view().update(sleSenderMPToken);
    view().update(sleDestinationMPToken);
    return tesSUCCESS;
}

void
ConfidentialMPTSend::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
}

bool
ConfidentialMPTSend::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
