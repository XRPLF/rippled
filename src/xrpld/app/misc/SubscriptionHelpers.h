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

#ifndef RIPPLE_APP_MISC_SUBSCRIPTIONHELPERS_H_INCLUDED
#define RIPPLE_APP_MISC_SUBSCRIPTIONHELPERS_H_INCLUDED

#include <xrpld/app/ledger/Ledger.h>
#include <xrpld/app/paths/Flow.h>
#include <xrpld/app/tx/detail/MPTokenAuthorize.h>
#include <xrpld/ledger/ApplyView.h>
#include <xrpld/ledger/ReadView.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/scope.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

template <ValidIssueType T>
static TER
canTransferTokenHelper(
    ReadView const& view,
    AccountID const& account,
    AccountID const& dest,
    STAmount const& amount,
    beast::Journal const& j);

template <>
TER
canTransferTokenHelper<Issue>(
    ReadView const& view,
    AccountID const& account,
    AccountID const& dest,
    STAmount const& amount,
    beast::Journal const& j)
{
    AccountID issuer = amount.getIssuer();
    if (issuer == account)
    {
        JLOG(j.trace())
            << "canTransferTokenHelper: Issuer is the same as the account.";
        return tesSUCCESS;
    }

    // If the issuer does not exist, return tecNO_ISSUER
    auto const sleIssuer = view.read(keylet::account(issuer));
    if (!sleIssuer)
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Issuer does not exist.";
        return tecNO_ISSUER;
    }

    // If the account does not have a trustline to the issuer, return tecNO_LINE
    auto const sleRippleState =
        view.read(keylet::line(account, issuer, amount.getCurrency()));
    if (!sleRippleState)
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Trust line does not exist.";
        return tecNO_LINE;
    }

    STAmount const balance = (*sleRippleState)[sfBalance];

    // If balance is positive, issuer must have higher address than account
    if (balance > beast::zero && issuer < account)
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Invalid trust line state.";
        return tecNO_PERMISSION;
    }

    // If balance is negative, issuer must have lower address than account
    if (balance < beast::zero && issuer > account)
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Invalid trust line state.";
        return tecNO_PERMISSION;
    }

    // If the issuer has requireAuth set, check if the account is authorized
    if (auto const ter = requireAuth(view, amount.issue(), account);
        ter != tesSUCCESS)
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Account is not authorized";
        return ter;
    }

    // If the issuer has requireAuth set, check if the destination is authorized
    if (auto const ter = requireAuth(view, amount.issue(), dest);
        ter != tesSUCCESS)
    {
        JLOG(j.trace())
            << "canTransferTokenHelper: Destination is not authorized.";
        return ter;
    }

    // If the issuer has frozen the account, return tecFROZEN
    if (isFrozen(view, account, amount.issue()) ||
        isDeepFrozen(
            view, account, amount.issue().currency, amount.issue().account))
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Account is frozen.";
        return tecFROZEN;
    }

    // If the issuer has frozen the destination, return tecFROZEN
    if (isFrozen(view, dest, amount.issue()) ||
        isDeepFrozen(
            view, dest, amount.issue().currency, amount.issue().account))
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Destination is frozen.";
        return tecFROZEN;
    }

    STAmount const spendableAmount = accountHolds(
        view, account, amount.getCurrency(), issuer, fhIGNORE_FREEZE, j);

    // If the balance is less than or equal to 0, return
    // tecINSUFFICIENT_FUNDS
    if (spendableAmount <= beast::zero)
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Spendable amount is less "
                           "than or equal to 0.";
        return tecINSUFFICIENT_FUNDS;
    }

    // If the spendable amount is less than the amount, return
    // tecINSUFFICIENT_FUNDS
    if (spendableAmount < amount)
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Spendable amount is less "
                           "than the amount.";
        return tecINSUFFICIENT_FUNDS;
    }

    // If the amount is not addable to the balance, return tecPRECISION_LOSS
    if (!canAdd(spendableAmount, amount))
        return tecPRECISION_LOSS;

    return tesSUCCESS;
}

template <>
TER
canTransferTokenHelper<MPTIssue>(
    ReadView const& view,
    AccountID const& account,
    AccountID const& dest,
    STAmount const& amount,
    beast::Journal const& j)
{
    AccountID issuer = amount.getIssuer();
    if (issuer == account)
    {
        JLOG(j.trace())
            << "canTransferTokenHelper: Issuer is the same as the account.";
        return tesSUCCESS;
    }

    // If the mpt does not exist, return tecOBJECT_NOT_FOUND
    auto const issuanceKey =
        keylet::mptIssuance(amount.get<MPTIssue>().getMptID());
    auto const sleIssuance = view.read(issuanceKey);
    if (!sleIssuance)
    {
        JLOG(j.trace())
            << "canTransferTokenHelper: MPT issuance does not exist.";
        return tecOBJECT_NOT_FOUND;
    }

    // If the issuer is not the same as the issuer of the mpt, return
    // tecNO_PERMISSION
    if (sleIssuance->getAccountID(sfIssuer) != issuer)
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Issuer is not the same as "
                           "the issuer of the MPT.";
        return tecNO_PERMISSION;
    }

    // If the account does not have the mpt, return tecOBJECT_NOT_FOUND
    if (!view.exists(keylet::mptoken(issuanceKey.key, account)))
    {
        JLOG(j.trace())
            << "canTransferTokenHelper: Account does not have the MPT.";
        return tecOBJECT_NOT_FOUND;
    }

    // If the issuer has requireAuth set, check if the account is
    // authorized
    auto const& mptIssue = amount.get<MPTIssue>();
    if (auto const ter =
            requireAuth(view, mptIssue, account, AuthType::WeakAuth);
        ter != tesSUCCESS)
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Account is not authorized.";
        return ter;
    }

    // If the issuer has requireAuth set, check if the destination is
    // authorized
    if (auto const ter = requireAuth(view, mptIssue, dest, AuthType::WeakAuth);
        ter != tesSUCCESS)
    {
        JLOG(j.trace())
            << "canTransferTokenHelper: Destination is not authorized.";
        return ter;
    }

    // If the issuer has locked the account, return tecLOCKED
    if (isFrozen(view, account, mptIssue))
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Account is locked.";
        return tecLOCKED;
    }

    // If the issuer has locked the destination, return tecLOCKED
    if (isFrozen(view, dest, mptIssue))
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Destination is locked.";
        return tecLOCKED;
    }

    // If the mpt cannot be transferred, return tecNO_AUTH
    if (auto const ter = canTransfer(view, mptIssue, account, dest);
        ter != tesSUCCESS)
    {
        JLOG(j.trace()) << "canTransferTokenHelper: MPT cannot be transferred.";
        return ter;
    }

    STAmount const spendableAmount = accountHolds(
        view,
        account,
        amount.get<MPTIssue>(),
        fhIGNORE_FREEZE,
        ahIGNORE_AUTH,
        j);

    // If the balance is less than or equal to 0, return
    // tecINSUFFICIENT_FUNDS
    if (spendableAmount <= beast::zero)
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Spendable amount is less "
                           "than or equal to 0.";
        return tecINSUFFICIENT_FUNDS;
    }

    // If the spendable amount is less than the amount, return
    // tecINSUFFICIENT_FUNDS
    if (spendableAmount < amount)
    {
        JLOG(j.trace()) << "canTransferTokenHelper: Spendable amount is less "
                           "than the amount.";
        return tecINSUFFICIENT_FUNDS;
    }

    // If the amount is not addable to the balance, return tecPRECISION_LOSS
    if (!canAdd(spendableAmount, amount))
        return tecPRECISION_LOSS;

    return tesSUCCESS;
}

}  // namespace ripple

#endif
