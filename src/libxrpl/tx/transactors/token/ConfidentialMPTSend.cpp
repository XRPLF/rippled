#include <xrpl/tx/transactors/token/ConfidentialMPTSend.h>

#include <xrpl/crypto/confidential.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/ConfidentialMPT.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace xrpl {
namespace {

bool
parseKey(Slice s, confidential::CompressedPoint& out)
{
    return confidential::parseCompressedPoint(s, out);
}

}  // namespace

XRPAmount
ConfidentialMPTSend::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    return confidentialMptBaseFee(view, tx);
}

NotTEC
ConfidentialMPTSend::preflight(PreflightContext const& ctx)
{
    if (ctx.tx.getAccountID(sfDestination) == ctx.tx.getAccountID(sfAccount))
        return temREDUNDANT;

    if (auto const ter = preflightCiphertext(ctx.tx[sfSenderEncryptedAmount]); !isTesSuccess(ter))
        return ter;
    if (auto const ter = preflightCiphertext(ctx.tx[sfDestinationEncryptedAmount]);
        !isTesSuccess(ter))
        return ter;
    if (auto const ter = preflightCiphertext(ctx.tx[sfIssuerEncryptedAmount]); !isTesSuccess(ter))
        return ter;
    if (auto const aud = ctx.tx[~sfAuditorEncryptedAmount]; aud)
    {
        if (auto const ter = preflightCiphertext(*aud); !isTesSuccess(ter))
            return ter;
    }

    if (auto const ter = preflightPoint33(ctx.tx[sfAmountCommitment]); !isTesSuccess(ter))
        return ter;
    if (auto const ter = preflightPoint33(ctx.tx[sfBalanceCommitment]); !isTesSuccess(ter))
        return ter;

    if (ctx.tx[sfZKProof].length() != confidential::kSendZkProofBytes)
        return temMALFORMED;

    if (ctx.tx.isFieldPresent(sfCredentialIDs) && !ctx.rules.enabled(featureCredentials))
        return temDISABLED;

    return tesSUCCESS;
}

TER
ConfidentialMPTSend::preclaim(PreclaimContext const& ctx)
{
    auto const account = ctx.tx[sfAccount];
    auto const dest = ctx.tx[sfDestination];
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
    if (sleIssuance->isFieldPresent(sfTransferFee) && (*sleIssuance)[sfTransferFee] != 0)
        return tecNO_PERMISSION;

    if (!ctx.view.exists(keylet::account(dest)))
        return tecNO_DST;

    auto const sleDstAcct = ctx.view.read(keylet::account(dest));
    if (sleDstAcct && sleDstAcct->isFlag(lsfRequireDestTag) &&
        !ctx.tx.isFieldPresent(sfDestinationTag))
        return tecDST_TAG_NEEDED;

    MPTIssue const issue{issuanceID};
    if (auto const ter = requireAuth(ctx.view, issue, account); !isTesSuccess(ter))
        return ter;
    if (auto const ter = requireAuth(ctx.view, issue, dest); !isTesSuccess(ter))
        return ter;
    if (auto const ter = canTransfer(ctx.view, issue, account, dest); !isTesSuccess(ter))
        return ter;
    if (isAnyFrozen(ctx.view, {account, dest}, issue))
        return tecLOCKED;

    auto const sleSrc = ctx.view.read(keylet::mptoken(issuanceID, account));
    auto const sleDst = ctx.view.read(keylet::mptoken(issuanceID, dest));
    if (!sleSrc || !sleDst)
        return tecOBJECT_NOT_FOUND;
    if (!sleSrc->isFieldPresent(sfHolderEncryptionKey) ||
        !sleSrc->isFieldPresent(sfConfidentialBalanceSpending) ||
        !sleDst->isFieldPresent(sfHolderEncryptionKey) ||
        !sleDst->isFieldPresent(sfConfidentialBalanceInbox))
        return tecNO_PERMISSION;

    if (sleIssuance->isFieldPresent(sfAuditorEncryptionKey) !=
        ctx.tx.isFieldPresent(sfAuditorEncryptedAmount))
        return tecNO_PERMISSION;

    if (auto const err = credentials::valid(ctx.tx, ctx.view, account, ctx.j); !isTesSuccess(err))
        return err;

    return tesSUCCESS;
}

TER
ConfidentialMPTSend::doApply()
{
    auto const account = accountID_;
    auto const dest = ctx_.tx[sfDestination];
    auto const issuanceID = ctx_.tx[sfMPTokenIssuanceID];

    auto sleIssuance = view().peek(keylet::mptIssuance(issuanceID));
    auto sleSrc = view().peek(keylet::mptoken(issuanceID, account));
    auto sleDst = view().peek(keylet::mptoken(issuanceID, dest));
    auto sleDstAcct = view().peek(keylet::account(dest));
    if (!sleIssuance || !sleSrc || !sleDst || !sleDstAcct)
        return tecINTERNAL;

    if (auto const ter =
            verifyDepositPreauth(ctx_.tx, view(), account, dest, sleDstAcct, ctx_.journal);
        !isTesSuccess(ter))
        return ter;

    confidential::CompressedPoint senderPk{};
    confidential::CompressedPoint destPk{};
    confidential::CompressedPoint issuerPk{};
    if (!parseKey((*sleSrc)[sfHolderEncryptionKey], senderPk) ||
        !parseKey((*sleDst)[sfHolderEncryptionKey], destPk) ||
        !parseKey((*sleIssuance)[sfIssuerEncryptionKey], issuerPk))
        return tecBAD_PROOF;

    confidential::Ciphertext encSender{};
    confidential::Ciphertext encDest{};
    confidential::Ciphertext encIssuer{};
    if (auto const ter = parseCiphertextField(ctx_.tx[sfSenderEncryptedAmount], encSender);
        !isTesSuccess(ter))
        return ter;
    if (auto const ter = parseCiphertextField(ctx_.tx[sfDestinationEncryptedAmount], encDest);
        !isTesSuccess(ter))
        return ter;
    if (auto const ter = parseCiphertextField(ctx_.tx[sfIssuerEncryptedAmount], encIssuer);
        !isTesSuccess(ter))
        return ter;
    if (!sameC1(encSender, encDest) || !sameC1(encSender, encIssuer))
        return tecBAD_PROOF;

    confidential::SendSigmaPublicInput pub;
    pub.senderKey = senderPk;
    pub.c1 = encSender.c1;
    pub.recipientKeys = {senderPk, destPk, issuerPk};
    pub.c2 = {encSender.c2, encDest.c2, encIssuer.c2};

    std::optional<confidential::Ciphertext> encAud;
    std::optional<confidential::CompressedPoint> auditorPk;
    if (sleIssuance->isFieldPresent(sfAuditorEncryptionKey))
    {
        confidential::CompressedPoint pk{};
        if (!parseKey((*sleIssuance)[sfAuditorEncryptionKey], pk))
            return tecBAD_PROOF;
        auditorPk = pk;
        confidential::Ciphertext ct{};
        if (!ctx_.tx.isFieldPresent(sfAuditorEncryptedAmount))
            return tecNO_PERMISSION;
        if (auto const ter = parseCiphertextField(ctx_.tx[sfAuditorEncryptedAmount], ct);
            !isTesSuccess(ter))
            return ter;
        if (!sameC1(encSender, ct))
            return tecBAD_PROOF;
        encAud = ct;
        pub.recipientKeys.push_back(pk);
        pub.c2.push_back(ct.c2);
    }

    if (!parseKey(ctx_.tx[sfAmountCommitment], pub.amountCommitment) ||
        !parseKey(ctx_.tx[sfBalanceCommitment], pub.balanceCommitment))
        return tecBAD_PROOF;

    confidential::Ciphertext spending{};
    if (auto const ter =
            parseCiphertextField((*sleSrc)[sfConfidentialBalanceSpending], spending);
        !isTesSuccess(ter))
        return ter;
    pub.balanceC1 = spending.c1;
    pub.balanceC2 = spending.c2;

    auto const version = (*sleSrc)[~sfConfidentialBalanceVersion].valueOr(0);
    auto const ctxId = confidential::transactionContextIDSend(
        static_cast<std::uint16_t>(ctx_.tx.getTxnType()),
        Slice(account.data(), account.size()),
        Slice(issuanceID.data(), issuanceID.size()),
        ctx_.tx.getSeqValue(),
        Slice(dest.data(), dest.size()),
        version);

    auto const zk = ctx_.tx[sfZKProof];
    if (zk.length() != confidential::kSendZkProofBytes)
        return tecBAD_PROOF;
    if (!confidential::verifySendSigma(
            pub, Slice(ctxId.data(), ctxId.size()), Slice(zk.data(), confidential::kSendSigmaProofBytes)))
        return tecBAD_PROOF;

    confidential::CompressedPoint remaining{};
    if (!confidential::pointSub(pub.balanceCommitment, pub.amountCommitment, remaining))
        return tecBAD_PROOF;
    if (!confidential::verifyBulletproofSend(
            pub.amountCommitment,
            remaining,
            Slice(
                zk.data() + confidential::kSendSigmaProofBytes,
                confidential::kAggregatedBulletproofBytes)))
        return tecBAD_PROOF;

    confidential::Scalar e{};
    if (!confidential::extractSigmaChallenge(
            Slice(zk.data(), confidential::kSendSigmaProofBytes), e))
        return tecBAD_PROOF;

    confidential::Ciphertext newSpend{};
    if (!confidential::elgamalSub(spending, encSender, senderPk, e, newSpend))
        return tecBAD_PROOF;
    setCiphertextField(*sleSrc, sfConfidentialBalanceSpending, newSpend);

    if (sleSrc->isFieldPresent(sfIssuerEncryptedBalance))
    {
        confidential::Ciphertext issBal{};
        if (auto const ter = parseCiphertextField((*sleSrc)[sfIssuerEncryptedBalance], issBal);
            !isTesSuccess(ter))
            return ter;
        confidential::Ciphertext next{};
        if (!confidential::elgamalSub(issBal, encIssuer, issuerPk, e, next))
            return tecBAD_PROOF;
        setCiphertextField(*sleSrc, sfIssuerEncryptedBalance, next);
    }
    if (encAud && sleSrc->isFieldPresent(sfAuditorEncryptedBalance))
    {
        confidential::Ciphertext audBal{};
        if (auto const ter = parseCiphertextField((*sleSrc)[sfAuditorEncryptedBalance], audBal);
            !isTesSuccess(ter))
            return ter;
        confidential::Ciphertext next{};
        if (!confidential::elgamalSub(audBal, *encAud, *auditorPk, e, next))
            return tecBAD_PROOF;
        setCiphertextField(*sleSrc, sfAuditorEncryptedBalance, next);
    }

    confidential::Ciphertext inbox{};
    if (auto const ter = parseCiphertextField((*sleDst)[sfConfidentialBalanceInbox], inbox);
        !isTesSuccess(ter))
        return ter;
    confidential::Ciphertext credited{};
    confidential::Ciphertext rerand{};
    if (!confidential::elgamalAdd(inbox, encDest, credited) ||
        !confidential::elgamalRerandomize(credited, destPk, e, rerand))
        return tecBAD_PROOF;
    setCiphertextField(*sleDst, sfConfidentialBalanceInbox, rerand);

    if (sleDst->isFieldPresent(sfIssuerEncryptedBalance))
    {
        confidential::Ciphertext issBal{};
        if (auto const ter = parseCiphertextField((*sleDst)[sfIssuerEncryptedBalance], issBal);
            !isTesSuccess(ter))
            return ter;
        confidential::Ciphertext creditedI{};
        confidential::Ciphertext rerandI{};
        if (!confidential::elgamalAdd(issBal, encIssuer, creditedI) ||
            !confidential::elgamalRerandomize(creditedI, issuerPk, e, rerandI))
            return tecBAD_PROOF;
        setCiphertextField(*sleDst, sfIssuerEncryptedBalance, rerandI);
    }
    if (encAud && auditorPk && sleDst->isFieldPresent(sfAuditorEncryptedBalance))
    {
        confidential::Ciphertext audBal{};
        if (auto const ter = parseCiphertextField((*sleDst)[sfAuditorEncryptedBalance], audBal);
            !isTesSuccess(ter))
            return ter;
        confidential::Ciphertext creditedA{};
        confidential::Ciphertext rerandA{};
        if (!confidential::elgamalAdd(audBal, *encAud, creditedA) ||
            !confidential::elgamalRerandomize(creditedA, *auditorPk, e, rerandA))
            return tecBAD_PROOF;
        setCiphertextField(*sleDst, sfAuditorEncryptedBalance, rerandA);
    }

    incrementConfidentialVersion(*sleSrc);
    view().update(sleSrc);
    view().update(sleDst);
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
