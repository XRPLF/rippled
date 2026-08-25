#include <xrpl/tx/transactors/token/ConfidentialMPTMergeInbox.h>

#include <xrpl/crypto/confidential.h>
#include <xrpl/ledger/helpers/ConfidentialMPT.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

namespace xrpl {

XRPAmount
ConfidentialMPTMergeInbox::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    return confidentialMptBaseFee(view, tx);
}

NotTEC
ConfidentialMPTMergeInbox::preflight(PreflightContext const&)
{
    return tesSUCCESS;
}

TER
ConfidentialMPTMergeInbox::preclaim(PreclaimContext const& ctx)
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
    auto const sleMpt = ctx.view.read(keylet::mptoken(issuanceID, account));
    if (!sleMpt)
        return tecOBJECT_NOT_FOUND;
    if (!sleMpt->isFieldPresent(sfConfidentialBalanceInbox) ||
        !sleMpt->isFieldPresent(sfConfidentialBalanceSpending))
        return tecNO_PERMISSION;
    if (isFrozen(ctx.view, account, MPTIssue{issuanceID}))
        return tecLOCKED;
    return tesSUCCESS;
}

TER
ConfidentialMPTMergeInbox::doApply()
{
    auto const account = accountID_;
    auto const issuanceID = ctx_.tx[sfMPTokenIssuanceID];
    auto sleIssuance = view().peek(keylet::mptIssuance(issuanceID));
    auto sleMpt = view().peek(keylet::mptoken(issuanceID, account));
    if (!sleIssuance || !sleMpt)
        return tecINTERNAL;

    confidential::Ciphertext inbox{};
    confidential::Ciphertext spending{};
    if (auto const ter = parseCiphertextField((*sleMpt)[sfConfidentialBalanceInbox], inbox);
        !isTesSuccess(ter))
        return ter;
    if (auto const ter =
            parseCiphertextField((*sleMpt)[sfConfidentialBalanceSpending], spending);
        !isTesSuccess(ter))
        return ter;
    confidential::Ciphertext merged{};
    if (!confidential::elgamalAdd(spending, inbox, merged))
        return tecINTERNAL;

    confidential::CompressedPoint holderPk{};
    if (!confidential::parseCompressedPoint((*sleMpt)[sfHolderEncryptionKey], holderPk))
        return tecINTERNAL;
    confidential::Ciphertext zero{};
    if (auto const ter = encZeroFor(view(), *sleIssuance, account, holderPk, zero);
        !isTesSuccess(ter))
        return ter;

    confidential::CiphertextBytes raw{};
    confidential::serializeCiphertext(merged, Slice(raw.data(), raw.size()));
    sleMpt->setFieldVL(sfConfidentialBalanceSpending, Slice(raw.data(), raw.size()));
    confidential::serializeCiphertext(zero, Slice(raw.data(), raw.size()));
    sleMpt->setFieldVL(sfConfidentialBalanceInbox, Slice(raw.data(), raw.size()));
    incrementConfidentialVersion(*sleMpt);
    view().update(sleMpt);
    return tesSUCCESS;
}

void
ConfidentialMPTMergeInbox::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
}

bool
ConfidentialMPTMergeInbox::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
