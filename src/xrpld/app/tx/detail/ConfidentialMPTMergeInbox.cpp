#include <xrpld/app/tx/detail/ConfidentialMPTMergeInbox.h>

#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

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

    if (!sleIssuance->isFlag(lsfMPTCanPrivacy))
        return tecNO_PERMISSION;

    // already checked in preflight, but should also check that issuer on the
    // issuance isn't the account either
    if (sleIssuance->getAccountID(sfIssuer) == ctx.tx[sfAccount])
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const sleMptoken = ctx.view.read(keylet::mptoken(ctx.tx[sfMPTokenIssuanceID], ctx.tx[sfAccount]));
    if (!sleMptoken)
        return tecOBJECT_NOT_FOUND;

    if (!sleMptoken->isFieldPresent(sfConfidentialBalanceInbox) ||
        !sleMptoken->isFieldPresent(sfConfidentialBalanceSpending) ||
        !sleMptoken->isFieldPresent(sfHolderElGamalPublicKey))
        return tecNO_PERMISSION;

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
        !sleMptoken->isFieldPresent(sfHolderElGamalPublicKey))
    {
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    // Merge inbox into spending: spending = spending + inbox
    // This allows holder to use received funds. Without merging, incoming
    // transfers sit in inbox and cannot be spent or converted back.
    Buffer sum(ecGamalEncryptedTotalLength);
    if (TER const ter = homomorphicAdd(
            (*sleMptoken)[sfConfidentialBalanceSpending], (*sleMptoken)[sfConfidentialBalanceInbox], sum);
        !isTesSuccess(ter))
        return tecINTERNAL;  // LCOV_EXCL_LINE

    (*sleMptoken)[sfConfidentialBalanceSpending] = sum;

    // Reset inbox to encrypted zero. Must use canonical zero encryption
    // (deterministic ciphertext) so the ledger state is reproducible.
    auto const zeroEncryption =
        encryptCanonicalZeroAmount((*sleMptoken)[sfHolderElGamalPublicKey], account_, mptIssuanceID);

    if (!zeroEncryption)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    (*sleMptoken)[sfConfidentialBalanceInbox] = *zeroEncryption;

    incrementConfidentialVersion(*sleMptoken);

    view().update(sleMptoken);
    return tesSUCCESS;
}

}  // namespace xrpl
