#include <xrpl/tx/transactors/token/ConfidentialMPTMergeInbox.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <memory>
#include <utility>

namespace xrpl {

NotTEC
ConfidentialMPTMergeInbox::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureConfidentialTransfer))
        return temDISABLED;

    // issuer cannot merge
    if (MPTIssue(ctx.tx[sfMPTokenIssuanceID]).getIssuer() == ctx.tx[sfAccount])
        return temMALFORMED;

    return tesSUCCESS;
}

XRPAmount
ConfidentialMPTMergeInbox::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    return Transactor::calculateBaseFee(view, tx, kConfidentialFeeMultiplier);
}

TER
ConfidentialMPTMergeInbox::preclaim(PreclaimContext const& ctx)
{
    auto const sleIssuance = ctx.view.read(keylet::mptokenIssuance(ctx.tx[sfMPTokenIssuanceID]));
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    if (!sleIssuance->isFlag(lsfMPTCanHoldConfidentialBalance))
        return tecNO_PERMISSION;

    // already checked in preflight, but should also check that issuer on the
    // issuance isn't the account either
    if (sleIssuance->getAccountID(sfIssuer) == ctx.tx[sfAccount])
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const sleMptoken =
        ctx.view.read(keylet::mptoken(ctx.tx[sfMPTokenIssuanceID], ctx.tx[sfAccount]));
    if (!sleMptoken)
        return tecOBJECT_NOT_FOUND;

    if (!sleMptoken->isFieldPresent(sfConfidentialBalanceInbox) ||
        !sleMptoken->isFieldPresent(sfConfidentialBalanceSpending) ||
        !sleMptoken->isFieldPresent(sfHolderEncryptionKey))
    {
        return tecNO_PERMISSION;
    }

    // Check lock
    auto const account = ctx.tx[sfAccount];
    MPTIssue const mptIssue(ctx.tx[sfMPTokenIssuanceID]);
    if (auto const ter = checkFrozen(ctx.view, account, mptIssue); !isTesSuccess(ter))
        return ter;

    // Check auth
    if (auto const ter = requireAuth(ctx.view, mptIssue, account); !isTesSuccess(ter))
        return ter;

    return tesSUCCESS;
}

TER
ConfidentialMPTMergeInbox::doApply()
{
    auto const mptIssuanceID = ctx_.tx[sfMPTokenIssuanceID];
    auto sleMptoken = view().peek(keylet::mptoken(mptIssuanceID, accountID_));
    if (!sleMptoken)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // sanity check
    if (!sleMptoken->isFieldPresent(sfConfidentialBalanceSpending) ||
        !sleMptoken->isFieldPresent(sfConfidentialBalanceInbox) ||
        !sleMptoken->isFieldPresent(sfHolderEncryptionKey))
    {
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    // Merge inbox into spending: spending = spending + inbox
    // This allows holder to use received funds. Without merging, incoming
    // transfers sit in inbox and cannot be spent or converted back.
    auto sum = homomorphicAdd(
        (*sleMptoken)[sfConfidentialBalanceSpending], (*sleMptoken)[sfConfidentialBalanceInbox]);
    if (!sum)
    {
        // LCOV_EXCL_START
        JLOG(ctx_.journal.error())
            << "ConfidentialMPTMergeInbox failed homomorphic add for inbox merge.";
        return tecINTERNAL;
        // LCOV_EXCL_STOP
    }

    (*sleMptoken)[sfConfidentialBalanceSpending] = std::move(*sum);

    // Reset inbox to encrypted zero. Must use canonical zero encryption
    // (deterministic ciphertext) so the ledger state is reproducible.
    auto zeroEncryption =
        encryptCanonicalZeroAmount((*sleMptoken)[sfHolderEncryptionKey], accountID_, mptIssuanceID);

    if (!zeroEncryption)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    (*sleMptoken)[sfConfidentialBalanceInbox] = std::move(*zeroEncryption);

    incrementConfidentialVersion(*sleMptoken);

    view().update(sleMptoken);
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
