#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
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

    // Transfer Rate only applies when:
    // 1. Issuer is not involved in the transfer (senderIssuer or
    // receiverIssuer)
    // 2. The locked rate is different from the parity rate

    // NOTE: Transfer fee in escrow works a bit differently from a normal
    // payment. In escrow, the fee is deducted from the locked/sending amount,
    // whereas in a normal payment, the transfer fee is taken on top of the
    // sending amount.
    auto finalAmt = amount;
    if ((!senderIssuer && !receiverIssuer) && lockedRate != kParityRate)
    {
        // compute transfer fee, if any
        auto const xferFee =
            amount.value() - divideRound(amount, lockedRate, amount.get<Issue>(), true);
        // compute balance to transfer
        finalAmt = amount.value() - xferFee;
    }

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

    // Transfer Rate only applies when:
    // 1. Issuer is not involved in the transfer (senderIssuer or
    // receiverIssuer)
    // 2. The locked rate is different from the parity rate

    // NOTE: Transfer fee in escrow works a bit differently from a normal
    // payment. In escrow, the fee is deducted from the locked/sending amount,
    // whereas in a normal payment, the transfer fee is taken on top of the
    // sending amount.
    auto finalAmt = amount;
    if ((!senderIssuer && !receiverIssuer) && lockedRate != kParityRate)
    {
        // compute transfer fee, if any
        auto const xferFee = amount.value() - divideRound(amount, lockedRate, amount.asset(), true);
        // compute balance to transfer
        finalAmt = amount.value() - xferFee;
    }
    return unlockEscrowMPT(
        ctx.view,
        sender,
        receiver,
        finalAmt,
        ctx.view.rules().enabled(fixTokenEscrowV1) ? amount : finalAmt,
        journal);
}

}  // namespace xrpl
