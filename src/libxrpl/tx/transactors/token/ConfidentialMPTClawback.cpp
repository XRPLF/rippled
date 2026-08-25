#include <xrpl/tx/transactors/token/ConfidentialMPTClawback.h>

#include <xrpl/crypto/confidential.h>
#include <xrpl/ledger/helpers/ConfidentialMPT.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

namespace xrpl {

XRPAmount
ConfidentialMPTClawback::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    return confidentialMptBaseFee(view, tx);
}

NotTEC
ConfidentialMPTClawback::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfAccount] == ctx.tx[sfHolder])
        return temMALFORMED;
    if (ctx.tx[sfMPTAmount] == 0 || ctx.tx[sfMPTAmount] > kMaxMpTokenAmount)
        return temBAD_AMOUNT;
    if (ctx.tx[sfZKProof].length() != confidential::kClawbackSigmaProofBytes)
        return temMALFORMED;
    return tesSUCCESS;
}

TER
ConfidentialMPTClawback::preclaim(PreclaimContext const& ctx)
{
    auto const issuanceID = ctx.tx[sfMPTokenIssuanceID];
    auto const sleIssuance = ctx.view.read(keylet::mptIssuance(issuanceID));
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;
    if (auditorMigrationPending(*sleIssuance))
        return tecLOCKED;
    if ((*sleIssuance)[sfIssuer] != ctx.tx[sfAccount])
        return temMALFORMED;
    if (!ctx.view.exists(keylet::account(ctx.tx[sfHolder])))
        return tecNO_TARGET;
    auto const sleMpt = ctx.view.read(keylet::mptoken(issuanceID, ctx.tx[sfHolder]));
    if (!sleMpt)
        return tecOBJECT_NOT_FOUND;
    if (!sleIssuance->isFlag(lsfMPTCanClawback))
        return tecNO_PERMISSION;
    if (!sleIssuance->isFieldPresent(sfIssuerEncryptionKey))
        return tecNO_PERMISSION;
    if (!sleMpt->isFieldPresent(sfIssuerEncryptedBalance))
        return tecNO_PERMISSION;
    if ((*sleIssuance)[sfConfidentialOutstandingAmount] < ctx.tx[sfMPTAmount])
        return tecINSUFFICIENT_FUNDS;
    return tesSUCCESS;
}

TER
ConfidentialMPTClawback::doApply()
{
    auto const issuanceID = ctx_.tx[sfMPTokenIssuanceID];
    auto const holder = ctx_.tx[sfHolder];
    auto sleIssuance = view().peek(keylet::mptIssuance(issuanceID));
    auto sleMpt = view().peek(keylet::mptoken(issuanceID, holder));
    if (!sleIssuance || !sleMpt)
        return tecINTERNAL;

    confidential::CompressedPoint issuerPk{};
    if (!confidential::parseCompressedPoint((*sleIssuance)[sfIssuerEncryptionKey], issuerPk))
        return tecBAD_PROOF;
    confidential::Ciphertext issuerBal{};
    if (auto const ter = parseCiphertextField((*sleMpt)[sfIssuerEncryptedBalance], issuerBal);
        !isTesSuccess(ter))
        return ter;

    auto const ctxId = confidential::transactionContextIDClawback(
        static_cast<std::uint16_t>(ctx_.tx.getTxnType()),
        Slice(accountID_.data(), accountID_.size()),
        Slice(issuanceID.data(), issuanceID.size()),
        ctx_.tx.getSeqValue(),
        Slice(holder.data(), holder.size()));

    confidential::ClawbackSigmaPublicInput pub;
    pub.issuerKey = issuerPk;
    pub.issuerBalance = issuerBal;
    pub.revealedAmount = ctx_.tx[sfMPTAmount];
    if (!confidential::verifyClawbackSigma(
            pub, Slice(ctxId.data(), ctxId.size()), ctx_.tx[sfZKProof]))
        return tecBAD_PROOF;

    auto const amount = ctx_.tx[sfMPTAmount];
    (*sleIssuance)[sfConfidentialOutstandingAmount] =
        (*sleIssuance)[sfConfidentialOutstandingAmount] - amount;
    (*sleIssuance)[sfOutstandingAmount] = (*sleIssuance)[sfOutstandingAmount] - amount;

    if (auto const ter = clearConfidentialState(*sleIssuance, *sleMpt);
        !isTesSuccess(ter))
        return ter;
    view().update(sleMpt);
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
