//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2025 Ripple Labs Inc.

  Permission to use, copy, modify, and/or distribute this software for any
  purpose  with  or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
  MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <xrpld/app/misc/DelegateUtils.h>
#include <xrpld/app/tx/detail/ConfidentialConvert.h>

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

    if (ctx.tx[sfHolderEncryptedAmount].length() !=
            ecGamalEncryptedTotalLength ||
        ctx.tx[sfIssuerEncryptedAmount].length() != ecGamalEncryptedTotalLength)
        return temMALFORMED;

    if (ctx.tx[sfMPTAmount] > maxMPTokenAmount)
        return temMALFORMED;

    // if (ctx.tx[sfZKProof].length() != ecEqualityProofLength)
    //     return temMALFORMED;

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

    if (sleIssuance->isFlag(lsfMPTNoConfidentialTransfer))
        return tecNO_PERMISSION;

    // issuer has not uploaded their pub key yet
    if (!sleIssuance->isFieldPresent(sfIssuerElGamalPublicKey))
        return tecNO_PERMISSION;

    auto const sleMptoken = ctx.view.read(
        keylet::mptoken(ctx.tx[sfMPTokenIssuanceID], ctx.tx[sfAccount]));
    if (!sleMptoken)
        return tecOBJECT_NOT_FOUND;

    // we still allow conversion of zero amount
    if ((*sleMptoken)[~sfMPTAmount].value_or(0) < ctx.tx[sfMPTAmount])
        return tecINSUFFICIENT_FUNDS;

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

    // todo: check zkproof/well formed

    // check equality proof
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
    //         ctx.tx[sfHolderEncryptedAmount], holderPubKey)) ||
    //     !isTesSuccess(checkEqualityProof(
    //         ctx.tx[sfIssuerEncryptedAmount],
    //         (*sleIssuance)[sfIssuerElGamalPublicKey])))
    // {
    //     return tecBAD_PROOF;
    // }

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

    // todo: we should check sfConfidentialBalanceSpending depending on if we
    // encrypt zero amount
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
    }
    else if (
        !sleMptoken->isFieldPresent(sfIssuerEncryptedBalance) &&
        !sleMptoken->isFieldPresent(sfConfidentialBalanceInbox) &&
        !sleMptoken->isFieldPresent(sfConfidentialBalanceSpending))
    {
        (*sleMptoken)[sfConfidentialBalanceInbox] = holderEc;
        (*sleMptoken)[sfIssuerEncryptedBalance] = issuerEc;
        (*sleMptoken)[sfConfidentialBalanceVersion] = 0;

        try
        {
            // encrypt sfConfidentialBalanceSpending with zero balance
            Buffer out;
            out = encryptAmount(0, (*sleMptoken)[sfHolderElGamalPublicKey]);
            (*sleMptoken)[sfConfidentialBalanceSpending] = out;
        }
        catch (std::exception const& e)
        {
            return tecINTERNAL;
        }
    }
    else
    {
        // both sfIssuerEncryptedBalance and sfConfidentialBalanceInbox should
        // exist together
        return tecINTERNAL;
    }

    view().update(sleIssuance);
    view().update(sleMptoken);
    return tesSUCCESS;
}

}  // namespace ripple
