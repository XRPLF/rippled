//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2023 Ripple Labs Inc.

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

#include <xrpld/app/tx/detail/Clawback.h>
#include <xrpld/ledger/View.h>
#include <xrpl/basics/MPTAmount.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/st.h>

namespace ripple {

template <ValidIssueType T>
static NotTEC
preflightHelper(PreflightContext const& ctx);

template <>
NotTEC
preflightHelper<Issue>(PreflightContext const& ctx)
{
    if (ctx.tx.isFieldPresent(sfMPTokenHolder))
        return temMALFORMED;

    AccountID const issuer = ctx.tx[sfAccount];
    STAmount const clawAmount = ctx.tx[sfAmount];

    // The issuer field is used for the token holder instead
    AccountID const& holder = clawAmount.getIssuer();

    if (issuer == holder || isXRP(clawAmount) || clawAmount <= beast::zero)
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

template <>
NotTEC
preflightHelper<MPTIssue>(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureMPTokensV1))
        return temDISABLED;

    auto const mptHolder = ctx.tx[~sfMPTokenHolder];
    auto const clawAmount = ctx.tx[sfAmount];

    if (!mptHolder)
        return temMALFORMED;

    // issuer is the same as holder
    if (ctx.tx[sfAccount] == *mptHolder)
        return temMALFORMED;

    if (clawAmount.mpt() > MPTAmount{maxMPTokenAmount} ||
        clawAmount <= beast::zero)
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

NotTEC
Clawback::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureClawback))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfClawbackMask)
        return temINVALID_FLAG;

    if (auto const ret = std::visit(
            [&]<typename T>(T const&) { return preflightHelper<T>(ctx); },
            ctx.tx[sfAmount].asset().value());
        !isTesSuccess(ret))
        return ret;

    return preflight2(ctx);
}

template <ValidIssueType T>
static TER
preclaimHelper(
    PreclaimContext const& ctx,
    SLE const& sleIssuer,
    AccountID const& issuer,
    AccountID const& holder,
    STAmount const& clawAmount);

template <>
TER
preclaimHelper<Issue>(
    PreclaimContext const& ctx,
    SLE const& sleIssuer,
    AccountID const& issuer,
    AccountID const& holder,
    STAmount const& clawAmount)
{
    std::uint32_t const issuerFlagsIn = sleIssuer.getFieldU32(sfFlags);

    // If AllowTrustLineClawback is not set or NoFreeze is set, return no
    // permission
    if (!(issuerFlagsIn & lsfAllowTrustLineClawback) ||
        (issuerFlagsIn & lsfNoFreeze))
        return tecNO_PERMISSION;

    auto const sleRippleState =
        ctx.view.read(keylet::line(holder, issuer, clawAmount.getCurrency()));
    if (!sleRippleState)
        return tecNO_LINE;

    STAmount const balance = (*sleRippleState)[sfBalance];

    // If balance is positive, issuer must have higher address than holder
    if (balance > beast::zero && issuer < holder)
        return tecNO_PERMISSION;

    // If balance is negative, issuer must have lower address than holder
    if (balance < beast::zero && issuer > holder)
        return tecNO_PERMISSION;

    // At this point, we know that issuer and holder accounts
    // are correct and a trustline exists between them.
    //
    // Must now explicitly check the balance to make sure
    // available balance is non-zero.
    //
    // We can't directly check the balance of trustline because
    // the available balance of a trustline is prone to new changes (eg.
    // XLS-34). So we must use `accountHolds`.
    if (accountHolds(
            ctx.view,
            holder,
            clawAmount.getCurrency(),
            issuer,
            fhIGNORE_FREEZE,
            ctx.j) <= beast::zero)
        return tecINSUFFICIENT_FUNDS;

    return tesSUCCESS;
}

template <>
TER
preclaimHelper<MPTIssue>(
    PreclaimContext const& ctx,
    SLE const& sleIssuer,
    AccountID const& issuer,
    AccountID const& holder,
    STAmount const& clawAmount)
{
    auto const issuanceKey =
        keylet::mptIssuance(clawAmount.get<MPTIssue>().getMptID());
    auto const sleIssuance = ctx.view.read(issuanceKey);
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    if (!((*sleIssuance)[sfFlags] & lsfMPTCanClawback))
        return tecNO_PERMISSION;

    if (sleIssuance->getAccountID(sfIssuer) != issuer)
        return tecNO_PERMISSION;

    if (!ctx.view.exists(keylet::mptoken(issuanceKey.key, holder)))
        return tecOBJECT_NOT_FOUND;

    if (accountHolds(
            ctx.view,
            holder,
            clawAmount.get<MPTIssue>(),
            fhIGNORE_FREEZE,
            ahIGNORE_AUTH,
            ctx.j) <= beast::zero)
        return tecINSUFFICIENT_FUNDS;

    return tesSUCCESS;
}

TER
Clawback::preclaim(PreclaimContext const& ctx)
{
    AccountID const issuer = ctx.tx[sfAccount];
    auto const clawAmount = ctx.tx[sfAmount];
    AccountID const holder = clawAmount.holds<Issue>()
        ? clawAmount.getIssuer()
        : ctx.tx[sfMPTokenHolder];

    auto const sleIssuer = ctx.view.read(keylet::account(issuer));
    auto const sleHolder = ctx.view.read(keylet::account(holder));
    if (!sleIssuer || !sleHolder)
        return terNO_ACCOUNT;

    if (sleHolder->isFieldPresent(sfAMMID))
        return tecAMM_ACCOUNT;

    return std::visit(
        [&]<typename T>(T const&) {
            return preclaimHelper<T>(
                ctx, *sleIssuer, issuer, holder, clawAmount);
        },
        ctx.tx[sfAmount].asset().value());
}

template <ValidIssueType T>
static TER
applyHelper(ApplyContext& ctx);

template <>
TER
applyHelper<Issue>(ApplyContext& ctx)
{
    AccountID const issuer = ctx.tx[sfAccount];
    STAmount clawAmount = ctx.tx[sfAmount];
    AccountID const holder = clawAmount.getIssuer();  // cannot be reference

    // Replace the `issuer` field with issuer's account
    clawAmount.setIssuer(issuer);
    if (holder == issuer)
        return tecINTERNAL;

    // Get the spendable balance. Must use `accountHolds`.
    STAmount const spendableAmount = accountHolds(
        ctx.view(),
        holder,
        clawAmount.getCurrency(),
        clawAmount.getIssuer(),
        fhIGNORE_FREEZE,
        ctx.journal);

    return rippleCredit(
        ctx.view(),
        holder,
        issuer,
        std::min(spendableAmount, clawAmount),
        true,
        ctx.journal);
}

template <>
TER
applyHelper<MPTIssue>(ApplyContext& ctx)
{
    AccountID const issuer = ctx.tx[sfAccount];
    auto clawAmount = ctx.tx[sfAmount];
    AccountID const holder = ctx.tx[sfMPTokenHolder];

    // Get the spendable balance. Must use `accountHolds`.
    STAmount const spendableAmount = accountHolds(
        ctx.view(),
        holder,
        clawAmount.get<MPTIssue>(),
        fhIGNORE_FREEZE,
        ahIGNORE_AUTH,
        ctx.journal);

    return rippleCreditMPT(
        ctx.view(),
        holder,
        issuer,
        std::min(spendableAmount, clawAmount),
        ctx.journal);
}

TER
Clawback::doApply()
{
    return std::visit(
        [&]<typename T>(T const&) { return applyHelper<T>(ctx_); },
        ctx_.tx[sfAmount].asset().value());
}

}  // namespace ripple
