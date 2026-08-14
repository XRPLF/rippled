#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

namespace xrpl {

// Deduct the transfer fee (if any) from an escrowed amount, returning the net
// amount delivered to the receiver. The transfer fee applies only when neither
// the sender nor the receiver is the issuer and the locked rate differs from
// parity.
//
// NOTE: Transfer fee in escrow works a bit differently from a normal payment.
// In escrow, the fee is deducted from the locked/sending amount, whereas in a
// normal payment, the transfer fee is taken on top of the sending amount.
//
// The net delivered amount is rounded down (fee rounded up in the issuer's
// favor). Rounding the net up (the legacy behavior) rounds the fee down, which
// for integral assets (e.g. MPT) collapses the fee to zero for small amounts
// and bypasses the transfer fee. Note that the legacy divideRound rounds to
// nearest, so the strict variant is used to floor the net. If the net rounds
// to zero the entire amount is consumed as the fee: the destination receives
// nothing, but the escrow still completes so the funds are not stuck (there
// may be no CancelAfter, so finish is the only exit).
[[nodiscard]] inline STAmount
escrowDeductTransferFee(
    ReadView const& view,
    Rate const& lockedRate,
    STAmount const& amount,
    bool senderIssuer,
    bool receiverIssuer)
{
    if ((senderIssuer || receiverIssuer) || lockedRate == kParityRate)
        return amount;

    bool const strictRounding = view.rules().enabled(fixCleanup3_4_0);
    auto const netAmt = strictRounding
        ? divideRoundStrict(amount, lockedRate, amount.asset(), false)
        : divideRound(amount, lockedRate, amount.asset(), true);
    // compute transfer fee, if any
    auto const xferFee = amount.value() - netAmt;
    // compute balance to transfer
    STAmount finalAmt = amount.value() - xferFee;

    return finalAmt;
}

template <ValidIssueType T>
TER
escrowUnlockApplyHelper(
    ApplyViewContext ctx,
    Rate lockedRate,
    SLE::ref sleDest,
    XRPAmount xrpBalance,
    STAmount const& amount,
    AccountID const& issuer,
    AccountID const& sender,
    AccountID const& receiver,
    bool createAsset,
    beast::Journal journal);

template <>
inline TER
escrowUnlockApplyHelper<Issue>(
    ApplyViewContext ctx,
    Rate lockedRate,
    SLE::ref sleDest,
    XRPAmount xrpBalance,
    STAmount const& amount,
    AccountID const& issuer,
    AccountID const& sender,
    AccountID const& receiver,
    bool createAsset,
    beast::Journal journal)
{
    auto const& issue = amount.get<Issue>();
    Keylet const trustLineKey = keylet::trustLine(receiver, issue);
    bool const recvLow = issuer > receiver;
    bool const senderIssuer = issuer == sender;
    bool const receiverIssuer = issuer == receiver;

    if (senderIssuer)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (receiverIssuer)
        return tesSUCCESS;

    if (!ctx.view.exists(trustLineKey) && createAsset)
    {
        // Can the account cover the trust line's reserve?
        auto const sponsorSle = getEffectiveTxReserveSponsor(ctx, sleDest);
        if (!sponsorSle)
            return sponsorSle.error();  // LCOV_EXCL_LINE

        if (auto const ret = checkReserve(
                ctx,
                sleDest,
                xrpBalance,
                *sponsorSle,
                {.ownerCountDelta = 1},
                journal,
                tecNO_LINE_INSUF_RESERVE);
            !isTesSuccess(ret))
        {
            JLOG(journal.trace()) << "Trust line does not exist. "
                                     "Insufficient reserve to create line.";
            return ret;
        }

        Currency const currency = issue.currency;
        STAmount initialBalance(issue);
        initialBalance.get<Issue>().account = noAccount();

        if (TER const ter = trustCreate(
                ctx.view,                            // payment sandbox
                recvLow,                             // is dest low?
                issuer,                              // source
                receiver,                            // destination
                trustLineKey.key,                    // ledger index
                sleDest,                             // Account to add to
                false,                               // authorize account
                !sleDest->isFlag(lsfDefaultRipple),  //
                false,                               // freeze trust line
                false,                               // deep freeze trust line
                initialBalance,                      // zero initial balance
                Issue(currency, receiver),           // limit of zero
                0,                                   // quality in
                0,                                   // quality out
                *sponsorSle,                         // sponsor
                journal);                            // journal
            !isTesSuccess(ter))
        {
            return ter;  // LCOV_EXCL_LINE
        }

        ctx.view.update(sleDest);
    }

    if (!ctx.view.exists(trustLineKey) && !receiverIssuer)
        return tecNO_LINE;

    auto const xferRate = transferRate(ctx.view, amount);
    // update if issuer rate is less than locked rate
    if (xferRate < lockedRate)
        lockedRate = xferRate;

    // Deduct the transfer fee (if any) from the escrowed amount.
    auto const finalAmt =
        escrowDeductTransferFee(ctx.view, lockedRate, amount, senderIssuer, receiverIssuer);

    // validate the line limit if the account submitting txn is not the receiver
    // of the funds
    if (!createAsset)
    {
        auto const sleRippleState = ctx.view.peek(trustLineKey);
        if (!sleRippleState)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        // if the issuer is the high, then we use the low limit
        // otherwise we use the high limit
        STAmount const lineLimit =
            sleRippleState->getFieldAmount(recvLow ? sfLowLimit : sfHighLimit);

        STAmount lineBalance = sleRippleState->getFieldAmount(sfBalance);

        // flip the sign of the line balance if the issuer is not high
        if (!recvLow)
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
        auto const ter = directSendNoFee(ctx.view, issuer, receiver, finalAmt, true, journal);
        if (!isTesSuccess(ter))
            return ter;  // LCOV_EXCL_LINE
    }
    return tesSUCCESS;
}

template <>
inline TER
escrowUnlockApplyHelper<MPTIssue>(
    ApplyViewContext ctx,
    Rate lockedRate,
    SLE::ref sleDest,
    XRPAmount xrpBalance,
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
    auto const issuanceKey = keylet::mptokenIssuance(mptID);
    auto const mptKeylet = keylet::mptoken(issuanceKey.key, receiver);
    if (!ctx.view.exists(mptKeylet) && createAsset && !receiverIssuer)
    {
        auto const sponsorSle = getEffectiveTxReserveSponsor(ctx, sleDest);
        if (!sponsorSle)
            return sponsorSle.error();  // LCOV_EXCL_LINE

        if (auto const ret = checkReserve(
                ctx, sleDest, xrpBalance, *sponsorSle, {.ownerCountDelta = 1}, journal);
            !isTesSuccess(ret))
            return ret;

        if (auto const ter = createMPToken(ctx.view, mptID, receiver, *sponsorSle, 0);
            !isTesSuccess(ter))
        {
            return ter;  // LCOV_EXCL_LINE
        }

        // update owner count.
        increaseOwnerCount(ctx.view, sleDest, *sponsorSle, 1, journal);
    }

    if (!ctx.view.exists(mptKeylet) && !receiverIssuer)
        return tecNO_PERMISSION;

    auto const xferRate = transferRate(ctx.view, amount);
    // update if issuer rate is less than locked rate
    if (xferRate < lockedRate)
        lockedRate = xferRate;

    // Deduct the transfer fee (if any) from the escrowed amount.
    auto const finalAmt =
        escrowDeductTransferFee(ctx.view, lockedRate, amount, senderIssuer, receiverIssuer);

    // Gross must be the originally locked amount so sfLockedAmount is fully
    // released and the fee (gross - net) is burned. Passing net as gross
    // unlocks only the delivered amount and leaves the fee stuck in
    // sfLockedAmount. That was the pre-fixTokenEscrowV1 behavior; once
    // fixCleanup3_4_0 charges a non-zero fee via divideRoundStrict, the
    // gross must be released even if V1 is off.
    bool const releaseGross =
        ctx.view.rules().enabled(fixTokenEscrowV1) || ctx.view.rules().enabled(fixCleanup3_4_0);
    return unlockEscrowMPT(
        ctx.view, sender, receiver, finalAmt, releaseGross ? amount : finalAmt, journal);
}

}  // namespace xrpl
