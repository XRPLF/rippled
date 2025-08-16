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

#ifndef RIPPLE_APP_MISC_ESCROWUTILS_H_INLCUDED
#define RIPPLE_APP_MISC_ESCROWUTILS_H_INLCUDED

#include <xrpld/app/tx/detail/MPTokenAuthorize.h>
#include <xrpld/ledger/View.h>

#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTAmount.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>

namespace ripple {

template <ValidIssueType T>
static NotTEC
createPreflightHelper(PreflightContext const& ctx);

template <>
NotTEC
createPreflightHelper<Issue>(PreflightContext const& ctx)
{
    STAmount const amount = ctx.tx[sfAmount];
    if (amount.native() || amount <= beast::zero)
        return temBAD_AMOUNT;

    if (badCurrency() == amount.getCurrency())
        return temBAD_CURRENCY;

    return tesSUCCESS;
}

template <>
NotTEC
createPreflightHelper<MPTIssue>(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureMPTokensV1))
        return temDISABLED;

    auto const amount = ctx.tx[sfAmount];
    if (amount.native() || amount.mpt() > MPTAmount{maxMPTokenAmount} ||
        amount <= beast::zero)
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

template <ValidIssueType T>
static TER
createPreclaimHelper(
    PreclaimContext const& ctx,
    AccountID const& account,
    AccountID const& dest,
    STAmount const& amount);

template <>
TER
createPreclaimHelper<Issue>(
    PreclaimContext const& ctx,
    AccountID const& account,
    AccountID const& dest,
    STAmount const& amount)
{
    AccountID issuer = amount.getIssuer();
    // If the issuer is the same as the account, return tecNO_PERMISSION
    if (issuer == account)
        return tecNO_PERMISSION;

    // If the lsfAllowTrustLineLocking is not enabled, return tecNO_PERMISSION
    auto const sleIssuer = ctx.view.read(keylet::account(issuer));
    if (!sleIssuer)
        return tecNO_ISSUER;
    if (!sleIssuer->isFlag(lsfAllowTrustLineLocking))
        return tecNO_PERMISSION;

    // If the account does not have a trustline to the issuer, return tecNO_LINE
    auto const sleRippleState =
        ctx.view.read(keylet::line(account, issuer, amount.getCurrency()));
    if (!sleRippleState)
        return tecNO_LINE;

    STAmount const balance = (*sleRippleState)[sfBalance];

    // If balance is positive, issuer must have higher address than account
    if (balance > beast::zero && issuer < account)
        return tecNO_PERMISSION;  // LCOV_EXCL_LINE

    // If balance is negative, issuer must have lower address than account
    if (balance < beast::zero && issuer > account)
        return tecNO_PERMISSION;  // LCOV_EXCL_LINE

    // If the issuer has requireAuth set, check if the account is authorized
    if (auto const ter = requireAuth(ctx.view, amount.issue(), account);
        ter != tesSUCCESS)
        return ter;

    // If the issuer has requireAuth set, check if the destination is authorized
    if (auto const ter = requireAuth(ctx.view, amount.issue(), dest);
        ter != tesSUCCESS)
        return ter;

    // If the issuer has frozen the account, return tecFROZEN
    if (isFrozen(ctx.view, account, amount.issue()))
        return tecFROZEN;

    // If the issuer has frozen the destination, return tecFROZEN
    if (isFrozen(ctx.view, dest, amount.issue()))
        return tecFROZEN;

    STAmount const spendableAmount = accountHolds(
        ctx.view,
        account,
        amount.getCurrency(),
        issuer,
        fhIGNORE_FREEZE,
        ctx.j);

    // If the balance is less than or equal to 0, return tecINSUFFICIENT_FUNDS
    if (spendableAmount <= beast::zero)
        return tecINSUFFICIENT_FUNDS;

    // If the spendable amount is less than the amount, return
    // tecINSUFFICIENT_FUNDS
    if (spendableAmount < amount)
        return tecINSUFFICIENT_FUNDS;

    // If the amount is not addable to the balance, return tecPRECISION_LOSS
    if (!canAdd(spendableAmount, amount))
        return tecPRECISION_LOSS;

    return tesSUCCESS;
}

template <>
TER
createPreclaimHelper<MPTIssue>(
    PreclaimContext const& ctx,
    AccountID const& account,
    AccountID const& dest,
    STAmount const& amount)
{
    AccountID issuer = amount.getIssuer();
    // If the issuer is the same as the account, return tecNO_PERMISSION
    if (issuer == account)
        return tecNO_PERMISSION;

    // If the mpt does not exist, return tecOBJECT_NOT_FOUND
    auto const issuanceKey =
        keylet::mptIssuance(amount.get<MPTIssue>().getMptID());
    auto const sleIssuance = ctx.view.read(issuanceKey);
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    // If the lsfMPTCanEscrow is not enabled, return tecNO_PERMISSION
    if (!sleIssuance->isFlag(lsfMPTCanEscrow))
        return tecNO_PERMISSION;

    // If the issuer is not the same as the issuer of the mpt, return
    // tecNO_PERMISSION
    if (sleIssuance->getAccountID(sfIssuer) != issuer)
        return tecNO_PERMISSION;  // LCOV_EXCL_LINE

    // If the account does not have the mpt, return tecOBJECT_NOT_FOUND
    if (!ctx.view.exists(keylet::mptoken(issuanceKey.key, account)))
        return tecOBJECT_NOT_FOUND;

    // If the issuer has requireAuth set, check if the account is
    // authorized
    auto const& mptIssue = amount.get<MPTIssue>();
    if (auto const ter =
            requireAuth(ctx.view, mptIssue, account, AuthType::WeakAuth);
        ter != tesSUCCESS)
        return ter;

    // If the issuer has requireAuth set, check if the destination is
    // authorized
    if (auto const ter =
            requireAuth(ctx.view, mptIssue, dest, AuthType::WeakAuth);
        ter != tesSUCCESS)
        return ter;

    // If the issuer has frozen the account, return tecLOCKED
    if (isFrozen(ctx.view, account, mptIssue))
        return tecLOCKED;

    // If the issuer has frozen the destination, return tecLOCKED
    if (isFrozen(ctx.view, dest, mptIssue))
        return tecLOCKED;

    // If the mpt cannot be transferred, return tecNO_AUTH
    if (auto const ter = canTransfer(ctx.view, mptIssue, account, dest);
        ter != tesSUCCESS)
        return ter;

    STAmount const spendableAmount = accountHolds(
        ctx.view,
        account,
        amount.get<MPTIssue>(),
        fhIGNORE_FREEZE,
        ahIGNORE_AUTH,
        ctx.j);

    // If the balance is less than or equal to 0, return tecINSUFFICIENT_FUNDS
    if (spendableAmount <= beast::zero)
        return tecINSUFFICIENT_FUNDS;

    // If the spendable amount is less than the amount, return
    // tecINSUFFICIENT_FUNDS
    if (spendableAmount < amount)
        return tecINSUFFICIENT_FUNDS;

    return tesSUCCESS;
}

template <ValidIssueType T>
static TER
escrowLockApplyHelper(
    ApplyView& view,
    AccountID const& issuer,
    AccountID const& sender,
    STAmount const& amount,
    beast::Journal journal);

template <>
TER
escrowLockApplyHelper<Issue>(
    ApplyView& view,
    AccountID const& issuer,
    AccountID const& sender,
    STAmount const& amount,
    beast::Journal journal)
{
    // Defensive: Issuer cannot create an escrow
    // LCOV_EXCL_START
    if (issuer == sender)
        return tecINTERNAL;
    // LCOV_EXCL_STOP

    auto const ter = rippleCredit(
        view,
        sender,
        issuer,
        amount,
        amount.holds<MPTIssue>() ? false : true,
        journal);
    if (ter != tesSUCCESS)
        return ter;  // LCOV_EXCL_LINE
    return tesSUCCESS;
}

template <>
TER
escrowLockApplyHelper<MPTIssue>(
    ApplyView& view,
    AccountID const& issuer,
    AccountID const& sender,
    STAmount const& amount,
    beast::Journal journal)
{
    // Defensive: Issuer cannot create an escrow
    // LCOV_EXCL_START
    if (issuer == sender)
        return tecINTERNAL;
    // LCOV_EXCL_STOP

    auto const ter = rippleLockEscrowMPT(view, sender, amount, journal);
    if (ter != tesSUCCESS)
        return ter;  // LCOV_EXCL_LINE
    return tesSUCCESS;
}

template <ValidIssueType T>
static TER
escrowUnlockPreclaimHelper(
    ReadView const& view,
    AccountID const& account,
    STAmount const& amount,
    bool checkFreeze = true);

template <>
TER
escrowUnlockPreclaimHelper<Issue>(
    ReadView const& view,
    AccountID const& account,
    STAmount const& amount,
    bool checkFreeze)
{
    AccountID issuer = amount.getIssuer();
    // If the issuer is the same as the account, return tesSUCCESS
    if (issuer == account)
        return tesSUCCESS;

    // If the issuer has requireAuth set, check if the destination is authorized
    if (auto const ter = requireAuth(view, amount.issue(), account);
        ter != tesSUCCESS)
        return ter;

    // If the issuer has deep frozen the account, return tecFROZEN
    if (checkFreeze &&
        isDeepFrozen(view, account, amount.getCurrency(), amount.getIssuer()))
        return tecFROZEN;

    return tesSUCCESS;
}

template <>
TER
escrowUnlockPreclaimHelper<MPTIssue>(
    ReadView const& view,
    AccountID const& account,
    STAmount const& amount,
    bool checkFreeze)
{
    AccountID issuer = amount.getIssuer();
    // If the issuer is the same as the account, return tesSUCCESS
    if (issuer == account)
        return tesSUCCESS;

    // If the mpt does not exist, return tecOBJECT_NOT_FOUND
    auto const issuanceKey =
        keylet::mptIssuance(amount.get<MPTIssue>().getMptID());
    auto const sleIssuance = view.read(issuanceKey);
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    // If the issuer has requireAuth set, check if the account is
    // authorized
    auto const& mptIssue = amount.get<MPTIssue>();
    if (auto const ter =
            requireAuth(view, mptIssue, account, AuthType::WeakAuth);
        ter != tesSUCCESS)
        return ter;

    // If the issuer has frozen the account, return tecLOCKED
    if (checkFreeze && isFrozen(view, account, mptIssue))
        return tecLOCKED;

    return tesSUCCESS;
}

template <ValidIssueType T>
static TER
escrowUnlockApplyHelper(
    ApplyView& view,
    Rate lockedRate,
    std::shared_ptr<SLE> const& sleDest,
    STAmount const& xrpBalance,
    STAmount const& amount,
    AccountID const& issuer,
    AccountID const& sender,
    AccountID const& receiver,
    bool createAsset,
    beast::Journal journal);

template <>
TER
escrowUnlockApplyHelper<Issue>(
    ApplyView& view,
    Rate lockedRate,
    std::shared_ptr<SLE> const& sleDest,
    STAmount const& xrpBalance,
    STAmount const& amount,
    AccountID const& issuer,
    AccountID const& sender,
    AccountID const& receiver,
    bool createAsset,
    beast::Journal journal)
{
    Keylet const trustLineKey = keylet::line(receiver, amount.issue());
    bool const recvLow = issuer > receiver;
    bool const senderIssuer = issuer == sender;
    bool const receiverIssuer = issuer == receiver;
    bool const issuerHigh = issuer > receiver;

    // LCOV_EXCL_START
    if (senderIssuer)
        return tecINTERNAL;
    // LCOV_EXCL_STOP

    if (receiverIssuer)
        return tesSUCCESS;

    if (!view.exists(trustLineKey) && createAsset && !receiverIssuer)
    {
        // Can the account cover the trust line's reserve?
        if (std::uint32_t const ownerCount = {sleDest->at(sfOwnerCount)};
            xrpBalance < view.fees().accountReserve(ownerCount + 1))
        {
            JLOG(journal.trace()) << "Trust line does not exist. "
                                     "Insufficent reserve to create line.";

            return tecNO_LINE_INSUF_RESERVE;
        }

        Currency const currency = amount.getCurrency();
        STAmount initialBalance(amount.issue());
        initialBalance.setIssuer(noAccount());

        // clang-format off
        if (TER const ter = trustCreate(
                view,                           // payment sandbox
                recvLow,                        // is dest low?
                issuer,                         // source
                receiver,                       // destination
                trustLineKey.key,               // ledger index
                sleDest,                        // Account to add to
                false,                          // authorize account
                (sleDest->getFlags() & lsfDefaultRipple) == 0,
                false,                          // freeze trust line
                false,                          // deep freeze trust line
                initialBalance,                 // zero initial balance
                Issue(currency, receiver),      // limit of zero
                0,                              // quality in
                0,                              // quality out
                journal);                       // journal
            !isTesSuccess(ter))
        {
            return ter; // LCOV_EXCL_LINE
        }
        // clang-format on

        view.update(sleDest);
    }

    if (!view.exists(trustLineKey) && !receiverIssuer)
        return tecNO_LINE;

    auto const xferRate = transferRate(view, amount);
    // update if issuer rate is less than locked rate
    if (xferRate < lockedRate)
        lockedRate = xferRate;

    // Transfer Rate only applies when:
    // 1. Issuer is not involved in the transfer (senderIssuer or
    // receiverIssuer)
    // 2. The locked rate is different from the parity rate

    // NOTE: Transfer fee in escrow works a bit differently from a normal
    // payment. In escrow, the fee is deducted from the locked/sending amount,
    // whereas in a normal payment, the transfer fee is taken on top of the
    // sending amount.
    auto finalAmt = amount;
    if ((!senderIssuer && !receiverIssuer) && lockedRate != parityRate)
    {
        // compute transfer fee, if any
        auto const xferFee = amount.value() -
            divideRound(amount, lockedRate, amount.issue(), true);
        // compute balance to transfer
        finalAmt = amount.value() - xferFee;
    }

    // validate the line limit if the account submitting txn is not the receiver
    // of the funds
    if (!createAsset)
    {
        auto const sleRippleState = view.peek(trustLineKey);
        if (!sleRippleState)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        // if the issuer is the high, then we use the low limit
        // otherwise we use the high limit
        STAmount const lineLimit = sleRippleState->getFieldAmount(
            issuerHigh ? sfLowLimit : sfHighLimit);

        STAmount lineBalance = sleRippleState->getFieldAmount(sfBalance);

        // flip the sign of the line balance if the issuer is not high
        if (!issuerHigh)
            lineBalance.negate();

        // add the final amount to the line balance
        lineBalance += finalAmt;

        // if the transfer would exceed the line limit return tecLIMIT_EXCEEDED
        if (lineLimit < lineBalance)
            return tecLIMIT_EXCEEDED;
    }

    // if destination is not the issuer then transfer funds
    if (!receiverIssuer)
    {
        auto const ter =
            rippleCredit(view, issuer, receiver, finalAmt, true, journal);
        if (ter != tesSUCCESS)
            return ter;  // LCOV_EXCL_LINE
    }
    return tesSUCCESS;
}

template <>
TER
escrowUnlockApplyHelper<MPTIssue>(
    ApplyView& view,
    Rate lockedRate,
    std::shared_ptr<SLE> const& sleDest,
    STAmount const& xrpBalance,
    STAmount const& amount,
    AccountID const& issuer,
    AccountID const& sender,
    AccountID const& receiver,
    bool createAsset,
    beast::Journal journal)
{
    bool const senderIssuer = issuer == sender;
    bool const receiverIssuer = issuer == receiver;

    auto const mptID = amount.get<MPTIssue>().getMptID();
    auto const issuanceKey = keylet::mptIssuance(mptID);
    if (!view.exists(keylet::mptoken(issuanceKey.key, receiver)) &&
        createAsset && !receiverIssuer)
    {
        if (std::uint32_t const ownerCount = {sleDest->at(sfOwnerCount)};
            xrpBalance < view.fees().accountReserve(ownerCount + 1))
        {
            return tecINSUFFICIENT_RESERVE;
        }

        if (auto const ter =
                MPTokenAuthorize::createMPToken(view, mptID, receiver, 0);
            !isTesSuccess(ter))
        {
            return ter;  // LCOV_EXCL_LINE
        }

        // update owner count.
        adjustOwnerCount(view, sleDest, 1, journal);
    }

    if (!view.exists(keylet::mptoken(issuanceKey.key, receiver)) &&
        !receiverIssuer)
        return tecNO_PERMISSION;

    auto const xferRate = transferRate(view, amount);
    // update if issuer rate is less than locked rate
    if (xferRate < lockedRate)
        lockedRate = xferRate;

    // Transfer Rate only applies when:
    // 1. Issuer is not involved in the transfer (senderIssuer or
    // receiverIssuer)
    // 2. The locked rate is different from the parity rate

    // NOTE: Transfer fee in escrow works a bit differently from a normal
    // payment. In escrow, the fee is deducted from the locked/sending amount,
    // whereas in a normal payment, the transfer fee is taken on top of the
    // sending amount.
    auto finalAmt = amount;
    if ((!senderIssuer && !receiverIssuer) && lockedRate != parityRate)
    {
        // compute transfer fee, if any
        auto const xferFee = amount.value() -
            divideRound(amount, lockedRate, amount.asset(), true);
        // compute balance to transfer
        finalAmt = amount.value() - xferFee;
    }

    return rippleUnlockEscrowMPT(view, sender, receiver, finalAmt, journal);
}

}  // namespace ripple

#endif  // RIPPLE_APP_MISC_ESCROWUTILS_H_INLCUDED
