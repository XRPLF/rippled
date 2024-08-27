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

template <ValidAmountType T>
static NotTEC
preflightHelper(PreflightContext const& ctx);

template <>
NotTEC
preflightHelper<STAmount>(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureClawback))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfClawbackMask)
        return temINVALID_FLAG;

    if (ctx.tx.isFieldPresent(sfMPTokenHolder))
        return temMALFORMED;

    AccountID const issuer = ctx.tx[sfAccount];
    STAmount const clawAmount = get<STAmount>(ctx.tx[sfAmount]);

    // The issuer field is used for the token holder instead
    AccountID const& holder = clawAmount.getIssuer();

    if (issuer == holder || isXRP(clawAmount) || clawAmount <= beast::zero)
        return temBAD_AMOUNT;

    return preflight2(ctx);
}

template <>
NotTEC
preflightHelper<STMPTAmount>(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureClawback))
        return temDISABLED;

    auto const mptHolder = ctx.tx[~sfMPTokenHolder];
    auto const clawAmount = get<STMPTAmount>(ctx.tx[sfAmount]);

    if (!ctx.rules.enabled(featureMPTokensV1))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (!mptHolder)
        return temMALFORMED;

    if (ctx.tx.getFlags() & tfClawbackMask)
        return temINVALID_FLAG;

    // issuer is the same as holder
    if (ctx.tx[sfAccount] == *mptHolder)
        return temMALFORMED;

    if (clawAmount > MPTAmount{maxMPTokenAmount} || clawAmount <= beast::zero)
        return temBAD_AMOUNT;

    return preflight2(ctx);
}

template <ValidAmountType T>
static TER
preclaimHelper(PreclaimContext const& ctx);

template <>
TER
preclaimHelper<STAmount>(PreclaimContext const& ctx)
{
    AccountID const issuer = ctx.tx[sfAccount];
    STAmount const clawAmount = get<STAmount>(ctx.tx[sfAmount]);
    AccountID const& holder = clawAmount.getIssuer();

    auto const sleIssuer = ctx.view.read(keylet::account(issuer));
    auto const sleHolder = ctx.view.read(keylet::account(holder));
    if (!sleIssuer || !sleHolder)
        return terNO_ACCOUNT;

    if (sleHolder->isFieldPresent(sfAMMID))
        return tecAMM_ACCOUNT;

    std::uint32_t const issuerFlagsIn = sleIssuer->getFieldU32(sfFlags);

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
preclaimHelper<STMPTAmount>(PreclaimContext const& ctx)
{
    AccountID const issuer = ctx.tx[sfAccount];
    auto const clawAmount = get<STMPTAmount>(ctx.tx[sfAmount]);
    AccountID const& holder = ctx.tx[sfMPTokenHolder];

    auto const sleIssuer = ctx.view.read(keylet::account(issuer));
    auto const sleHolder = ctx.view.read(keylet::account(holder));
    if (!sleIssuer || !sleHolder)
        return terNO_ACCOUNT;

    if (sleHolder->isFieldPresent(sfAMMID))
        return tecAMM_ACCOUNT;

    auto const issuanceKey = keylet::mptIssuance(clawAmount.issue().getMptID());
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
            clawAmount.issue(),
            fhIGNORE_FREEZE,
            ahIGNORE_AUTH,
            ctx.j) <= beast::zero)
        return tecINSUFFICIENT_FUNDS;

    return tesSUCCESS;
}

template <ValidAmountType T>
static TER
applyHelper(ApplyContext& ctx);

template <>
TER
applyHelper<STAmount>(ApplyContext& ctx)
{
    AccountID const& issuer = ctx.tx[sfAccount];
    STAmount clawAmount = get<STAmount>(ctx.tx[sfAmount]);
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
applyHelper<STMPTAmount>(ApplyContext& ctx)
{
    AccountID const& issuer = ctx.tx[sfAccount];
    auto clawAmount = get<STMPTAmount>(ctx.tx[sfAmount]);
    AccountID const holder = ctx.tx[sfMPTokenHolder];

    // Get the spendable balance. Must use `accountHolds`.
    STMPTAmount const spendableAmount = accountHolds(
        ctx.view(),
        holder,
        clawAmount.issue(),
        fhIGNORE_FREEZE,
        ahIGNORE_AUTH,
        ctx.journal);

    return rippleCredit(
        ctx.view(),
        holder,
        issuer,
        std::min(spendableAmount, clawAmount),
        ctx.journal);
}

NotTEC
Clawback::preflight(PreflightContext const& ctx)
{
    return std::visit(
        [&]<typename T>(T const&) { return preflightHelper<T>(ctx); },
        ctx.tx[sfAmount].getValue());
}

TER
Clawback::preclaim(PreclaimContext const& ctx)
{
    return std::visit(
        [&]<typename T>(T const&) { return preclaimHelper<T>(ctx); },
        ctx.tx[sfAmount].getValue());
}

TER
Clawback::doApply()
{
    return std::visit(
        [&]<typename T>(T const&) { return applyHelper<T>(ctx_); },
        ctx_.tx[sfAmount].getValue());
}

}  // namespace ripple
