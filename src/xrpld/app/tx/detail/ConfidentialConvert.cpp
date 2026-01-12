#include <xrpld/app/misc/DelegateUtils.h>
#include <xrpld/app/tx/detail/ConfidentialConvert.h>

#include <xrpl/ledger/View.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
ConfidentialConvert::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureConfidentialTransfer))
        return temDISABLED;

    // issuer cannot convert
    if (MPTIssue(ctx.tx[sfMPTokenIssuanceID]).getIssuer() == ctx.tx[sfAccount])
        return temMALFORMED;

    if (auto const res = checkEncryptedAmountFormat(ctx.tx); !isTesSuccess(res))
        return res;

    if (ctx.tx[sfMPTAmount] > maxMPTokenAmount)
        return temBAD_AMOUNT;

    if (ctx.tx.isFieldPresent(sfHolderElGamalPublicKey) &&
        ctx.tx[sfHolderElGamalPublicKey].length() != ecPubKeyLength)
        return temMALFORMED;

    auto const expectedCount =
        ctx.tx.isFieldPresent(sfAuditorEncryptedAmount) ? 3 : 2;
    if (ctx.tx[sfZKProof].size() != expectedCount * ecEqualityProofLength)
        return temMALFORMED;

    return tesSUCCESS;
}

TER
ConfidentialConvert::preclaim(PreclaimContext const& ctx)
{
    // ensure that issuance exists
    auto const sleIssuance =
        ctx.view.read(keylet::mptIssuance(ctx.tx[sfMPTokenIssuanceID]));
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    if (!sleIssuance->isFlag(lsfMPTCanPrivacy))
        return tecNO_PERMISSION;

    // already checked in preflight, but should also check that issuer on the
    // issuance isn't the account either
    if (sleIssuance->getAccountID(sfIssuer) == ctx.tx[sfAccount])
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // issuer has not uploaded their pub key yet
    if (!sleIssuance->isFieldPresent(sfIssuerElGamalPublicKey))
        return tecNO_PERMISSION;

    bool const hasAuditor = ctx.tx.isFieldPresent(sfAuditorEncryptedAmount);

    // tx must include auditor ciphertext if the issuance has enabled auditing
    if (sleIssuance->isFieldPresent(sfAuditorElGamalPublicKey) && !hasAuditor)
        return tecNO_PERMISSION;

    auto const sleMptoken = ctx.view.read(
        keylet::mptoken(ctx.tx[sfMPTokenIssuanceID], ctx.tx[sfAccount]));
    if (!sleMptoken)
        return tecOBJECT_NOT_FOUND;

    auto const mptIssue = MPTIssue{ctx.tx[sfMPTokenIssuanceID]};
    STAmount const mptAmount = STAmount(
        MPTAmount{static_cast<MPTAmount::value_type>(ctx.tx[sfMPTAmount])},
        mptIssue);
    if (accountHolds(
            ctx.view,
            ctx.tx[sfAccount],
            mptIssue,
            FreezeHandling::fhZERO_IF_FROZEN,
            AuthHandling::ahZERO_IF_UNAUTHORIZED,
            ctx.j) < mptAmount)
    {
        return tecINSUFFICIENT_FUNDS;
    }

    // must have pk to convert
    if (!sleMptoken->isFieldPresent(sfHolderElGamalPublicKey) &&
        !ctx.tx.isFieldPresent(sfHolderElGamalPublicKey))
        return tecNO_PERMISSION;

    // can't update if there's already a pk
    if (sleMptoken->isFieldPresent(sfHolderElGamalPublicKey) &&
        ctx.tx.isFieldPresent(sfHolderElGamalPublicKey))
        return tecDUPLICATE;

    auto const holderPubKey = ctx.tx.isFieldPresent(sfHolderElGamalPublicKey)
        ? ctx.tx[sfHolderElGamalPublicKey]
        : (*sleMptoken)[sfHolderElGamalPublicKey];

    auto const contextHash = getConvertContextHash(
        ctx.tx[sfAccount],
        ctx.tx[sfSequence],
        ctx.tx[sfMPTokenIssuanceID],
        ctx.tx[sfMPTAmount]);

    std::vector<Buffer> const zkps = getEqualityProofs(ctx.tx[sfZKProof]);

    auto const& amount = ctx.tx[sfMPTAmount];

    // we already checked proof size in preflight, still do sanity check here
    // since we are going to access individual vector entries
    auto const expectedCount = ctx.tx[sfZKProof].size() / ecEqualityProofLength;
    if (zkps.size() != expectedCount)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // check equality proof
    if (!isTesSuccess(verifyEqualityProof(
            amount,
            zkps[0],
            holderPubKey,
            ctx.tx[sfHolderEncryptedAmount],
            contextHash)) ||
        !isTesSuccess(verifyEqualityProof(
            amount,
            zkps[1],
            (*sleIssuance)[sfIssuerElGamalPublicKey],
            ctx.tx[sfIssuerEncryptedAmount],
            contextHash)))
    {
        return tecBAD_PROOF;
    }

    // Verify Auditor proof if present
    if (hasAuditor &&
        !isTesSuccess(verifyEqualityProof(
            amount,
            zkps[2],
            (*sleIssuance)[sfAuditorElGamalPublicKey],
            ctx.tx[sfAuditorEncryptedAmount],
            contextHash)))
    {
        return tecBAD_PROOF;
    }

    return tesSUCCESS;
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
        (*sleMptoken)[sfHolderElGamalPublicKey] =
            ctx_.tx[sfHolderElGamalPublicKey];

    (*sleMptoken)[sfMPTAmount] = amt - amtToConvert;
    (*sleIssuance)[sfConfidentialOutstandingAmount] =
        (*sleIssuance)[~sfConfidentialOutstandingAmount].value_or(0) +
        amtToConvert;

    Slice const holderEc = ctx_.tx[sfHolderEncryptedAmount];
    Slice const issuerEc = ctx_.tx[sfIssuerEncryptedAmount];

    std::optional<Slice> const auditorEc = ctx_.tx[~sfAuditorEncryptedAmount];

    // todo: we should check sfConfidentialBalanceSpending depending on
    // if we encrypt zero amount
    if (sleMptoken->isFieldPresent(sfIssuerEncryptedBalance) &&
        sleMptoken->isFieldPresent(sfConfidentialBalanceInbox) &&
        sleMptoken->isFieldPresent(sfConfidentialBalanceSpending))
    {
        // homomorphically add holder's encrypted balance
        {
            Buffer sum(ecGamalEncryptedTotalLength);
            if (TER const ter = homomorphicAdd(
                    holderEc, (*sleMptoken)[sfConfidentialBalanceInbox], sum);
                !isTesSuccess(ter))
                return tecINTERNAL;

            (*sleMptoken)[sfConfidentialBalanceInbox] = sum;
        }

        // homomorphically add issuer's encrypted balance
        {
            Buffer sum(ecGamalEncryptedTotalLength);
            if (TER const ter = homomorphicAdd(
                    issuerEc, (*sleMptoken)[sfIssuerEncryptedBalance], sum);
                !isTesSuccess(ter))
                return tecINTERNAL;

            (*sleMptoken)[sfIssuerEncryptedBalance] = sum;
        }

        // homomorphically add auditor's encrypted balance
        if (auditorEc)
        {
            Buffer sum(ecGamalEncryptedTotalLength);
            if (TER const ter = homomorphicAdd(
                    *auditorEc, (*sleMptoken)[sfAuditorEncryptedBalance], sum);
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

        try
        {
            // encrypt sfConfidentialBalanceSpending with zero balance
            Buffer out;
            out = encryptAmount(0, (*sleMptoken)[sfHolderElGamalPublicKey])
                      .ciphertext;
            (*sleMptoken)[sfConfidentialBalanceSpending] = out;
        }
        catch (std::exception const& e)
        {
            return tecINTERNAL;  // LCOV_EXCL_LINE
        }
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

}  // namespace ripple
