#include <xrpld/app/tx/detail/ConfidentialConvertBack.h>

#include <xrpl/ledger/View.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
ConfidentialConvertBack::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureConfidentialTransfer))
        return temDISABLED;

    // issuer cannot convert
    if (MPTIssue(ctx.tx[sfMPTokenIssuanceID]).getIssuer() == ctx.tx[sfAccount])
        return temMALFORMED;

    if (ctx.tx[sfHolderEncryptedAmount].length() !=
            ecGamalEncryptedTotalLength ||
        ctx.tx[sfIssuerEncryptedAmount].length() != ecGamalEncryptedTotalLength)
        return temBAD_CIPHERTEXT;

    if (ctx.tx[sfMPTAmount] == 0 || ctx.tx[sfMPTAmount] > maxMPTokenAmount)
        return temBAD_AMOUNT;

    if (!isValidCiphertext(ctx.tx[sfHolderEncryptedAmount]) ||
        !isValidCiphertext(ctx.tx[sfIssuerEncryptedAmount]))
        return temBAD_CIPHERTEXT;

    // todo: update with correct size of proof since it might also contain range
    // proof
    // if (ctx.tx[sfZKProof].length() != ecEqualityProofLength)
    //     return temMALFORMED;

    return tesSUCCESS;
}

TER
ConfidentialConvertBack::preclaim(PreclaimContext const& ctx)
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

    auto const sleMptoken = ctx.view.read(
        keylet::mptoken(ctx.tx[sfMPTokenIssuanceID], ctx.tx[sfAccount]));
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
    if ((*sleIssuance)[~sfConfidentialOutstandingAmount].value_or(0) <
        ctx.tx[sfMPTAmount])
    {
        return tecINSUFFICIENT_FUNDS;
    }

    auto const mptIssuanceID = ctx.tx[sfMPTokenIssuanceID];
    auto const account = ctx.tx[sfAccount];

    // Check lock
    MPTIssue const mptIssue(mptIssuanceID);
    if (auto const ter = checkFrozen(ctx.view, account, mptIssue);
        !isTesSuccess(ter))
        return ter;

    // Check auth
    if (auto const ter = requireAuth(ctx.view, mptIssue, account);
        !isTesSuccess(ter))
        return ter;

    // todo: need addtional parsing, the proof should contain multiple proofs
    // auto checkEqualityProof = [&](auto const& encryptedAmount,
    //                               auto const& pubKey) -> TER {
    //     return proveEquality(
    //         ctx.tx[sfZKProof],
    //         encryptedAmount,
    //         pubKey,
    //         ctx.tx[sfMPTAmount],
    //         ctx.tx.getTransactionID(),
    //         (*sleMptoken)[~sfConfidentialBalanceVersion].value_or(0));
    // };

    // if (!isTesSuccess(checkEqualityProof(
    //         ctx.tx[sfHolderEncryptedAmount],
    //         (*sleMptoken)[sfHolderElGamalPublicKey])) ||
    //     !isTesSuccess(checkEqualityProof(
    //         ctx.tx[sfIssuerEncryptedAmount],
    //         (*sleIssuance)[sfIssuerElGamalPublicKey])))
    // {
    //     return tecBAD_PROOF;
    // }

    // todo: also check range proof that
    // sfHolderEncryptedAmount <= sfConfidentialBalanceSpending AND
    // sfIssuerEncryptedAmount <= sfIssuerEncryptedBalance

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

    view().update(sleIssuance);
    view().update(sleMptoken);
    return tesSUCCESS;
}

}  // namespace ripple
