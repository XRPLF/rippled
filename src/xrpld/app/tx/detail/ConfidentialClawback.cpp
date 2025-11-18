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
#include <xrpld/app/tx/detail/ConfidentialClawback.h>

#include <xrpl/ledger/View.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
ConfidentialClawback::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureConfidentialTransfer))
        return temDISABLED;

    auto const account = ctx.tx[sfAccount];
    auto const issuer = MPTIssue(ctx.tx[sfMPTokenIssuanceID]).getIssuer();

    // Only issuer can clawback
    if (account != issuer)
        return temMALFORMED;

    // Cannot clawback from self
    if (account == ctx.tx[sfHolder])
        return temMALFORMED;

    auto const clawAmount = ctx.tx[sfMPTAmount];
    if (clawAmount == 0 || clawAmount > maxMPTokenAmount)
        return temBAD_AMOUNT;

    // if (ctx.tx[sfZKProof].length() != ecEqualityProofLength)
    //     return temMALFORMED;

    return tesSUCCESS;
}

TER
ConfidentialClawback::preclaim(PreclaimContext const& ctx)
{
    // Check if sender account exists
    auto const account = ctx.tx[sfAccount];
    if (!ctx.view.exists(keylet::account(account)))
        return terNO_ACCOUNT;

    // Check if holder account exists
    auto const holder = ctx.tx[sfHolder];
    if (!ctx.view.exists(keylet::account(holder)))
        return tecNO_TARGET;

    // Check if MPT issuance exists
    auto const mptIssuanceID = ctx.tx[sfMPTokenIssuanceID];
    auto const sleIssuance = ctx.view.read(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    // Sanity check: issuer must be the same as account
    if (sleIssuance->getAccountID(sfIssuer) != account)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // Check if issuance has issuer ElGamal public key
    if (!sleIssuance->isFieldPresent(sfIssuerElGamalPublicKey))
        return tecNO_PERMISSION;

    // Check if clawback is allowed
    if (!sleIssuance->isFlag(lsfMPTCanClawback))
        return tecNO_PERMISSION;

    // Check holder's MPToken
    auto const sleHolderMPToken =
        ctx.view.read(keylet::mptoken(mptIssuanceID, holder));
    if (!sleHolderMPToken)
        return tecOBJECT_NOT_FOUND;

    // Check if holder has confidential balances to claw back
    if (!sleHolderMPToken->isFieldPresent(sfIssuerEncryptedBalance))
        return tecNO_PERMISSION;

    // Sanity check: claw amount can not exceed confidential outstanding amount
    if (ctx.tx[sfMPTAmount] >
        (*sleIssuance)[~sfConfidentialOutstandingAmount].value_or(0))
        return tecINSUFFICIENT_FUNDS;

    // todo: ZKP Verification
    // verify the MPT amount to clawback is the holder's confidential balance

    // if (!isTesSuccess(terProof))
    //     return tecBAD_PROOF;

    return tesSUCCESS;
}

TER
ConfidentialClawback::doApply()
{
    auto const mptIssuanceID = ctx_.tx[sfMPTokenIssuanceID];
    auto const holder = ctx_.tx[sfHolder];

    auto sleIssuance = view().peek(keylet::mptIssuance(mptIssuanceID));
    auto sleHolderMPToken = view().peek(keylet::mptoken(mptIssuanceID, holder));

    if (!sleIssuance || !sleHolderMPToken)
        return tecINTERNAL;

    auto const clawAmount = ctx_.tx[sfMPTAmount];

    Slice const holderPubKey = (*sleHolderMPToken)[sfHolderElGamalPublicKey];
    Slice const issuerPubKey = (*sleIssuance)[sfIssuerElGamalPublicKey];

    // Encrypt zero amount
    Buffer encZeroForHolder;
    Buffer encZeroForIssuer;
    try
    {
        encZeroForHolder =
            encryptCanonicalZeroAmount(holderPubKey, holder, mptIssuanceID);

        encZeroForIssuer =
            encryptCanonicalZeroAmount(issuerPubKey, holder, mptIssuanceID);
    }
    catch (std::exception const& e)
    {
        JLOG(ctx_.journal.error())
            << "ConfidentialClawback: Failed to generate canonical zero: "
            << e.what();
        return tecINTERNAL;
    }

    // Set holder's confidential balances to encrypted zero
    (*sleHolderMPToken)[sfConfidentialBalanceInbox] = encZeroForHolder;
    (*sleHolderMPToken)[sfConfidentialBalanceSpending] = encZeroForHolder;
    (*sleHolderMPToken)[sfIssuerEncryptedBalance] = encZeroForIssuer;
    (*sleHolderMPToken)[sfConfidentialBalanceVersion] = 0;

    // Decrease Global Confidential Outstanding Amount
    auto const oldCOA = (*sleIssuance)[sfConfidentialOutstandingAmount];
    (*sleIssuance)[sfConfidentialOutstandingAmount] = oldCOA - clawAmount;

    // Decrease Global Total Outstanding Amount
    auto const oldOA = (*sleIssuance)[sfOutstandingAmount];
    (*sleIssuance)[sfOutstandingAmount] = oldOA - clawAmount;

    view().update(sleHolderMPToken);
    view().update(sleIssuance);

    return tesSUCCESS;
}
}  // namespace ripple
