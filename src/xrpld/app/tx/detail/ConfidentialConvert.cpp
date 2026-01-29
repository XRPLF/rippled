#include <xrpld/app/tx/detail/ConfidentialConvert.h>

#include <xrpl/ledger/View.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace xrpl {

NotTEC
ConfidentialConvert::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureConfidentialTransfer))
        return temDISABLED;

    // issuer cannot convert
    if (MPTIssue(ctx.tx[sfMPTokenIssuanceID]).getIssuer() == ctx.tx[sfAccount])
        return temMALFORMED;

    if (ctx.tx[sfMPTAmount] > maxMPTokenAmount)
        return temBAD_AMOUNT;

    if (ctx.tx[sfBlindingFactor].size() != ecBlindingFactorLength)
        return temMALFORMED;

    if (ctx.tx.isFieldPresent(sfHolderElGamalPublicKey))
    {
        if (ctx.tx[sfHolderElGamalPublicKey].length() != ecPubKeyLength)
            return temMALFORMED;

        // proof of knowledge of the secret key corresponding to the provided
        // public key is needed when holder ec public key is being set.
        if (!ctx.tx.isFieldPresent(sfZKProof))
            return temMALFORMED;

        // verify schnorr proof length when registerring holder ec public key
        if (ctx.tx[sfZKProof].size() != ecSchnorrProofLength)
            return temMALFORMED;
    }
    else
    {
        // zkp should not be present if public key was already set
        if (ctx.tx.isFieldPresent(sfZKProof))
            return temMALFORMED;
    }

    // check encrypted amount format after the above basic checks
    // this check is more expensive so put it at the end
    if (auto const res = checkEncryptedAmountFormat(ctx.tx); !isTesSuccess(res))
        return res;

    return tesSUCCESS;
}

TER
ConfidentialConvert::preclaim(PreclaimContext const& ctx)
{
    auto const account = ctx.tx[sfAccount];
    auto const issuanceID = ctx.tx[sfMPTokenIssuanceID];
    auto const amount = ctx.tx[sfMPTAmount];

    // ensure that issuance exists
    auto const sleIssuance = ctx.view.read(keylet::mptIssuance(issuanceID));
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    if (!sleIssuance->isFlag(lsfMPTCanPrivacy))
        return tecNO_PERMISSION;

    // already checked in preflight, but should also check that issuer on the
    // issuance isn't the account either
    if (sleIssuance->getAccountID(sfIssuer) == account)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // issuer has not uploaded their pub key yet
    if (!sleIssuance->isFieldPresent(sfIssuerElGamalPublicKey))
        return tecNO_PERMISSION;

    bool const hasAuditor = ctx.tx.isFieldPresent(sfAuditorEncryptedAmount);
    bool const requiresAuditor = sleIssuance->isFieldPresent(sfAuditorElGamalPublicKey);

    // tx must include auditor ciphertext if the issuance has enabled
    // auditing, and must not include it if auditing is not enabled
    if (requiresAuditor != hasAuditor)
        return tecNO_PERMISSION;

    auto const sleMptoken = ctx.view.read(keylet::mptoken(issuanceID, account));
    if (!sleMptoken)
        return tecOBJECT_NOT_FOUND;

    auto const mptIssue = MPTIssue{issuanceID};
    STAmount const mptAmount = STAmount(MPTAmount{static_cast<MPTAmount::value_type>(amount)}, mptIssue);
    if (accountHolds(
            ctx.view,
            account,
            mptIssue,
            FreezeHandling::fhZERO_IF_FROZEN,
            AuthHandling::ahZERO_IF_UNAUTHORIZED,
            ctx.j) < mptAmount)
    {
        return tecINSUFFICIENT_FUNDS;
    }

    auto const hasHolderKeyOnLedger = sleMptoken->isFieldPresent(sfHolderElGamalPublicKey);
    auto const hasHolderKeyInTx = ctx.tx.isFieldPresent(sfHolderElGamalPublicKey);

    // must have pk to convert
    if (!hasHolderKeyOnLedger && !hasHolderKeyInTx)
        return tecNO_PERMISSION;

    // can't update if there's already a pk
    if (hasHolderKeyOnLedger && hasHolderKeyInTx)
        return tecDUPLICATE;

    Slice holderPubKey;
    if (hasHolderKeyInTx)
    {
        holderPubKey = ctx.tx[sfHolderElGamalPublicKey];

        auto const contextHash = getConvertContextHash(account, ctx.tx[sfSequence], issuanceID, amount);

        // when register new pk, verify through schnorr proof
        if (!isTesSuccess(verifySchnorrProof(holderPubKey, ctx.tx[sfZKProof], contextHash)))
        {
            return tecBAD_PROOF;
        }
    }
    else
    {
        holderPubKey = (*sleMptoken)[sfHolderElGamalPublicKey];
    }

    std::optional<ConfidentialRecipient> auditor;
    if (hasAuditor)
    {
        auditor.emplace(
            ConfidentialRecipient{(*sleIssuance)[sfAuditorElGamalPublicKey], ctx.tx[sfAuditorEncryptedAmount]});
    }

    return verifyRevealedAmount(
        amount,
        ctx.tx[sfBlindingFactor],
        {holderPubKey, ctx.tx[sfHolderEncryptedAmount]},
        {(*sleIssuance)[sfIssuerElGamalPublicKey], ctx.tx[sfIssuerEncryptedAmount]},
        auditor);
}

TER
ConfidentialConvert::doApply()
{
    auto const mptIssuanceID = ctx_.tx[sfMPTokenIssuanceID];

    auto sleMptoken = view().peek(keylet::mptoken(mptIssuanceID, account_));
    if (!sleMptoken)
        return tecINTERNAL;

    auto sleIssuance = view().peek(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
        return tecINTERNAL;

    auto const amtToConvert = ctx_.tx[sfMPTAmount];
    auto const amt = (*sleMptoken)[~sfMPTAmount].value_or(0);

    if (ctx_.tx.isFieldPresent(sfHolderElGamalPublicKey))
        (*sleMptoken)[sfHolderElGamalPublicKey] = ctx_.tx[sfHolderElGamalPublicKey];

    (*sleMptoken)[sfMPTAmount] = amt - amtToConvert;
    (*sleIssuance)[sfConfidentialOutstandingAmount] =
        (*sleIssuance)[~sfConfidentialOutstandingAmount].value_or(0) + amtToConvert;

    Slice const holderEc = ctx_.tx[sfHolderEncryptedAmount];
    Slice const issuerEc = ctx_.tx[sfIssuerEncryptedAmount];

    auto const auditorEc = ctx_.tx[~sfAuditorEncryptedAmount];

    // todo: we should check sfConfidentialBalanceSpending depending on
    // if we encrypt zero amount
    if (sleMptoken->isFieldPresent(sfIssuerEncryptedBalance) &&
        sleMptoken->isFieldPresent(sfConfidentialBalanceInbox) &&
        sleMptoken->isFieldPresent(sfConfidentialBalanceSpending))
    {
        // homomorphically add holder's encrypted balance
        {
            Buffer sum(ecGamalEncryptedTotalLength);
            if (TER const ter = homomorphicAdd(holderEc, (*sleMptoken)[sfConfidentialBalanceInbox], sum);
                !isTesSuccess(ter))
                return tecINTERNAL;

            (*sleMptoken)[sfConfidentialBalanceInbox] = sum;
        }

        // homomorphically add issuer's encrypted balance
        {
            Buffer sum(ecGamalEncryptedTotalLength);
            if (TER const ter = homomorphicAdd(issuerEc, (*sleMptoken)[sfIssuerEncryptedBalance], sum);
                !isTesSuccess(ter))
                return tecINTERNAL;

            (*sleMptoken)[sfIssuerEncryptedBalance] = sum;
        }

        // homomorphically add auditor's encrypted balance
        if (auditorEc)
        {
            Buffer sum(ecGamalEncryptedTotalLength);
            if (TER const ter = homomorphicAdd(*auditorEc, (*sleMptoken)[sfAuditorEncryptedBalance], sum);
                !isTesSuccess(ter))
                return tecINTERNAL;

            (*sleMptoken)[sfAuditorEncryptedBalance] = sum;
        }
    }
    else if (
        !sleMptoken->isFieldPresent(sfIssuerEncryptedBalance) &&
        !sleMptoken->isFieldPresent(sfConfidentialBalanceInbox) &&
        !sleMptoken->isFieldPresent(sfConfidentialBalanceSpending))
    {
        (*sleMptoken)[sfConfidentialBalanceInbox] = holderEc;
        (*sleMptoken)[sfIssuerEncryptedBalance] = issuerEc;
        (*sleMptoken)[sfConfidentialBalanceVersion] = 0;

        if (auditorEc)
            (*sleMptoken)[sfAuditorEncryptedBalance] = *auditorEc;

        // encrypt sfConfidentialBalanceSpending with zero balance
        auto const zeroBalance =
            encryptCanonicalZeroAmount((*sleMptoken)[sfHolderElGamalPublicKey], account_, mptIssuanceID);

        if (!zeroBalance)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        (*sleMptoken)[sfConfidentialBalanceSpending] = *zeroBalance;
    }
    else
    {
        // both sfIssuerEncryptedBalance and sfConfidentialBalanceInbox should
        // exist together
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }

    view().update(sleIssuance);
    view().update(sleMptoken);
    return tesSUCCESS;
}

}  // namespace xrpl
