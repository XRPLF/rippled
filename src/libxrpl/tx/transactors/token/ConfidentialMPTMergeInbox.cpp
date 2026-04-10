#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/transactors/token/ConfidentialMPTMergeInbox.h>

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

TER
ConfidentialMPTMergeInbox::preclaim(PreclaimContext const& ctx)
{
    auto const sleIssuance = ctx.view.read(keylet::mptIssuance(ctx.tx[sfMPTokenIssuanceID]));
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    if (!sleIssuance->isFlag(lsfMPTCanConfidentialAmount))
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
        return tecNO_PERMISSION;

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
    auto sleMptoken = view().peek(keylet::mptoken(mptIssuanceID, account_));
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
        return tecINTERNAL;  // LCOV_EXCL_LINE

    (*sleMptoken)[sfConfidentialBalanceSpending] = std::move(*sum);

    // Reset inbox to encrypted zero. Must use canonical zero encryption
    // (deterministic ciphertext) so the ledger state is reproducible.
    auto zeroEncryption =
        encryptCanonicalZeroAmount((*sleMptoken)[sfHolderEncryptionKey], account_, mptIssuanceID);

    if (!zeroEncryption)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    (*sleMptoken)[sfConfidentialBalanceInbox] = std::move(*zeroEncryption);

    incrementConfidentialVersion(*sleMptoken);

    view().update(sleMptoken);
    return tesSUCCESS;
}

}  // namespace xrpl
