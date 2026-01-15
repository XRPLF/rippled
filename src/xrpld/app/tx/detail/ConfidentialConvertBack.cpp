#include <xrpld/app/tx/detail/ConfidentialConvertBack.h>

#include <xrpl/ledger/View.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <cstddef>

namespace ripple {

size_t
expectedProofLength(std::shared_ptr<SLE const> const& issuance)
{
    auto const equalityProofLength = getEqualityProofLength(
        issuance->isFieldPresent(sfAuditorElGamalPublicKey));

    // todo: add pederson and range proof length
    return equalityProofLength;
}

NotTEC
ConfidentialConvertBack::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureConfidentialTransfer))
        return temDISABLED;

    // issuer cannot convert
    if (MPTIssue(ctx.tx[sfMPTokenIssuanceID]).getIssuer() == ctx.tx[sfAccount])
        return temMALFORMED;

    if (auto const res = checkEncryptedAmountFormat(ctx.tx); !isTesSuccess(res))
        return res;

    if (ctx.tx[sfMPTAmount] == 0 || ctx.tx[sfMPTAmount] > maxMPTokenAmount)
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

TER
verifyProofs(
    STTx const& tx,
    std::shared_ptr<SLE const> const& issuance,
    std::shared_ptr<SLE const> const& mptoken)
{
    if (expectedProofLength(issuance) != tx[sfZKProof].size())
        return tecBAD_PROOF;

    auto const mptIssuanceID = tx[sfMPTokenIssuanceID];
    auto const account = tx[sfAccount];
    auto const amount = tx[sfMPTAmount];

    auto const contextHash = getConvertBackContextHash(
        account,
        tx[sfSequence],
        mptIssuanceID,
        amount,
        (*mptoken)[~sfConfidentialBalanceVersion].value_or(0));

    // Prepare Auditor Info
    std::optional<EncryptedAmountInfo> auditor;
    bool const hasAuditor = issuance->isFieldPresent(sfAuditorElGamalPublicKey);
    if (hasAuditor)
    {
        auditor.emplace(
            EncryptedAmountInfo{
                (*issuance)[sfAuditorElGamalPublicKey],
                tx[sfAuditorEncryptedAmount]});
    }

    // verify equality proofs
    {
        auto const equalityZkps = getEqualityProofs(
            Slice{tx[sfZKProof].data(), getEqualityProofLength(hasAuditor)});

        return verifyEqualityProofs(
            amount,
            equalityZkps,
            EncryptedAmountInfo{
                (*mptoken)[sfHolderElGamalPublicKey],
                tx[sfHolderEncryptedAmount]},  // Holder
            EncryptedAmountInfo{
                (*issuance)[sfIssuerElGamalPublicKey],
                tx[sfIssuerEncryptedAmount]},  // Issuer
            auditor,                           // Optional auditor
            contextHash);
    }

    return tesSUCCESS;
}

TER
ConfidentialConvertBack::preclaim(PreclaimContext const& ctx)
{
    auto const mptIssuanceID = ctx.tx[sfMPTokenIssuanceID];
    auto const account = ctx.tx[sfAccount];
    auto const amount = ctx.tx[sfMPTAmount];

    // ensure that issuance exists
    auto const sleIssuance = ctx.view.read(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    if (!sleIssuance->isFlag(lsfMPTCanPrivacy))
        return tecNO_PERMISSION;

    bool const hasAuditor = ctx.tx.isFieldPresent(sfAuditorEncryptedAmount);
    bool const requiresAuditor =
        sleIssuance->isFieldPresent(sfAuditorElGamalPublicKey);

    // tx must include auditor ciphertext if the issuance has enabled
    // auditing
    if (requiresAuditor && !hasAuditor)
        return tecNO_PERMISSION;

    // if auditing is not supported then user should not upload auditor
    // ciphertext
    if (!requiresAuditor && hasAuditor)
        return tecNO_PERMISSION;

    // already checked in preflight, but should also check that issuer on
    // the issuance isn't the account either
    if (sleIssuance->getAccountID(sfIssuer) == account)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const sleMptoken =
        ctx.view.read(keylet::mptoken(mptIssuanceID, account));
    if (!sleMptoken)
        return tecOBJECT_NOT_FOUND;

    if (!sleMptoken->isFieldPresent(sfConfidentialBalanceSpending) ||
        !sleMptoken->isFieldPresent(sfHolderElGamalPublicKey))
    {
        return tecNO_PERMISSION;
    }

    // if the total circulating confidential balance is smaller than what the
    // holder is trying to convert back, we know for sure this txn should
    // fail
    if ((*sleIssuance)[~sfConfidentialOutstandingAmount].value_or(0) < amount)
    {
        return tecINSUFFICIENT_FUNDS;
    }

    // Check lock
    MPTIssue const mptIssue(mptIssuanceID);
    if (auto const ter = checkFrozen(ctx.view, account, mptIssue);
        !isTesSuccess(ter))
        return ter;

    // Check auth
    if (auto const ter = requireAuth(ctx.view, mptIssue, account);
        !isTesSuccess(ter))
        return ter;

    if (TER const res = verifyProofs(ctx.tx, sleIssuance, sleMptoken);
        !isTesSuccess(res))
        return res;

    return tesSUCCESS;
}

TER
ConfidentialConvertBack::doApply()
{
    auto const mptIssuanceID = ctx_.tx[sfMPTokenIssuanceID];

    auto sleMptoken = view().peek(keylet::mptoken(mptIssuanceID, account_));
    if (!sleMptoken)
        return tecINTERNAL;

    auto sleIssuance = view().peek(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
        return tecINTERNAL;

    auto const amtToConvertBack = ctx_.tx[sfMPTAmount];
    auto const amt = (*sleMptoken)[~sfMPTAmount].value_or(0);

    (*sleMptoken)[sfMPTAmount] = amt + amtToConvertBack;
    (*sleIssuance)[sfConfidentialOutstandingAmount] =
        (*sleIssuance)[sfConfidentialOutstandingAmount] - amtToConvertBack;

    // it's fine if it reaches max uint32, it just resets to 0
    (*sleMptoken)[sfConfidentialBalanceVersion] =
        (*sleMptoken)[~sfConfidentialBalanceVersion].value_or(0u) + 1u;

    std::optional<Slice> const auditorEc = ctx_.tx[~sfAuditorEncryptedAmount];

    // homomorphically subtract holder's encrypted balance
    {
        Buffer res(ecGamalEncryptedTotalLength);
        if (TER const ter = homomorphicSubtract(
                (*sleMptoken)[sfConfidentialBalanceSpending],
                ctx_.tx[sfHolderEncryptedAmount],
                res);
            !isTesSuccess(ter))
            return tecINTERNAL;

        (*sleMptoken)[sfConfidentialBalanceSpending] = res;
    }

    // homomorphically subtract issuer's encrypted balance
    {
        Buffer res(ecGamalEncryptedTotalLength);
        if (TER const ter = homomorphicSubtract(
                (*sleMptoken)[sfIssuerEncryptedBalance],
                ctx_.tx[sfIssuerEncryptedAmount],
                res);
            !isTesSuccess(ter))
            return tecINTERNAL;

        (*sleMptoken)[sfIssuerEncryptedBalance] = res;
    }

    if (auditorEc)
    {
        Buffer res(ecGamalEncryptedTotalLength);
        if (TER const ter = homomorphicSubtract(
                (*sleMptoken)[sfAuditorEncryptedBalance],
                ctx_.tx[sfAuditorEncryptedAmount],
                res);
            !isTesSuccess(ter))
            return tecINTERNAL;

        (*sleMptoken)[sfAuditorEncryptedBalance] = res;
    }

    view().update(sleIssuance);
    view().update(sleMptoken);
    return tesSUCCESS;
}

}  // namespace ripple
