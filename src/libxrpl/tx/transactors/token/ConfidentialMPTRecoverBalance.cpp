#include <xrpl/tx/transactors/token/ConfidentialMPTRecoverBalance.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
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

namespace xrpl {

NotTEC
ConfidentialMPTRecoverBalance::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureConfidentialMPTKeyRotation))
        return temDISABLED;

    auto const account = ctx.tx[sfAccount];
    auto const issuer = MPTIssue(ctx.tx[sfMPTokenIssuanceID]).getIssuer();

    // Only the issuer can perform recovery
    if (account != issuer)
        return temMALFORMED;

    // Cannot recover for self
    if (account == ctx.tx[sfHolder])
        return temMALFORMED;

    // Verify ConfidentialBalanceSpending field is present and has valid length
    if (!ctx.tx.isFieldPresent(sfConfidentialBalanceSpending))
        return temMALFORMED;

    if (ctx.tx[sfConfidentialBalanceSpending].length() != kEcGamalEncryptedTotalLength)
        return temBAD_CIPHERTEXT;

    // Verify it's a valid elliptic curve point
    if (!isValidCiphertext(ctx.tx[sfConfidentialBalanceSpending]))
        return temBAD_CIPHERTEXT;

    // Verify ZKProof is present
    if (!ctx.tx.isFieldPresent(sfZKProof))
        return temMALFORMED;

    // TODO: Verify proof length once the proof implementation is complete
    // For now, just check it's not empty
    // The spec mentions Chaum-Pedersen equality proof (approximately 196 bytes)
    // But the exact size will be defined when the crypto implementation is ready
    if (ctx.tx[sfZKProof].empty())
        return temMALFORMED;

    return tesSUCCESS;
}

XRPAmount
ConfidentialMPTRecoverBalance::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    return Transactor::calculateBaseFee(view, tx, kConfidentialFeeMultiplier + 1);
}

TER
ConfidentialMPTRecoverBalance::preclaim(PreclaimContext const& ctx)
{
    auto const account = ctx.tx[sfAccount];
    auto const holder = ctx.tx[sfHolder];

    // Check if issuer account exists
    if (!ctx.view.exists(keylet::account(account)))
        return terNO_ACCOUNT;

    // Check if holder account exists
    if (!ctx.view.exists(keylet::account(holder)))
        return tecNO_TARGET;

    // Check if MPT issuance exists
    auto const mptIssuanceID = ctx.tx[sfMPTokenIssuanceID];
    auto const sleIssuance = ctx.view.read(keylet::mptokenIssuance(mptIssuanceID));
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    // Sanity check: account must be the issuer
    if (sleIssuance->getAccountID(sfIssuer) != account)
        return tecNO_PERMISSION;

    // Check if issuance allows confidential transfer
    if (!sleIssuance->isFlag(lsfMPTCanHoldConfidentialBalance))
        return tecNO_PERMISSION;

    // Check if issuance has issuer ElGamal public key
    if (!sleIssuance->isFieldPresent(sfIssuerEncryptionKey))
        return tecNO_PERMISSION;

    // Check holder's MPToken
    auto const sleHolderMPToken = ctx.view.read(keylet::mptoken(mptIssuanceID, holder));
    if (!sleHolderMPToken)
        return tecOBJECT_NOT_FOUND;

    // Check if holder has issuer encrypted balance (the mirror)
    if (!sleHolderMPToken->isFieldPresent(sfIssuerEncryptedBalance))
        return tecNO_PERMISSION;

    // Check if holder has a pending RecoveryKey
    if (!sleHolderMPToken->isFieldPresent(sfRecoveryKey))
        return tecNO_PERMISSION;

    // Check if holder's issuer mirror is stale (needs migration first)
    // This check ensures the mirror is encrypted under the current issuer key
    if (sleIssuance->isFieldPresent(sfIssuerKeyEpoch) &&
        sleHolderMPToken->isFieldPresent(sfIssuerKeyMirrorEpoch))
    {
        auto const issuanceEpoch = sleIssuance->getFieldU32(sfIssuerKeyEpoch);
        auto const holderMirrorEpoch = sleHolderMPToken->getFieldU32(sfIssuerKeyMirrorEpoch);

        if (holderMirrorEpoch < issuanceEpoch)
            return tecNO_PERMISSION;  // Mirror is stale
    }

    // TODO: Verify the Chaum-Pedersen equality proof once crypto implementation is ready
    // The proof should verify that the new spending ciphertext encrypts the same value
    // as the on-ledger sfIssuerEncryptedBalance
    //
    // For now, skip proof verification as per task description:
    // "Proof part will be in another task in case there's any changes from crypto side"

    return tesSUCCESS;
}

TER
ConfidentialMPTRecoverBalance::doApply()
{
    auto const mptIssuanceID = ctx_.tx[sfMPTokenIssuanceID];
    auto const holder = ctx_.tx[sfHolder];

    auto sleIssuance = view().read(keylet::mptokenIssuance(mptIssuanceID));
    auto sleHolderMPToken = view().peek(keylet::mptoken(mptIssuanceID, holder));

    if (!sleIssuance || !sleHolderMPToken)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // Get the recovery key from the holder's MPToken
    auto const recoveryKey = sleHolderMPToken->at(sfRecoveryKey);

    // Set the new spending balance from the transaction
    sleHolderMPToken->at(sfConfidentialBalanceSpending) = ctx_.tx[sfConfidentialBalanceSpending];

    // Replace the holder's public key with the recovery key
    sleHolderMPToken->setFieldVL(sfHolderEncryptionKey, recoveryKey);

    // Reset inbox to encrypted zero under the new recovery key
    auto const encZeroForRecoveryKey =
        encryptCanonicalZeroAmount(recoveryKey, holder, mptIssuanceID);
    if (!encZeroForRecoveryKey)
        return tecINTERNAL;  // LCOV_EXCL_LINE
    sleHolderMPToken->at(sfConfidentialBalanceInbox) = *encZeroForRecoveryKey;

    // Clear the RecoveryKey field (recovery is complete)
    sleHolderMPToken->makeFieldAbsent(sfRecoveryKey);

    // Sync the holder's epoch with the issuer's epoch (key rotation)
    if (sleIssuance->isFieldPresent(sfIssuerKeyEpoch))
    {
        auto const issuerEpoch = sleIssuance->getFieldU32(sfIssuerKeyEpoch);
        sleHolderMPToken->setFieldU32(sfIssuerKeyMirrorEpoch, issuerEpoch);
    }

    // Increment the confidential balance version
    incrementConfidentialVersion(*sleHolderMPToken);

    view().update(sleHolderMPToken);

    return tesSUCCESS;
}

void
ConfidentialMPTRecoverBalance::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
}

bool
ConfidentialMPTRecoverBalance::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
