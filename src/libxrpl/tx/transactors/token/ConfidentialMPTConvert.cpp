#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/transactors/token/ConfidentialMPTConvert.h>

namespace xrpl {

NotTEC
ConfidentialMPTConvert::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureConfidentialTransfer))
        return temDISABLED;

    // issuer cannot convert
    if (MPTIssue(ctx.tx[sfMPTokenIssuanceID]).getIssuer() == ctx.tx[sfAccount])
        return temMALFORMED;

    if (ctx.tx[sfMPTAmount] > maxMPTokenAmount)
        return temBAD_AMOUNT;

    if (ctx.tx.isFieldPresent(sfHolderEncryptionKey))
    {
        if (!isValidCompressedECPoint(ctx.tx[sfHolderEncryptionKey]))
            return temMALFORMED;

        // proof of knowledge of the secret key corresponding to the provided
        // public key is needed when holder ec public key is being set.
        if (!ctx.tx.isFieldPresent(sfZKProof))
            return temMALFORMED;

        // verify schnorr proof length when registering holder ec public key
        if (ctx.tx[sfZKProof].size() != ecSchnorrProofLength)
            return temMALFORMED;
    }
    else
    {
        // Either both sfHolderEncryptionKey and sfZKProof should be present, or both should be
        // absent.
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
ConfidentialMPTConvert::preclaim(PreclaimContext const& ctx)
{
    auto const account = ctx.tx[sfAccount];
    auto const issuanceID = ctx.tx[sfMPTokenIssuanceID];
    auto const amount = ctx.tx[sfMPTAmount];

    // ensure that issuance exists
    auto const sleIssuance = ctx.view.read(keylet::mptIssuance(issuanceID));
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    if (!sleIssuance->isFlag(lsfMPTCanConfidentialAmount) ||
        !sleIssuance->isFieldPresent(sfIssuerEncryptionKey))
        return tecNO_PERMISSION;

    // already checked in preflight, but should also check that issuer on the
    // issuance isn't the account either
    if (sleIssuance->getAccountID(sfIssuer) == account)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    bool const hasAuditor = ctx.tx.isFieldPresent(sfAuditorEncryptedAmount);
    bool const requiresAuditor = sleIssuance->isFieldPresent(sfAuditorEncryptionKey);

    // tx must include auditor ciphertext if the issuance has enabled
    // auditing, and must not include it if auditing is not enabled
    if (requiresAuditor != hasAuditor)
        return tecNO_PERMISSION;

    auto const sleMptoken = ctx.view.read(keylet::mptoken(issuanceID, account));
    if (!sleMptoken)
        return tecOBJECT_NOT_FOUND;

    auto const mptIssue = MPTIssue{issuanceID};

    // Explicit freeze and auth checks are required because accountHolds
    // with fhZERO_IF_FROZEN/ahZERO_IF_UNAUTHORIZED only implicitly rejects
    // non-zero amounts. A zero-amount convert would bypass those implicit
    // checks, allowing frozen or unauthorized accounts to register ElGamal
    // keys and initialize confidential balance fields.

    // Check lock
    if (auto const ter = checkFrozen(ctx.view, account, mptIssue); !isTesSuccess(ter))
        return ter;

    // Check auth
    if (auto const ter = requireAuth(ctx.view, mptIssue, account); !isTesSuccess(ter))
        return ter;

    STAmount const mptAmount =
        STAmount(MPTAmount{static_cast<MPTAmount::value_type>(amount)}, mptIssue);
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

    auto const hasHolderKeyOnLedger = sleMptoken->isFieldPresent(sfHolderEncryptionKey);
    auto const hasHolderKeyInTx = ctx.tx.isFieldPresent(sfHolderEncryptionKey);

    // must have pk to convert
    if (!hasHolderKeyOnLedger && !hasHolderKeyInTx)
        return tecNO_PERMISSION;

    // can't update if there's already a pk
    if (hasHolderKeyOnLedger && hasHolderKeyInTx)
        return tecDUPLICATE;

    // Run all verifications before returning any error to prevent timing attacks
    // that could reveal which proof failed.
    bool valid = true;

    Slice holderPubKey;
    if (hasHolderKeyInTx)
    {
        holderPubKey = ctx.tx[sfHolderEncryptionKey];

        auto const contextHash =
            getConvertContextHash(account, issuanceID, ctx.tx.getSeqProxy().value());

        if (auto const ter = verifySchnorrProof(holderPubKey, ctx.tx[sfZKProof], contextHash);
            !isTesSuccess(ter))
        {
            valid = false;
        }
    }
    else
    {
        holderPubKey = (*sleMptoken)[sfHolderEncryptionKey];
    }

    std::optional<ConfidentialRecipient> auditor;
    if (hasAuditor)
    {
        auditor.emplace(
            ConfidentialRecipient{
                (*sleIssuance)[sfAuditorEncryptionKey], ctx.tx[sfAuditorEncryptedAmount]});
    }

    auto const blindingFactor = ctx.tx[sfBlindingFactor];
    if (auto const ter = verifyRevealedAmount(
            amount,
            Slice(blindingFactor.data(), blindingFactor.size()),
            {holderPubKey, ctx.tx[sfHolderEncryptedAmount]},
            {(*sleIssuance)[sfIssuerEncryptionKey], ctx.tx[sfIssuerEncryptedAmount]},
            auditor);
        !isTesSuccess(ter))
    {
        valid = false;
    }

    if (!valid)
        return tecBAD_PROOF;

    return tesSUCCESS;
}

TER
ConfidentialMPTConvert::doApply()
{
    auto const mptIssuanceID = ctx_.tx[sfMPTokenIssuanceID];

    auto sleMptoken = view().peek(keylet::mptoken(mptIssuanceID, account_));
    if (!sleMptoken)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto sleIssuance = view().peek(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const amtToConvert = ctx_.tx[sfMPTAmount];
    auto const amt = (*sleMptoken)[~sfMPTAmount].value_or(0);

    if (ctx_.tx.isFieldPresent(sfHolderEncryptionKey))
        (*sleMptoken)[sfHolderEncryptionKey] = ctx_.tx[sfHolderEncryptionKey];

    // Converting decreases regular balance and increases confidential outstanding.
    // The confidential outstanding tracks total tokens in confidential form globally.
    auto const currentCOA = (*sleIssuance)[~sfConfidentialOutstandingAmount].value_or(0);
    if (amtToConvert > maxMPTokenAmount - currentCOA)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    (*sleMptoken)[sfMPTAmount] = amt - amtToConvert;
    (*sleIssuance)[sfConfidentialOutstandingAmount] = currentCOA + amtToConvert;

    Slice const holderEc = ctx_.tx[sfHolderEncryptedAmount];
    Slice const issuerEc = ctx_.tx[sfIssuerEncryptedAmount];

    auto const auditorEc = ctx_.tx[~sfAuditorEncryptedAmount];

    // Two cases for Convert:
    // 1. Holder already has confidential balances -> homomorphically add to inbox
    // 2. First-time convert -> initialize all confidential balance fields
    if (sleMptoken->isFieldPresent(sfIssuerEncryptedBalance) &&
        sleMptoken->isFieldPresent(sfConfidentialBalanceInbox) &&
        sleMptoken->isFieldPresent(sfConfidentialBalanceSpending))
    {
        // Case 1: Add to existing inbox balance (holder will merge later)
        {
            auto sum = homomorphicAdd(holderEc, (*sleMptoken)[sfConfidentialBalanceInbox]);
            if (!sum)
                return tecINTERNAL;  // LCOV_EXCL_LINE

            (*sleMptoken)[sfConfidentialBalanceInbox] = std::move(*sum);
        }

        // homomorphically add issuer's encrypted balance
        {
            auto sum = homomorphicAdd(issuerEc, (*sleMptoken)[sfIssuerEncryptedBalance]);
            if (!sum)
                return tecINTERNAL;  // LCOV_EXCL_LINE

            (*sleMptoken)[sfIssuerEncryptedBalance] = std::move(*sum);
        }

        // homomorphically add auditor's encrypted balance
        if (auditorEc)
        {
            auto sum = homomorphicAdd(*auditorEc, (*sleMptoken)[sfAuditorEncryptedBalance]);
            if (!sum)
                return tecINTERNAL;  // LCOV_EXCL_LINE

            (*sleMptoken)[sfAuditorEncryptedBalance] = std::move(*sum);
        }
    }
    else if (
        !sleMptoken->isFieldPresent(sfIssuerEncryptedBalance) &&
        !sleMptoken->isFieldPresent(sfConfidentialBalanceInbox) &&
        !sleMptoken->isFieldPresent(sfConfidentialBalanceSpending))
    {
        // Case 2: First-time convert - initialize all confidential fields
        (*sleMptoken)[sfConfidentialBalanceInbox] = holderEc;
        (*sleMptoken)[sfIssuerEncryptedBalance] = issuerEc;
        (*sleMptoken)[sfConfidentialBalanceVersion] = 0;

        if (auditorEc)
            (*sleMptoken)[sfAuditorEncryptedBalance] = *auditorEc;

        // Spending balance starts at zero. Must use canonical zero encryption
        // (deterministic ciphertext) so the ledger state is reproducible.
        auto zeroBalance = encryptCanonicalZeroAmount(
            (*sleMptoken)[sfHolderEncryptionKey], account_, mptIssuanceID);

        if (!zeroBalance)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        (*sleMptoken)[sfConfidentialBalanceSpending] = std::move(*zeroBalance);
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
