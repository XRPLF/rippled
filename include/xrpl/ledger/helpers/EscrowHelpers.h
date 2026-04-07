#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTAmount.h>
#include <xrpl/protocol/Rate.h>

namespace xrpl {

template <ValidIssueType T>
TER
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
inline TER
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

    if (senderIssuer)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (receiverIssuer)
        return tesSUCCESS;

    if (!view.exists(trustLineKey) && createAsset)
    {
        // Can the account cover the trust line's reserve?
        if (std::uint32_t const ownerCount = {sleDest->at(sfOwnerCount)};
            xrpBalance < view.fees().accountReserve(ownerCount + 1))
        {
            JLOG(journal.trace()) << "Trust line does not exist. "
                                     "Insufficient reserve to create line.";

            return tecNO_LINE_INSUF_RESERVE;
        }

        Currency const currency = amount.getCurrency();
        STAmount initialBalance(amount.issue());
        initialBalance.setIssuer(noAccount());

        if (TER const ter = trustCreate(
                view,                                           // payment sandbox
                recvLow,                                        // is dest low?
                issuer,                                         // source
                receiver,                                       // destination
                trustLineKey.key,                               // ledger index
                sleDest,                                        // Account to add to
                false,                                          // authorize account
                (sleDest->getFlags() & lsfDefaultRipple) == 0,  //
                false,                                          // freeze trust line
                false,                                          // deep freeze trust line
                initialBalance,                                 // zero initial balance
                Issue(currency, receiver),                      // limit of zero
                0,                                              // quality in
                0,                                              // quality out
                journal);                                       // journal
            !isTesSuccess(ter))
        {
            return ter;  // LCOV_EXCL_LINE
        }

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
        auto const xferFee = amount.value() - divideRound(amount, lockedRate, amount.issue(), true);
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
        auto const ter = directSendNoFee(view, issuer, receiver, finalAmt, true, journal);
        if (!isTesSuccess(ter))
            return ter;  // LCOV_EXCL_LINE
    }
    return tesSUCCESS;
}

template <>
inline TER
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
    if (!view.exists(keylet::mptoken(issuanceKey.key, receiver)) && createAsset && !receiverIssuer)
    {
        if (std::uint32_t const ownerCount = {sleDest->at(sfOwnerCount)};
            xrpBalance < view.fees().accountReserve(ownerCount + 1))
        {
            return tecINSUFFICIENT_RESERVE;
        }

        if (auto const ter = createMPToken(view, mptID, receiver, 0); !isTesSuccess(ter))
        {
            return ter;  // LCOV_EXCL_LINE
        }

        // update owner count.
        adjustOwnerCount(view, sleDest, 1, journal);
    }

    if (!view.exists(keylet::mptoken(issuanceKey.key, receiver)) && !receiverIssuer)
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
        auto const xferFee = amount.value() - divideRound(amount, lockedRate, amount.asset(), true);
        // compute balance to transfer
        finalAmt = amount.value() - xferFee;
    }
    return unlockEscrowMPT(
        view,
        sender,
        receiver,
        finalAmt,
        view.rules().enabled(fixTokenEscrowV1) ? amount : finalAmt,
        journal);
}

}  // namespace xrpl
