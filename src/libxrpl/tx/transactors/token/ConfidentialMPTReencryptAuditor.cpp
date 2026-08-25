#include <xrpl/tx/transactors/token/ConfidentialMPTReencryptAuditor.h>

#include <xrpl/crypto/confidential.h>
#include <xrpl/ledger/helpers/ConfidentialMPT.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

namespace xrpl {

XRPAmount
ConfidentialMPTReencryptAuditor::calculateBaseFee(
    ReadView const& view,
    STTx const& tx)
{
    return confidentialMptBaseFee(view, tx);
}

NotTEC
ConfidentialMPTReencryptAuditor::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfAccount] == ctx.tx[sfHolder])
        return temMALFORMED;
    if (auto const ter =
            preflightCiphertext(ctx.tx[sfAuditorEncryptedBalance]);
        !isTesSuccess(ter))
        return ter;
    if (ctx.tx[sfZKProof].length() !=
        confidential::kAuditorEqualitySigmaProofBytes)
        return temMALFORMED;
    return tesSUCCESS;
}

TER
ConfidentialMPTReencryptAuditor::preclaim(PreclaimContext const& ctx)
{
    auto const issuanceID = ctx.tx[sfMPTokenIssuanceID];
    auto const sleIssuance = ctx.view.read(keylet::mptIssuance(issuanceID));
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;
    if ((*sleIssuance)[sfIssuer] != ctx.tx[sfAccount])
        return temMALFORMED;
    if (!auditorMigrationPending(*sleIssuance) ||
        (*sleIssuance)[~sfAuditorMigrationCount].value_or(0) == 0)
        return tecNO_PERMISSION;
    if (!sleIssuance->isFieldPresent(sfIssuerEncryptionKey))
        return tecNO_PERMISSION;

    auto const holder = ctx.tx[sfHolder];
    if (!ctx.view.exists(keylet::account(holder)))
        return tecNO_TARGET;
    auto const sleMpt = ctx.view.read(keylet::mptoken(issuanceID, holder));
    if (!sleMpt)
        return tecOBJECT_NOT_FOUND;
    if (!sleMpt->isFieldPresent(sfIssuerEncryptedBalance))
        return tecNO_PERMISSION;

    auto const currentVersion = (*sleIssuance)[sfAuditorKeyVersion];
    auto const targetVersion = currentVersion + 1;
    auto const holderVersion = (*sleMpt)[~sfAuditorKeyVersion];
    if (holderVersion && *holderVersion == targetVersion)
        return tecDUPLICATE;
    if (sleIssuance->isFieldPresent(sfAuditorEncryptionKey) &&
        (!holderVersion || *holderVersion != currentVersion))
        return tecNO_PERMISSION;

    return tesSUCCESS;
}

TER
ConfidentialMPTReencryptAuditor::doApply()
{
    auto const issuanceID = ctx_.tx[sfMPTokenIssuanceID];
    auto const holder = ctx_.tx[sfHolder];
    auto sleIssuance = view().peek(keylet::mptIssuance(issuanceID));
    auto sleMpt = view().peek(keylet::mptoken(issuanceID, holder));
    if (!sleIssuance || !sleMpt)
        return tecINTERNAL;

    confidential::CompressedPoint issuerKey{};
    confidential::CompressedPoint auditorKey{};
    if (!confidential::parseCompressedPoint(
            (*sleIssuance)[sfIssuerEncryptionKey], issuerKey) ||
        !confidential::parseCompressedPoint(
            (*sleIssuance)[sfPendingAuditorEncryptionKey], auditorKey))
        return tecBAD_PROOF;

    confidential::Ciphertext issuerCiphertext{};
    confidential::Ciphertext auditorCiphertext{};
    if (auto const ter = parseCiphertextField(
            (*sleMpt)[sfIssuerEncryptedBalance], issuerCiphertext);
        !isTesSuccess(ter))
        return ter;
    if (auto const ter = parseCiphertextField(
            ctx_.tx[sfAuditorEncryptedBalance], auditorCiphertext);
        !isTesSuccess(ter))
        return ter;

    auto const targetVersion = (*sleIssuance)[sfAuditorKeyVersion] + 1;
    auto const context = confidential::transactionContextIDMigrateAuditor(
        static_cast<std::uint16_t>(ctx_.tx.getTxnType()),
        Slice(accountID_.data(), accountID_.size()),
        Slice(issuanceID.data(), issuanceID.size()),
        ctx_.tx.getSeqValue(),
        Slice(holder.data(), holder.size()),
        targetVersion);

    confidential::AuditorEqualitySigmaPublicInput pub{
        issuerKey,
        issuerCiphertext,
        auditorKey,
        auditorCiphertext};
    if (!confidential::verifyAuditorEqualitySigma(
            pub, Slice(context.data(), context.size()), ctx_.tx[sfZKProof]))
        return tecBAD_PROOF;

    setCiphertextField(
        *sleMpt, sfAuditorEncryptedBalance, auditorCiphertext);
    (*sleMpt)[sfAuditorKeyVersion] = targetVersion;

    auto const remaining = (*sleIssuance)[sfAuditorMigrationCount];
    if (remaining == 0)
        return tecINTERNAL;
    if (remaining == 1)
    {
        auto const pending = (*sleIssuance)[sfPendingAuditorEncryptionKey];
        sleIssuance->setFieldVL(sfAuditorEncryptionKey, pending);
        (*sleIssuance)[sfAuditorKeyVersion] = targetVersion;
        sleIssuance->makeFieldAbsent(sfPendingAuditorEncryptionKey);
        sleIssuance->makeFieldAbsent(sfAuditorMigrationCount);
    }
    else
    {
        (*sleIssuance)[sfAuditorMigrationCount] = remaining - 1;
    }

    view().update(sleMpt);
    view().update(sleIssuance);
    return tesSUCCESS;
}

void
ConfidentialMPTReencryptAuditor::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
}

bool
ConfidentialMPTReencryptAuditor::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
