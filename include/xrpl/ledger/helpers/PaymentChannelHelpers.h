#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>
#include <optional>

namespace xrpl {

/**
 * Validate the token amount of a PaymentChannelCreate or PaymentChannelFund
 * transaction during preflight.
 *
 * @param rules The current ledger rules used to check amendment status.
 * @param amount The channel or funding amount from the transaction.
 * @return tesSUCCESS if the amount is valid; temBAD_AMOUNT, temBAD_CURRENCY,
 *     or temDISABLED otherwise.
 */
template <ValidIssueType T>
NotTEC
payChanAmountPreflightHelper(Rules const& rules, STAmount const& amount);

template <>
NotTEC
payChanAmountPreflightHelper<Issue>(Rules const&, STAmount const& amount);

template <>
NotTEC
payChanAmountPreflightHelper<MPTIssue>(Rules const& rules, STAmount const& amount);

/**
 * Check that locking @p amount debits the source's trust line by exactly
 * @p amount.
 *
 * @param view The ledger view holding the source's trust line.
 * @param account The channel source whose balance is debited.
 * @param amount The amount to lock in the channel.
 * @param j Journal passed to the balance lookup.
 * @return tecPRECISION_LOSS if @p amount is an IOU and the source's balance
 *     less @p amount is not exactly representable; tesSUCCESS otherwise,
 *     including for XRP and MPT amounts.
 */
TER
payChanLockPrecisionHelper(
    ReadView const& view,
    AccountID const& account,
    STAmount const& amount,
    beast::Journal j);

/**
 * Close a payment channel and return its remaining funds to the channel owner.
 *
 * @param slep The SLE for the PayChannel object to close.
 * @param ctx The apply view context (view and transaction) in which ledger
 *     state modifications are made.
 * @param key The ledger key identifying the PayChannel entry.
 * @param txAccount The account submitting the transaction that closes the
 *     channel.
 * @param j Journal used for fatal-level diagnostic messages.
 * @return tesSUCCESS on success; tefBAD_LEDGER if a directory removal
 *     fails; tefINTERNAL if the source account SLE cannot be found.
 */
TER
closeChannel(
    SLE::Ref slep,
    ApplyViewContext ctx,
    UInt256 const& key,
    AccountID const& txAccount,
    beast::Journal j);

/**
 * Add two uint32_t values with saturation at UINT32_MAX.
 *
 * @param rules  The current ledger rules used to check amendment status.
 * @param lhs    Left-hand operand.
 * @param rhs    Right-hand operand.
 * @return       @p lhs + @p rhs, saturated at UINT32_MAX when the amendment
 *               is active.
 */
uint32_t
saturatingAdd(Rules const& rules, uint32_t const lhs, uint32_t const rhs);

/**
 * Determine whether a payment channel time field represents an expired time.
 *
 * @param view       The apply view providing the parent close time and rules.
 * @param timeField  The optional expiry timestamp (seconds since the XRP
 *                   Ledger epoch).  If empty, the function returns false.
 * @return           @c true if @p timeField is set and the indicated time is
 *                   in the past relative to the view's parent close time;
 *                   @c false otherwise.
 */
bool
isChannelExpired(ApplyView const& view, std::optional<std::uint32_t> timeField);

}  // namespace xrpl
