#include <xrpl/tx/transactors/token/ConfidentialMPTConvert.h>

#include <xrpl/crypto/confidential.h>
#include <xrpl/ledger/helpers/ConfidentialMPT.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

#include <limits>

namespace xrpl {
namespace {

bool
parseKey(Slice s, confidential::CompressedPoint& out)
{
    return confidential::parseCompressedPoint(s, out);
}

}  // namespace

XRPAmount
ConfidentialMPTConvert::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    return confidentialMptBaseFee(view, tx);
}

NotTEC
ConfidentialMPTConvert::preflight(PreflightContext const& ctx)
{
    // sfBlindingFactor is UINT256, so the codec rejects non-32-byte values
    // before preflight constructs an STTx.
    static_assert(uint256::kBytes == confidential::kScalarBytes);

    auto const amount = ctx.tx[sfMPTAmount];
    if (amount > kMaxMpTokenAmount)
        return temBAD_AMOUNT;

    if (auto const key = ctx.tx[~sfHolderEncryptionKey]; key)
    {
        if (key->length() != confidential::kCompressedPointBytes)
            return temMALFORMED;
        if (!ctx.tx.isFieldPresent(sfZKProof))
            return temMALFORMED;
        if (ctx.tx[sfZKProof].length() != confidential::kSchnorrRegisterProofBytes)
            return temMALFORMED;
    }
    else if (ctx.tx.isFieldPresent(sfZKProof))
    {
        return temMALFORMED;
    }

    if (auto const ter = preflightCiphertext(ctx.tx[sfHolderEncryptedAmount]); !isTesSuccess(ter))
        return ter;
    if (auto const ter = preflightCiphertext(ctx.tx[sfIssuerEncryptedAmount]); !isTesSuccess(ter))
        return ter;
    if (auto const aud = ctx.tx[~sfAuditorEncryptedAmount]; aud)
    {
        if (auto const ter = preflightCiphertext(*aud); !isTesSuccess(ter))
            return ter;
    }
    return tesSUCCESS;
}

TER
ConfidentialMPTConvert::preclaim(PreclaimContext const& ctx)
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

    auto const amount = ctx.tx[sfMPTAmount];
    if ((*sleMpt)[sfMPTAmount] < amount)
        return tecINSUFFICIENT_FUNDS;

    if (sleIssuance->isFieldPresent(sfAuditorEncryptionKey) !=
        ctx.tx.isFieldPresent(sfAuditorEncryptedAmount))
        return tecNO_PERMISSION;

    bool const initializing = !sleMpt->isFieldPresent(sfHolderEncryptionKey);
    if (initializing)
    {
        if (!ctx.tx.isFieldPresent(sfHolderEncryptionKey) || !ctx.tx.isFieldPresent(sfZKProof))
            return tecNO_PERMISSION;
    }
    else if (ctx.tx.isFieldPresent(sfHolderEncryptionKey))
    {
        return tecDUPLICATE;
    }

    if (isFrozen(ctx.view, account, MPTIssue{issuanceID}))
        return tecLOCKED;

    return tesSUCCESS;
}

TER
ConfidentialMPTConvert::doApply()
{
    auto const account = accountID_;
    auto const issuanceID = ctx_.tx[sfMPTokenIssuanceID];
    auto sleIssuance = view().peek(keylet::mptIssuance(issuanceID));
    auto sleMpt = view().peek(keylet::mptoken(issuanceID, account));
    if (!sleIssuance || !sleMpt)
        return tecINTERNAL;

    confidential::CompressedPoint holderPk{};
    confidential::CompressedPoint issuerPk{};
    if (!parseKey((*sleIssuance)[sfIssuerEncryptionKey], issuerPk))
        return tecBAD_PROOF;

    bool const initializing = !sleMpt->isFieldPresent(sfHolderEncryptionKey);
    if (initializing)
    {
        if (!parseKey(ctx_.tx[sfHolderEncryptionKey], holderPk))
            return tecBAD_PROOF;
        auto const ctxId = confidential::transactionContextIDConvert(
            static_cast<std::uint16_t>(ctx_.tx.getTxnType()),
            Slice(account.data(), account.size()),
            Slice(issuanceID.data(), issuanceID.size()),
            ctx_.tx.getSeqValue());
        if (!confidential::verifySchnorrRegister(
                holderPk,
                Slice(ctxId.data(), ctxId.size()),
                ctx_.tx[sfZKProof]))
            return tecBAD_PROOF;
    }
    else if (!parseKey((*sleMpt)[sfHolderEncryptionKey], holderPk))
    {
        return tecBAD_PROOF;
    }

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

    (*sleMpt)[sfMPTAmount] = (*sleMpt)[sfMPTAmount] - amount;
    (*sleIssuance)[sfConfidentialOutstandingAmount] =
        (*sleIssuance)[sfConfidentialOutstandingAmount] + amount;

    if (initializing)
    {
        auto const holderCount = (*sleIssuance)[sfConfidentialHolderCount];
        if (holderCount == std::numeric_limits<std::uint32_t>::max())
            return tecINTERNAL;
        (*sleIssuance)[sfConfidentialHolderCount] = holderCount + 1;

        sleMpt->setFieldVL(
            sfHolderEncryptionKey,
            Slice(holderPk.data(), holderPk.size()));
        confidential::Ciphertext zeroSpend{};
        if (auto const ter = encZeroFor(view(), *sleIssuance, account, holderPk, zeroSpend);
            !isTesSuccess(ter))
            return ter;
        confidential::CiphertextBytes raw{};
        confidential::serializeCiphertext(zeroSpend, Slice(raw.data(), raw.size()));
        sleMpt->setFieldVL(sfConfidentialBalanceSpending, Slice(raw.data(), raw.size()));
        sleMpt->setFieldVL(sfConfidentialBalanceInbox, Slice(raw.data(), raw.size()));
        confidential::Ciphertext zeroIss{};
        if (auto const ter = encZeroFor(view(), *sleIssuance, account, issuerPk, zeroIss);
            !isTesSuccess(ter))
            return ter;
        confidential::serializeCiphertext(zeroIss, Slice(raw.data(), raw.size()));
        sleMpt->setFieldVL(sfIssuerEncryptedBalance, Slice(raw.data(), raw.size()));
        if (auditorPk)
        {
            confidential::Ciphertext zeroA{};
            if (auto const ter = encZeroFor(view(), *sleIssuance, account, *auditorPk, zeroA);
                !isTesSuccess(ter))
                return ter;
            confidential::serializeCiphertext(zeroA, Slice(raw.data(), raw.size()));
            sleMpt->setFieldVL(sfAuditorEncryptedBalance, Slice(raw.data(), raw.size()));
            sleMpt->setFieldU32(
                sfAuditorKeyVersion,
                (*sleIssuance)[sfAuditorKeyVersion]);
        }
        sleMpt->setFieldU32(sfConfidentialBalanceVersion, 0);
    }

    confidential::Ciphertext inbox{};
    confidential::Ciphertext issuerBal{};
    if (auto const ter = parseCiphertextField((*sleMpt)[sfConfidentialBalanceInbox], inbox);
        !isTesSuccess(ter))
        return ter;
    if (auto const ter = parseCiphertextField((*sleMpt)[sfIssuerEncryptedBalance], issuerBal);
        !isTesSuccess(ter))
        return ter;
    confidential::Ciphertext newInbox{};
    confidential::Ciphertext newIssuer{};
    if (!confidential::elgamalAdd(inbox, holderCt, newInbox) ||
        !confidential::elgamalAdd(issuerBal, issuerCt, newIssuer))
        return tecBAD_PROOF;
    confidential::CiphertextBytes raw{};
    confidential::serializeCiphertext(newInbox, Slice(raw.data(), raw.size()));
    sleMpt->setFieldVL(sfConfidentialBalanceInbox, Slice(raw.data(), raw.size()));
    confidential::serializeCiphertext(newIssuer, Slice(raw.data(), raw.size()));
    sleMpt->setFieldVL(sfIssuerEncryptedBalance, Slice(raw.data(), raw.size()));
    if (auditorCt && sleMpt->isFieldPresent(sfAuditorEncryptedBalance))
    {
        confidential::Ciphertext audBal{};
        if (auto const ter = parseCiphertextField((*sleMpt)[sfAuditorEncryptedBalance], audBal);
            !isTesSuccess(ter))
            return ter;
        confidential::Ciphertext newAud{};
        if (!confidential::elgamalAdd(audBal, *auditorCt, newAud))
            return tecBAD_PROOF;
        confidential::serializeCiphertext(newAud, Slice(raw.data(), raw.size()));
        sleMpt->setFieldVL(sfAuditorEncryptedBalance, Slice(raw.data(), raw.size()));
    }

    view().update(sleMpt);
    view().update(sleIssuance);
    return tesSUCCESS;
}

void
ConfidentialMPTConvert::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
}

bool
ConfidentialMPTConvert::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
