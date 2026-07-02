#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <expected>
#include <optional>
#include <set>
#include <vector>

namespace xrpl {

/** Check if the issuer has the global freeze flag set.
    @param issuer The account to check
    @return true if the account has global freeze set
*/
[[nodiscard]] bool
isGlobalFrozen(ReadView const& view, AccountID const& issuer);

/** Calculate liquid XRP balance for an account.
 *
 *  This function may be used to calculate the amount of XRP that
 *  the holder is able to freely spend. It subtracts reserve requirements.
 *
 *  ownerCountAdj adjusts the owner count in case the caller calculates
 *  before ledger entries are added or removed. Positive to add, negative
 *  to subtract.
 *
 *  @param view The ledger view to read from
 *  @param id The account ID to check
 *  @param ownerCountAdj Positive to add to count, negative to reduce count
 *  @param j Journal for logging
 *  @return The liquid XRP amount available to the account
 */
[[nodiscard]] XRPAmount
xrpLiquid(ReadView const& view, AccountID const& id, std::int32_t ownerCountAdj, beast::Journal j);

struct Adjustment
{
    std::int32_t ownerCountDelta = 0;
    std::int32_t accountCountDelta = 0;
};

/** Returns the account reserve, in drops.
 *
 *  Actual owner count can be adjusted by delta in ownerCountAdj
 *  Actual reserve count can be adjusted by delta in accountCountAdj
 *  The reserve is calculated as:
 *  (ownerCount + "sponsoring object count" - "sponsored object count" + additionalOwnerCount) *
 *  increment + (1 if not sponsored account + sponsoringAccountCount) * "reserve base"
 *
 *  @param view The ledger view to read from
 *  @param sle The ledger entry for the account
 *  @param j Journal for logging
 *  @param ownerCountAdj Adjustment to the owner count (default: 0)
 *  @param accountCountAdj Adjustment to the account count (default: 0)
 *  @return The account reserve amount in drops
 */
[[nodiscard]] XRPAmount
accountReserve(ReadView const& view, SLE::const_ref sle, beast::Journal j, Adjustment adj = {});

/** Convenience overload that accepts AccountID instead of SLE.
 *
 *  @param view The ledger view to read from
 *  @param id The account ID
 *  @param j Journal for logging
 *  @param ownerCountAdj Adjustment to the owner count (default: 0)
 *  @param accountCountAdj Adjustment to the account count (default: 0)
 *  @return The account reserve amount in drops
 */
[[nodiscard]] inline XRPAmount
accountReserve(ReadView const& view, AccountID const& id, beast::Journal j, Adjustment adj = {})
{
    return accountReserve(view, view.read(keylet::account(id)), j, adj);
}

/** Check if an account has insufficient reserve.
 *
 *  The transaction-level reserve sponsor (if any) is taken from
 *  `ctx.txReserveContext` and, per XLS-68, is only applied when `accSle` is the
 *  tx.Account. For any other account the sponsor is ignored and the account
 *  must cover its own reserve.
 *
 *  @param ctx The apply-view context (provides the view, tx and reserve context)
 *  @param accSle The account's ledger entry
 *  @param accBalance The account's balance
 *  @param adj Adjustment to the owner/account counts
 *  @param j Journal for logging (default: null sink)
 *  @return Transaction result code
 */
[[nodiscard]] TER
checkInsufficientReserve(
    ApplyViewContext ctx,
    SLE::const_ref accSle,
    STAmount const& accBalance,
    Adjustment adj,
    beast::Journal j = beast::Journal{beast::Journal::getNullSink()});

/** Return number of the objects which reserve is covered by the account(sle) (so called "owner
 *  count"). Actual owner count can be adjusted by delta in ownerCountAdj.
 *
 *  @param sle The account's ledger entry
 *  @param j Journal for logging
 *  @param ownerCountAdj Adjustment to the owner count (default: 0)
 *  @return The adjusted owner count
 */
std::uint32_t
ownerCount(SLE::const_ref sle, beast::Journal j, std::int32_t ownerCountAdj = 0);

/** Increase owner-count fields when the caller supplies the sponsor.
 *
 *  This helper does not create a ledger object. It updates reserve accounting
 *  after the caller has created/updated an object.
 *  If sponsorSle is provided, this also adjusts the account's sponsored count
 *  and the sponsor's sponsoring count.
 *
 *  @param view The apply view for making changes
 *  @param reserveCtx The account and sponsor information
 *  @param count Amount to add to the owner count
 *  @param j Journal for logging
 */
void
increaseOwnerCount(
    ApplyView& view,
    ReserveContext const& reserveCtx,
    std::uint32_t count,
    beast::Journal j);

/** Decrease owner-count fields when the caller supplies the sponsor.
 *
 *  This helper does not delete a ledger object. It updates reserve accounting
 *  after the caller has removed an owner-counted reserve, or for special
 *  owner-count changes whose sponsor cannot be derived from an object's
 *  sfSponsor field.
 *
 *  @param view The apply view for making changes
 *  @param reserveCtx The account and sponsor information
 *  @param count Amount to remove from the owner count
 *  @param j Journal for logging
 */
void
decreaseOwnerCount(
    ApplyView& view,
    ReserveContext const& reserveCtx,
    std::uint32_t count,
    beast::Journal j);

/** Decrease owner-count fields for an existing ledger object.
 *
 *  This helper derives the reserve sponsor from objectSle's sfSponsor field,
 *  then updates the same owner-count fields as decreaseOwnerCount. Use this
 *  when removing an existing object whose reserve sponsor is stored on that
 *  object.
 *
 *  @param view The apply view for making changes
 *  @param accountSle The account's ledger entry
 *  @param objectSle The object's ledger entry
 *  @param count Amount to remove from the owner count
 *  @param j Journal for logging
 */
void
decreaseOwnerCountForObject(
    ApplyView& view,
    SLE::ref accountSle,
    SLE::ref objectSle,
    std::uint32_t count,
    beast::Journal j);

/** Convenience overload that accepts AccountID instead of account SLE reference.
 *
 *  @param view The apply view for making changes
 *  @param account The account ID
 *  @param objectSle The object's ledger entry
 *  @param count Amount to remove from the owner count
 *  @param j Journal for logging
 */
inline void
decreaseOwnerCountForObject(
    ApplyView& view,
    AccountID const& account,
    SLE::ref objectSle,
    std::uint32_t count,
    beast::Journal j)
{
    SLE::ref accountSle = view.peek(keylet::account(account));
    decreaseOwnerCountForObject(view, accountSle, objectSle, count, j);
}

/** Owner-count helpers scoped to the lending protocol. Kept in a nested
 *  namespace so this broker-specific handling can't be reached accidentally by
 *  the generic account-reserve code paths (which only ever operate on an
 *  ACCOUNT_ROOT).
 */
namespace lending {

/** Adjust a LoanBroker's own sfOwnerCount.
 *
 *  A LoanBroker's OwnerCount tracks the number of outstanding loans it holds,
 *  and is distinct from the broker's pseudo-account's owner count. Unlike an
 *  account's OwnerCount it carries no reserve and cannot be sponsored, so it is
 *  adjusted directly rather than through ReserveContext / increaseOwnerCount.
 *
 *  @param view      The apply view for making changes
 *  @param brokerSle The LoanBroker ledger entry
 *  @param amount    Signed amount to add to the loan count (nonzero)
 *  @param j         Journal for logging
 */
void
adjustOwnerCount(ApplyView& view, SLE::ref brokerSle, std::int32_t amount, beast::Journal j);

}  // namespace lending

/** Returns IOU issuer transfer fee as Rate. Rate specifies
 * the fee as fractions of 1 billion. For example, 1% transfer rate
 * is represented as 1,010,000,000.
 * @param issuer The IOU issuer
 */
[[nodiscard]] Rate
transferRate(ReadView const& view, AccountID const& issuer);

/** Generate a pseudo-account address from a pseudo owner key.
    @param pseudoOwnerKey The key to generate the address from
    @return The generated account ID
*/
AccountID
pseudoAccountAddress(ReadView const& view, uint256 const& pseudoOwnerKey);

/** Returns the list of fields that define an ACCOUNT_ROOT as a pseudo-account
    if set.

 The list is constructed during initialization and is const after that.
 Pseudo-account designator fields MUST be maintained by including the
 SField::sMD_PseudoAccount flag in the SField definition.
*/
[[nodiscard]] std::vector<SField const*> const&
getPseudoAccountFields();

/** Convenience overload that reads the account from the view. */
[[nodiscard]] bool
isPseudoAccount(SLE::const_ref sleAcct, std::set<SField const*> const& pseudoFieldFilter = {});

/** Convenience overload that reads the account from the view.
 *
 *  @param view The ledger view to read from
 *  @param accountId The account ID to check
 *  @param pseudoFieldFilter Optional set of specific pseudo-account fields to filter (default:
 * empty)
 *  @return true if the account is a pseudo-account (or matches the filter), false otherwise
 */
[[nodiscard]] inline bool
isPseudoAccount(
    ReadView const& view,
    AccountID const& accountId,
    std::set<SField const*> const& pseudoFieldFilter = {})
{
    return isPseudoAccount(view.read(keylet::account(accountId)), pseudoFieldFilter);
}

/**
 * Create pseudo-account, storing pseudoOwnerKey into ownerField.
 *
 * The list of valid ownerField is maintained in AccountRootHelpers.cpp and
 * the caller to this function must perform necessary amendment check(s)
 * before using a field. The amendment check is **not** performed in
 * createPseudoAccount.
 */
[[nodiscard]] std::expected<SLE::pointer, TER>
createPseudoAccount(ApplyView& view, uint256 const& pseudoOwnerKey, SField const& ownerField);

/** Checks the destination and tag.

- Checks that the SLE is not null.
- If the SLE requires a destination tag, checks that there is a tag.
*/
[[nodiscard]] TER
checkDestinationAndTag(SLE::const_ref toSle, bool hasDestinationTag);

}  // namespace xrpl
