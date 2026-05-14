/** @file
 *  Token-delivery helper for IOU and MPT escrow resolution.
 *
 *  Implements `escrowUnlockApplyHelper`, the single function responsible for
 *  crediting the appropriate account when an IOU or MPT escrow is finished
 *  (`EscrowFinish`) or cancelled (`EscrowCancel`) under `featureTokenEscrow`.
 *  The function is specialised once for `Issue` (IOU trust-line path) and once
 *  for `MPTIssue` (MPToken path); callers reach the correct specialisation via
 *  `std::visit` on the `Asset` variant, with zero runtime dispatch overhead.
 */
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

/** Credit an account with tokens held in escrow, applying transfer-fee logic.
 *
 *  Primary template — no body is provided. Only the `Issue` and `MPTIssue`
 *  full specialisations are defined. Callers should invoke via `std::visit`
 *  on an `Asset` variant so the compiler selects the correct specialisation
 *  at compile time.
 *
 *  @tparam T Asset type; must satisfy `ValidIssueType` (`Issue` or `MPTIssue`).
 *  @param view Mutable ledger view on which state changes are applied.
 *  @param lockedRate Transfer rate snapshotted at escrow creation time.
 *      Pass `kPARITY_RATE` for cancellations (return to sender, no fee).
 *  @param sleDest SLE for the destination account (`receiver`); used for
 *      owner-count and reserve checks when auto-creating a trust line or
 *      MPToken holding object.
 *  @param xrpBalance Pre-fee XRP balance of the destination account; compared
 *      against the incremental reserve required to create a new holding object.
 *  @param amount Escrowed token amount (face value locked at escrow creation).
 *  @param issuer Token issuer.
 *  @param sender Escrow creator / original token sender.
 *  @param receiver Account that will receive the unlocked tokens.
 *  @param createAsset When `true`, auto-creates a trust line or MPToken object
 *      for `receiver` if one does not already exist. Callers set this only
 *      when the transaction submitter is also the beneficiary, preserving
 *      account sovereignty over directory entries.
 *  @param journal Logging sink.
 *  @return `tesSUCCESS` on success, or a `tec` error code on failure.
 */
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

/** IOU trust-line specialisation of `escrowUnlockApplyHelper`.
 *
 *  Delivers IOU tokens from a finished or cancelled escrow to `receiver`,
 *  optionally creating the trust line and applying the snapshotted transfer
 *  fee.
 *
 *  **Issuer short-circuits.** `sender == issuer` returns `tecINTERNAL` (an
 *  issuer cannot be an escrow originator for their own obligation).
 *  `receiver == issuer` returns `tesSUCCESS` immediately — delivery to the
 *  issuer is a redemption handled by the calling transactor at the balance
 *  level.
 *
 *  **Trust line creation.** When `createAsset` is `true` and no trust line
 *  exists, one is created with a zero balance and zero limit via `trustCreate`.
 *  The `sfDefaultRipple` flag is inherited from `sleDest`. Reserve is checked
 *  first; insufficient reserve returns `tecNO_LINE_INSUF_RESERVE`. When
 *  `createAsset` is `false` and no line exists, returns `tecNO_LINE`.
 *
 *  **Transfer fee.** The effective rate is `min(lockedRate, currentRate)`,
 *  protecting the receiver from a rate increase during the escrow lifetime.
 *  The fee is deducted *from* `amount` (not added on top), so `receiver` gets
 *  `amount - fee`. When neither party is the issuer and the rate differs from
 *  `kPARITY_RATE`, the check against the trust-line limit uses `finalAmt`.
 *
 *  **Limit check.** When `createAsset` is `false`, the post-transfer balance
 *  is compared to `receiver`'s trust-line limit; `tecLIMIT_EXCEEDED` is
 *  returned if the delivery would exceed it. This check is skipped when
 *  `createAsset` is `true` because a freshly created line has a zero limit
 *  and would always fail it spuriously.
 *
 *  @note This function is reached via `std::visit` on an `Asset` variant in
 *      `EscrowFinish` and `EscrowCancel`. `EscrowCancel` always passes
 *      `kPARITY_RATE` so no fee is charged on the return-to-sender path.
 */
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
    Issue const& issue = amount.get<Issue>();
    Keylet const trustLineKey = keylet::line(receiver, issue);
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

        Currency const currency = issue.currency;
        STAmount initialBalance(issue);
        initialBalance.get<Issue>().account = noAccount();

        if (TER const ter = trustCreate(
                view,
                recvLow,
                issuer,
                receiver,
                trustLineKey.key,
                sleDest,
                false,
                (sleDest->getFlags() & lsfDefaultRipple) == 0,
                false,
                false,
                initialBalance,
                Issue(currency, receiver),
                0,
                0,
                journal);
            !isTesSuccess(ter))
        {
            return ter;  // LCOV_EXCL_LINE
        }

        view.update(sleDest);
    }

    if (!view.exists(trustLineKey) && !receiverIssuer)
        return tecNO_LINE;

    auto const xferRate = transferRate(view, amount);
    // Cap to the lower of the snapshotted and current rate to protect the receiver.
    if (xferRate < lockedRate)
        lockedRate = xferRate;

    // Fee is deducted from `amount` (not added on top): finalAmt = amount - fee.
    // No fee when either party is the issuer, or when lockedRate == kPARITY_RATE.
    auto finalAmt = amount;
    if ((!senderIssuer && !receiverIssuer) && lockedRate != kPARITY_RATE)
    {
        auto const xferFee =
            amount.value() - divideRound(amount, lockedRate, amount.get<Issue>(), true);
        finalAmt = amount.value() - xferFee;
    }

    // Limit check skipped when createAsset is true (freshly created line has
    // zero limit and would always fail spuriously).
    if (!createAsset)
    {
        auto const sleRippleState = view.peek(trustLineKey);
        if (!sleRippleState)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        // recvLow true → receiver is low side → use sfLowLimit; else sfHighLimit.
        STAmount const lineLimit =
            sleRippleState->getFieldAmount(recvLow ? sfLowLimit : sfHighLimit);

        STAmount lineBalance = sleRippleState->getFieldAmount(sfBalance);

        if (!recvLow)
            lineBalance.negate();

        lineBalance += finalAmt;

        if (lineLimit < lineBalance)
            return tecLIMIT_EXCEEDED;
    }

    if (!receiverIssuer)
    {
        auto const ter = directSendNoFee(view, issuer, receiver, finalAmt, true, journal);
        if (!isTesSuccess(ter))
            return ter;  // LCOV_EXCL_LINE
    }
    return tesSUCCESS;
}

/** MPT specialisation of `escrowUnlockApplyHelper`.
 *
 *  Delivers MPT tokens from a finished or cancelled escrow to `receiver`,
 *  optionally creating an MPToken holding object and applying the snapshotted
 *  transfer fee.
 *
 *  **MPToken creation.** When `createAsset` is `true`, `receiver` is not the
 *  issuer, and no MPToken SLE exists for this issuance, one is created via
 *  `createMPToken` and the owner count is incremented. Insufficient reserve
 *  returns `tecINSUFFICIENT_RESERVE`. If no MPToken exists after the creation
 *  attempt (and `receiver` is not the issuer), returns `tecNO_PERMISSION`.
 *
 *  **Transfer fee.** Identical to the `Issue` path: effective rate is
 *  `min(lockedRate, currentRate)`, fee is deducted *from* `amount`, and no
 *  fee is applied when either party is the issuer or the rate is parity.
 *
 *  **`fixTokenEscrowV1` bug fix.** The gross amount passed to `unlockEscrowMPT`
 *  (used to reduce `sfOutstandingAmount`) is `amount` when the amendment is
 *  enabled, and `finalAmt` otherwise. Without the fix, the outstanding supply
 *  is only reduced by the net delivered amount, silently retaining the fee
 *  portion; with the fix, the full face value is removed from circulation and
 *  the fee is burned from the outstanding supply.
 *
 *  @note `EscrowCancel` passes `kPARITY_RATE` so no fee is charged when
 *      tokens are returned to the original sender.
 */
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

        adjustOwnerCount(view, sleDest, 1, journal);
    }

    if (!view.exists(keylet::mptoken(issuanceKey.key, receiver)) && !receiverIssuer)
        return tecNO_PERMISSION;

    auto const xferRate = transferRate(view, amount);
    // Cap to the lower of the snapshotted and current rate to protect the receiver.
    if (xferRate < lockedRate)
        lockedRate = xferRate;

    // Fee is deducted from `amount` (not added on top): finalAmt = amount - fee.
    // No fee when either party is the issuer, or when lockedRate == kPARITY_RATE.
    auto finalAmt = amount;
    if ((!senderIssuer && !receiverIssuer) && lockedRate != kPARITY_RATE)
    {
        auto const xferFee = amount.value() - divideRound(amount, lockedRate, amount.asset(), true);
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
