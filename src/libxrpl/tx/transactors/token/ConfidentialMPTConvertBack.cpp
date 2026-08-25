#include <xrpl/tx/transactors/token/ConfidentialMPTConvertBack.h>

#include <xrpl/crypto/confidential.h>
#include <xrpl/ledger/helpers/ConfidentialMPT.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

namespace xrpl {
namespace {

bool
parseKey(Slice s, confidential::CompressedPoint& out)
{
    return confidential::parseCompressedPoint(s, out);
}

}  // namespace

XRPAmount
ConfidentialMPTConvertBack::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    return confidentialMptBaseFee(view, tx);
}

NotTEC
ConfidentialMPTConvertBack::preflight(PreflightContext const& ctx)
{
    auto const amount = ctx.tx[sfMPTAmount];
    // SPEC INCONSISTENCY: XLS-0096 rejects m = 0 with temBAD_AMOUNT; the
    // compact-sigma note allows m ≥ 0. Wire validation follows the XLS table.
    if (amount == 0 || amount > kMaxMpTokenAmount)
        return temBAD_AMOUNT;

    if (auto const ter = preflightCiphertext(ctx.tx[sfHolderEncryptedAmount]); !isTesSuccess(ter))
        return ter;
    if (auto const ter = preflightCiphertext(ctx.tx[sfIssuerEncryptedAmount]); !isTesSuccess(ter))
        return ter;
    if (auto const aud = ctx.tx[~sfAuditorEncryptedAmount]; aud)
    {
        if (auto const ter = preflightCiphertext(*aud); !isTesSuccess(ter))
            return ter;
    }
    if (auto const ter = preflightPoint33(ctx.tx[sfBalanceCommitment]); !isTesSuccess(ter))
        return ter;
    if (ctx.tx[sfZKProof].length() != confidential::kConvertBackZkProofBytes)
        return temMALFORMED;
    return tesSUCCESS;
}

TER
ConfidentialMPTConvertBack::preclaim(PreclaimContext const& ctx)
{
    auto const account = ctx.tx[sfAccount];
    auto const issuanceID = ctx.tx[sfMPTokenIssuanceID];
    auto const sleIssuance = ctx.view.read(keylet::mptIssuance(issuanceID));
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;
    if (auditorMigrationPending(*sleIssuance))
        return tecLOCKED;
    if (account == (*sleIssuance)[sfIssuer])
        return temMALFORMED;
    if (!sleIssuance->isFlag(lsfMPTCanHoldConfidentialBalance))
        return tecNO_PERMISSION;
    if (!sleIssuance->isFieldPresent(sfIssuerEncryptionKey))
        return tecNO_PERMISSION;

    auto const sleMpt = ctx.view.read(keylet::mptoken(issuanceID, account));
    if (!sleMpt)
        return tecOBJECT_NOT_FOUND;
    if (!sleMpt->isFieldPresent(sfHolderEncryptionKey) ||
        !sleMpt->isFieldPresent(sfConfidentialBalanceSpending))
        return tecNO_PERMISSION;

    auto const amount = ctx.tx[sfMPTAmount];
    if ((*sleIssuance)[sfConfidentialOutstandingAmount] < amount)
        return tecINSUFFICIENT_FUNDS;
    if ((*sleMpt)[sfMPTAmount] > kMaxMpTokenAmount - amount)
        return tecPATH_DRY;

    if (sleIssuance->isFieldPresent(sfAuditorEncryptionKey) !=
        ctx.tx.isFieldPresent(sfAuditorEncryptedAmount))
        return tecNO_PERMISSION;

    if (isFrozen(ctx.view, account, MPTIssue{issuanceID}))
        return tecLOCKED;

    return tesSUCCESS;
}

TER
ConfidentialMPTConvertBack::doApply()
{
    auto const account = accountID_;
    auto const issuanceID = ctx_.tx[sfMPTokenIssuanceID];
    auto sleIssuance = view().peek(keylet::mptIssuance(issuanceID));
    auto sleMpt = view().peek(keylet::mptoken(issuanceID, account));
    if (!sleIssuance || !sleMpt)
        return tecINTERNAL;

    confidential::CompressedPoint holderPk{};
    confidential::CompressedPoint issuerPk{};
    if (!parseKey((*sleMpt)[sfHolderEncryptionKey], holderPk) ||
        !parseKey((*sleIssuance)[sfIssuerEncryptionKey], issuerPk))
        return tecBAD_PROOF;

    std::optional<confidential::CompressedPoint> auditorPk;
    if (sleIssuance->isFieldPresent(sfAuditorEncryptionKey))
    {
        confidential::CompressedPoint pk{};
        if (!parseKey((*sleIssuance)[sfAuditorEncryptionKey], pk))
            return tecBAD_PROOF;
        auditorPk = pk;
    }

    confidential::Ciphertext holderCt{};
    confidential::Ciphertext issuerCt{};
    if (auto const ter = parseCiphertextField(ctx_.tx[sfHolderEncryptedAmount], holderCt);
        !isTesSuccess(ter))
        return ter;
    if (auto const ter = parseCiphertextField(ctx_.tx[sfIssuerEncryptedAmount], issuerCt);
        !isTesSuccess(ter))
        return ter;
    std::optional<confidential::Ciphertext> auditorCt;
    if (auto const aud = ctx_.tx[~sfAuditorEncryptedAmount]; aud)
    {
        confidential::Ciphertext ct{};
        if (auto const ter = parseCiphertextField(*aud, ct); !isTesSuccess(ter))
            return ter;
        auditorCt = ct;
    }

    confidential::Scalar r{};
    auto const bf = ctx_.tx[sfBlindingFactor];
    if (!confidential::parseScalar(Slice(bf.data(), bf.size()), r))
        return tecBAD_PROOF;

    auto const amount = ctx_.tx[sfMPTAmount];
    if (auto const ter = checkPlaintextCiphertexts(
            amount, r, holderPk, issuerPk, auditorPk, holderCt, issuerCt, auditorCt);
        !isTesSuccess(ter))
        return ter;

    confidential::Ciphertext spending{};
    if (auto const ter =
            parseCiphertextField((*sleMpt)[sfConfidentialBalanceSpending], spending);
        !isTesSuccess(ter))
        return ter;

    confidential::ConvertBackSigmaPublicInput pub;
    pub.holderKey = holderPk;
    pub.balanceC1 = spending.c1;
    pub.balanceC2 = spending.c2;
    if (!parseKey(ctx_.tx[sfBalanceCommitment], pub.balanceCommitment))
        return tecBAD_PROOF;

    auto const version = (*sleMpt)[~sfConfidentialBalanceVersion].valueOr(0);
    auto const ctxId = confidential::transactionContextIDConvertBack(
        static_cast<std::uint16_t>(ctx_.tx.getTxnType()),
        Slice(account.data(), account.size()),
        Slice(issuanceID.data(), issuanceID.size()),
        ctx_.tx.getSeqValue(),
        version);

    auto const zk = ctx_.tx[sfZKProof];
    if (zk.length() != confidential::kConvertBackZkProofBytes)
        return tecBAD_PROOF;
    if (!confidential::verifyConvertBackSigma(
            pub,
            Slice(ctxId.data(), ctxId.size()),
            Slice(zk.data(), confidential::kConvertBackSigmaProofBytes)))
        return tecBAD_PROOF;

    confidential::CompressedPoint amountG{};
    confidential::CompressedPoint remaining{};
    auto const mScalar = confidential::amountToScalar(amount);
    if (!confidential::pointMulBase(mScalar, amountG) ||
        !confidential::pointSub(pub.balanceCommitment, amountG, remaining))
        return tecBAD_PROOF;
    if (!confidential::verifyBulletproofSingle(
            remaining,
            Slice(
                zk.data() + confidential::kConvertBackSigmaProofBytes,
                confidential::kSingleBulletproofBytes)))
        return tecBAD_PROOF;

    confidential::Scalar e{};
    if (!confidential::extractSigmaChallenge(
            Slice(zk.data(), confidential::kConvertBackSigmaProofBytes), e))
        return tecBAD_PROOF;

    confidential::Ciphertext newSpend{};
    if (!confidential::elgamalSub(spending, holderCt, holderPk, e, newSpend))
        return tecBAD_PROOF;
    setCiphertextField(*sleMpt, sfConfidentialBalanceSpending, newSpend);

    if (sleMpt->isFieldPresent(sfIssuerEncryptedBalance))
    {
        confidential::Ciphertext issBal{};
        if (auto const ter = parseCiphertextField((*sleMpt)[sfIssuerEncryptedBalance], issBal);
            !isTesSuccess(ter))
            return ter;
        confidential::Ciphertext next{};
        if (!confidential::elgamalSub(issBal, issuerCt, issuerPk, e, next))
            return tecBAD_PROOF;
        setCiphertextField(*sleMpt, sfIssuerEncryptedBalance, next);
    }
    if (auditorCt && sleMpt->isFieldPresent(sfAuditorEncryptedBalance))
    {
        confidential::Ciphertext audBal{};
        if (auto const ter = parseCiphertextField((*sleMpt)[sfAuditorEncryptedBalance], audBal);
            !isTesSuccess(ter))
            return ter;
        confidential::Ciphertext next{};
        if (!confidential::elgamalSub(audBal, *auditorCt, *auditorPk, e, next))
            return tecBAD_PROOF;
        setCiphertextField(*sleMpt, sfAuditorEncryptedBalance, next);
    }

    (*sleMpt)[sfMPTAmount] = (*sleMpt)[sfMPTAmount] + amount;
    (*sleIssuance)[sfConfidentialOutstandingAmount] =
        (*sleIssuance)[sfConfidentialOutstandingAmount] - amount;

    // Once global confidential supply reaches zero, non-negativity guarantees
    // every confidential component is zero. The final converter can leave
    // confidential mode immediately; other zero holders may self-delete via
    // MPTokenAuthorize under the same global-zero condition.
    if ((*sleIssuance)[sfConfidentialOutstandingAmount] == 0)
    {
        if (auto const ter = clearConfidentialState(*sleIssuance, *sleMpt);
            !isTesSuccess(ter))
            return ter;
    }
    else
    {
        incrementConfidentialVersion(*sleMpt);
    }
    view().update(sleMpt);
    view().update(sleIssuance);
    return tesSUCCESS;
}

void
ConfidentialMPTConvertBack::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
}

bool
ConfidentialMPTConvertBack::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
