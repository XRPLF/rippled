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
#include <xrpld/app/tx/detail/ConfidentialSend.h>

#include <xrpl/ledger/View.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
ConfidentialSend::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureConfidentialTransfer))
        return temDISABLED;

    auto const account = ctx.tx[sfAccount];
    auto const issuer = MPTIssue(ctx.tx[sfMPTokenIssuanceID]).getIssuer();

    // ConfidentialSend only allows holder to holder, holder to second account,
    // and second account to holder transfers. So issuer cannot be the sender.
    if (account == issuer)
        return temMALFORMED;

    // Can not send to self
    if (account == ctx.tx[sfDestination])
        return temMALFORMED;

    if (ctx.tx[sfSenderEncryptedAmount].length() !=
            ecGamalEncryptedTotalLength ||
        ctx.tx[sfDestinationEncryptedAmount].length() !=
            ecGamalEncryptedTotalLength ||
        ctx.tx[sfIssuerEncryptedAmount].length() != ecGamalEncryptedTotalLength)
        return temBAD_CIPHERTEXT;

    if (!isValidCiphertext(ctx.tx[sfSenderEncryptedAmount]) ||
        !isValidCiphertext(ctx.tx[sfDestinationEncryptedAmount]) ||
        !isValidCiphertext(ctx.tx[sfIssuerEncryptedAmount]))
        return temBAD_CIPHERTEXT;

    // if (ctx.tx[sfZKProof].length() != ecEqualityProofLength)
    //     return temMALFORMED;

    return tesSUCCESS;
}

TER
ConfidentialSend::preclaim(PreclaimContext const& ctx)
{
    // Check if sender account exists
    auto const account = ctx.tx[sfAccount];
    if (!ctx.view.exists(keylet::account(account)))
        return terNO_ACCOUNT;

    // Check if destination account exists
    auto const destination = ctx.tx[sfDestination];
    if (!ctx.view.exists(keylet::account(destination)))
        return tecNO_TARGET;

    // Check if MPT issuance exists
    auto const mptIssuanceID = ctx.tx[sfMPTokenIssuanceID];
    auto const sleIssuance = ctx.view.read(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    // Check if the issuance allows transfer
    if (!sleIssuance->isFlag(lsfMPTCanTransfer))
        return tecLOCKED;

    // Check if issuance allows confidential transfer
    if (sleIssuance->isFlag(lsfMPTNoConfidentialTransfer))
        return tecNO_PERMISSION;

    // Check if issuance has issuer ElGamal public key
    if (!sleIssuance->isFieldPresent(sfIssuerElGamalPublicKey))
        return tecNO_PERMISSION;

    // already checked in preflight, but should also check that issuer on the
    // issuance isn't the account either
    if (sleIssuance->getAccountID(sfIssuer) == ctx.tx[sfAccount])
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // Check sender's MPToken
    auto const sleSenderMPToken =
        ctx.view.read(keylet::mptoken(mptIssuanceID, account));
    if (!sleSenderMPToken)
        return tecOBJECT_NOT_FOUND;

    // Check sender's MPToken has necessary fields for confidential send
    if (!sleSenderMPToken->isFieldPresent(sfHolderElGamalPublicKey) ||
        !sleSenderMPToken->isFieldPresent(sfConfidentialBalanceSpending) ||
        !sleSenderMPToken->isFieldPresent(sfIssuerEncryptedBalance))
        return tecNO_PERMISSION;

    // Check destination's MPToken
    auto const sleDestinationMPToken =
        ctx.view.read(keylet::mptoken(mptIssuanceID, destination));
    if (!sleDestinationMPToken)
        return tecOBJECT_NOT_FOUND;

    // Check destination's MPToken has necessary fields for confidential send
    if (!sleDestinationMPToken->isFieldPresent(sfHolderElGamalPublicKey) ||
        !sleDestinationMPToken->isFieldPresent(sfConfidentialBalanceInbox) ||
        !sleDestinationMPToken->isFieldPresent(sfIssuerEncryptedBalance))
        return tecNO_PERMISSION;

    // Check lock
    MPTIssue const mptIssue(mptIssuanceID);
    if (auto const ter = checkFrozen(ctx.view, account, mptIssue);
        ter != tesSUCCESS)
        return ter;

    if (auto const ter = checkFrozen(ctx.view, destination, mptIssue);
        ter != tesSUCCESS)
        return ter;

    // Check auth
    if (auto const ter = requireAuth(ctx.view, mptIssue, account);
        !isTesSuccess(ter))
        return ter;

    if (auto const ter = requireAuth(ctx.view, mptIssue, destination);
        !isTesSuccess(ter))
        return ter;

    // todo: check zkproof. equality proof and range proof, combined or separate
    // TBD. TER const terProof = verifyConfidentialSendProof(
    //     ctx.tx[sfZKProof],
    //     (*sleSender)[sfConfidentialBalanceSpending],
    //     ctx.tx[sfSenderEncryptedAmount],
    //     ctx.tx[sfDestinationEncryptedAmount],
    //     ctx.tx[sfIssuerEncryptedAmount],
    //     (*sleSender)[sfHolderElGamalPublicKey],
    //     (*sleDestination)[sfHolderElGamalPublicKey],
    //     (*sleIssuance)[sfIssuerElGamalPublicKey],
    //     (*sleSender)[~sfConfidentialBalanceVersion].value_or(0),
    //     ctx.tx.getTransactionID()
    // );

    // if (!isTesSuccess(terProof))
    //     return tecBAD_PROOF;

    return tesSUCCESS;
}

TER
ConfidentialSend::doApply()
{
    auto const mptIssuanceID = ctx_.tx[sfMPTokenIssuanceID];
    auto const account = ctx_.tx[sfAccount];
    auto const destination = ctx_.tx[sfDestination];

    auto sleSender = view().peek(keylet::mptoken(mptIssuanceID, account));
    auto sleDestination =
        view().peek(keylet::mptoken(mptIssuanceID, destination));

    if (!sleSender || !sleDestination)
        return tecINTERNAL;

    Slice const senderEc = ctx_.tx[sfSenderEncryptedAmount];
    Slice const destEc = ctx_.tx[sfDestinationEncryptedAmount];
    Slice const issuerEc = ctx_.tx[sfIssuerEncryptedAmount];

    // Subtract from sender's spending balance
    {
        Slice const curSpending = (*sleSender)[sfConfidentialBalanceSpending];
        Buffer newSpending(ecGamalEncryptedTotalLength);

        if (TER const ter =
                homomorphicSubtract(curSpending, senderEc, newSpending);
            !isTesSuccess(ter))
            return tecINTERNAL;

        (*sleSender)[sfConfidentialBalanceSpending] = newSpending;
    }

    // Subtract from issuer's balance
    {
        Slice const curIssuerEnc = (*sleSender)[sfIssuerEncryptedBalance];
        Buffer newIssuerEnc(ecGamalEncryptedTotalLength);

        if (TER const ter =
                homomorphicSubtract(curIssuerEnc, issuerEc, newIssuerEnc);
            !isTesSuccess(ter))
            return tecINTERNAL;

        (*sleSender)[sfIssuerEncryptedBalance] = newIssuerEnc;
    }

    // Increment version
    (*sleSender)[sfConfidentialBalanceVersion] =
        (*sleSender)[~sfConfidentialBalanceVersion].value_or(0u) + 1u;

    // Add to destination's inbox balance
    {
        Slice const curInbox = (*sleDestination)[sfConfidentialBalanceInbox];
        Buffer newInbox(ecGamalEncryptedTotalLength);

        if (TER const ter = homomorphicAdd(curInbox, destEc, newInbox);
            !isTesSuccess(ter))
            return tecINTERNAL;

        (*sleDestination)[sfConfidentialBalanceInbox] = newInbox;
    }

    // Add to issuer's balance
    {
        Slice const curIssuerEnc = (*sleDestination)[sfIssuerEncryptedBalance];
        Buffer newIssuerEnc(ecGamalEncryptedTotalLength);

        if (TER const ter =
                homomorphicAdd(curIssuerEnc, issuerEc, newIssuerEnc);
            !isTesSuccess(ter))
            return tecINTERNAL;

        (*sleDestination)[sfIssuerEncryptedBalance] = newIssuerEnc;
    }

    view().update(sleSender);
    view().update(sleDestination);
    return tesSUCCESS;
}
}  // namespace ripple
