#include <xrpld/app/tx/detail/ConfidentialMergeInbox.h>

#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace xrpl {

NotTEC
ConfidentialMergeInbox::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureConfidentialTransfer))
        return temDISABLED;

    // issuer cannot merge
    if (MPTIssue(ctx.tx[sfMPTokenIssuanceID]).getIssuer() == ctx.tx[sfAccount])
        return temMALFORMED;

    return tesSUCCESS;
}

TER
ConfidentialMergeInbox::preclaim(PreclaimContext const& ctx)
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
ConfidentialMergeInbox::doApply()
{
    auto const mptIssuanceID = ctx_.tx[sfMPTokenIssuanceID];
    auto sleMptoken = view().peek(keylet::mptoken(mptIssuanceID, account_));
    if (!sleMptoken)
        return tecINTERNAL;

    // sanity check
    if (!sleMptoken->isFieldPresent(sfConfidentialBalanceSpending) ||
        !sleMptoken->isFieldPresent(sfConfidentialBalanceInbox) ||
        !sleMptoken->isFieldPresent(sfHolderElGamalPublicKey))
    {
        return tecINTERNAL;
    }

    // homomorphically add holder's encrypted balance
    Buffer sum(ecGamalEncryptedTotalLength);
    if (TER const ter = homomorphicAdd(
            (*sleMptoken)[sfConfidentialBalanceSpending], (*sleMptoken)[sfConfidentialBalanceInbox], sum);
        !isTesSuccess(ter))
        return tecINTERNAL;

    (*sleMptoken)[sfConfidentialBalanceSpending] = sum;

    auto const zeroEncryption =
        encryptCanonicalZeroAmount((*sleMptoken)[sfHolderElGamalPublicKey], account_, mptIssuanceID);

    if (!zeroEncryption)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    (*sleMptoken)[sfConfidentialBalanceInbox] = *zeroEncryption;

    // it's fine if it reaches max uint32, it just resets to 0
    (*sleMptoken)[sfConfidentialBalanceVersion] = (*sleMptoken)[~sfConfidentialBalanceVersion].value_or(0u) + 1u;

    view().update(sleMptoken);
    return tesSUCCESS;
}

}  // namespace xrpl
