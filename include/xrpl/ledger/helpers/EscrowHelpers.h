#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
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
#include <xrpl/protocol/MPTAmount.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

namespace xrpl {

/**
 * Validate that @p account may lock @p amount of a token for later delivery
 * to @p dest.
 *
 * The lock-side counterpart of escrowUnlockPreclaimHelper: every issuer
 * control (locking opt-in, authorization, freeze/lock, transferability,
 * spendable balance) that gates locking token value lives here, so any
 * transactor that locks funds applies the same rules. The signature is
 * view-based rather than PreclaimContext-based so it can also run from
 * doApply.
 */
template <ValidIssueType T>
TER
escrowLockPreclaimHelper(
    ReadView const& view,
    AccountID const& account,
    AccountID const& dest,
    STAmount const& amount,
    beast::Journal j);

template <>
inline TER
escrowLockPreclaimHelper<Issue>(
    ReadView const& view,
    AccountID const& account,
    AccountID const& dest,
    STAmount const& amount,
    beast::Journal j)
{
    auto const& issue = amount.get<Issue>();
    auto const& issuer = amount.getIssuer();
    // If the issuer is the same as the account, return tecNO_PERMISSION
    if (issuer == account)
        return tecNO_PERMISSION;

    // If the lsfAllowTrustLineLocking is not enabled, return tecNO_PERMISSION
    auto const sleIssuer = view.read(keylet::account(issuer));
    if (!sleIssuer)
        return tecNO_ISSUER;
    if (!sleIssuer->isFlag(lsfAllowTrustLineLocking))
        return tecNO_PERMISSION;

    // If the account does not have a trustline to the issuer, return tecNO_LINE
    auto const sleRippleState = view.read(keylet::trustLine(account, issuer, issue.currency));
    if (!sleRippleState)
        return tecNO_LINE;

    STAmount const balance = (*sleRippleState)[sfBalance];

    // If balance is positive, issuer must have higher address than account
    if (balance > beast::kZero && issuer < account)
        return tecNO_PERMISSION;  // LCOV_EXCL_LINE

    // If balance is negative, issuer must have lower address than account
    if (balance < beast::kZero && issuer > account)
        return tecNO_PERMISSION;  // LCOV_EXCL_LINE

    // If the issuer has requireAuth set, check if the account is authorized
    if (auto const ter = requireAuth(view, issue, account); !isTesSuccess(ter))
        return ter;

    // If the issuer has requireAuth set, check if the destination is authorized
    if (auto const ter = requireAuth(view, issue, dest); !isTesSuccess(ter))
        return ter;

    // If the issuer has frozen the account, return tecFROZEN
    if (isFrozen(view, account, issue))
        return tecFROZEN;

    // If the issuer has frozen the destination, return tecFROZEN
    if (isFrozen(view, dest, issue))
        return tecFROZEN;

    STAmount const spendableAmount =
        accountHolds(view, account, issue.currency, issuer, FreezeHandling::IgnoreFreeze, j);

    // If the balance is less than or equal to 0, return tecINSUFFICIENT_FUNDS
    if (spendableAmount <= beast::kZero)
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
inline TER
escrowLockPreclaimHelper<MPTIssue>(
    ReadView const& view,
    AccountID const& account,
    AccountID const& dest,
    STAmount const& amount,
    beast::Journal j)
{
    AccountID const issuer = amount.getIssuer();
    // If the issuer is the same as the account, return tecNO_PERMISSION
    if (issuer == account)
        return tecNO_PERMISSION;

    // If the mpt does not exist, return tecOBJECT_NOT_FOUND
    auto const issuanceKey = keylet::mptokenIssuance(amount.get<MPTIssue>().getMptID());
    auto const sleIssuance = view.read(issuanceKey);
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
    if (!view.exists(keylet::mptoken(issuanceKey.key, account)))
        return tecOBJECT_NOT_FOUND;

    // If the issuer has requireAuth set, check if the account is
    // authorized
    auto const& mptIssue = amount.get<MPTIssue>();
    if (auto const ter = requireAuth(view, mptIssue, account, AuthType::WeakAuth);
        !isTesSuccess(ter))
        return ter;

    // If the issuer has requireAuth set, check if the destination is
    // authorized
    if (auto const ter = requireAuth(view, mptIssue, dest, AuthType::WeakAuth); !isTesSuccess(ter))
        return ter;

    // If the issuer has frozen the account, return tecLOCKED
    if (isFrozen(view, account, *sleIssuance))
        return tecLOCKED;

    // If the issuer has frozen the destination, return tecLOCKED
    if (isFrozen(view, dest, *sleIssuance))
        return tecLOCKED;

    // If the mpt cannot be transferred, return tecNO_AUTH
    if (auto const ter = canTransfer(view, mptIssue, account, dest); !isTesSuccess(ter))
        return ter;

    STAmount const spendableAmount = accountHolds(
        view,
        account,
        amount.get<MPTIssue>(),
        FreezeHandling::IgnoreFreeze,
        AuthHandling::IgnoreAuth,
        j);

    // If the balance is less than or equal to 0, return tecINSUFFICIENT_FUNDS
    if (spendableAmount <= beast::kZero)
        return tecINSUFFICIENT_FUNDS;

    // If the spendable amount is less than the amount, return
    // tecINSUFFICIENT_FUNDS
    if (spendableAmount < amount)
        return tecINSUFFICIENT_FUNDS;

    return tesSUCCESS;
}

template <ValidIssueType T>
TER
escrowLockApplyHelper(
    ApplyView& view,
    AccountID const& issuer,
    AccountID const& sender,
    STAmount const& amount,
    beast::Journal journal);

template <>
inline TER
escrowLockApplyHelper<Issue>(
    ApplyView& view,
    AccountID const& issuer,
    AccountID const& sender,
    STAmount const& amount,
    beast::Journal journal)
{
    // Defensive: Issuer cannot create an escrow
    if (issuer == sender)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const ter =
        directSendNoFee(view, sender, issuer, amount, !amount.holds<MPTIssue>(), journal);
    if (!isTesSuccess(ter))
        return ter;  // LCOV_EXCL_LINE
    return tesSUCCESS;
}

template <>
inline TER
escrowLockApplyHelper<MPTIssue>(
    ApplyView& view,
    AccountID const& issuer,
    AccountID const& sender,
    STAmount const& amount,
    beast::Journal journal)
{
    // Defensive: Issuer cannot create an escrow
    if (issuer == sender)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const ter = lockEscrowMPT(view, sender, amount, journal);
    if (!isTesSuccess(ter))
        return ter;  // LCOV_EXCL_LINE
    return tesSUCCESS;
}

template <ValidIssueType T>
TER
escrowUnlockPreclaimHelper(
    ReadView const& view,
    AccountID const& account,
    STAmount const& amount,
    bool checkFreeze = true);

template <>
inline TER
escrowUnlockPreclaimHelper<Issue>(
    ReadView const& view,
    AccountID const& account,
    STAmount const& amount,
    bool checkFreeze)
{
    AccountID const& issuer = amount.getIssuer();
    // If the issuer is the same as the account, return tesSUCCESS
    if (issuer == account)
        return tesSUCCESS;

    // If the issuer has requireAuth set, check if the destination is authorized
    if (auto const ter = requireAuth(view, amount.get<Issue>(), account); !isTesSuccess(ter))
        return ter;

    // If the issuer has deep frozen the destination, return tecFROZEN
    if (checkFreeze &&
        isDeepFrozen(view, account, amount.get<Issue>().currency, amount.getIssuer()))
        return tecFROZEN;

    return tesSUCCESS;
}

template <>
inline TER
escrowUnlockPreclaimHelper<MPTIssue>(
    ReadView const& view,
    AccountID const& account,
    STAmount const& amount,
    bool checkFreeze)
{
    AccountID const& issuer = amount.getIssuer();
    // If the issuer is the same as the account, return tesSUCCESS
    if (issuer == account)
        return tesSUCCESS;

    // If the mpt does not exist, return tecOBJECT_NOT_FOUND
    auto const issuanceKey = keylet::mptokenIssuance(amount.get<MPTIssue>().getMptID());
    auto const sleIssuance = view.read(issuanceKey);
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    // If the issuer has requireAuth set, check if the account is
    // authorized
    auto const& mptIssue = amount.get<MPTIssue>();
    if (auto const ter = requireAuth(view, mptIssue, account, AuthType::WeakAuth);
        !isTesSuccess(ter))
        return ter;

    // If the issuer has frozen the account, return tecLOCKED
    if (checkFreeze && isFrozen(view, account, *sleIssuance))
        return tecLOCKED;

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

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
    bool const senderIssuer = issuer == sender;
    bool const receiverIssuer = issuer == receiver;

    if (senderIssuer)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (receiverIssuer)
        return tesSUCCESS;

    auto const& issue = amount.get<Issue>();
    Keylet const trustLineKey = keylet::trustLine(receiver, issue);
    bool const recvLow = issuer > receiver;

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
        if (ctx.view.rules().enabled(fixCleanup3_4_0))
        {
            XRPL_ASSERT(
                lockedRate >= kParityRate,
                "xrpl::escrowUnlockApplyHelper<MPTIssue> : lockedRate is at least parity");
            // MPTs are integral, so round the delivered amount down and
            // charge any fractional transfer fee to the escrowed amount.
            auto const delivered =
                mulRatio(amount.mpt(), kParityRate.value, lockedRate.value, false);
            finalAmt = STAmount(amount.asset(), delivered.value());
        }
        else
        {
            // compute transfer fee, if any
            auto const xferFee =
                amount.value() - divideRound(amount, lockedRate, amount.asset(), true);
            // compute balance to transfer
            finalAmt = amount.value() - xferFee;
        }
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
