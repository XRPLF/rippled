#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

#include <expected>
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
accountReserve(
    ReadView const& view,
    SLE::const_ref sle,
    beast::Journal j,
    std::int32_t ownerCountAdj = 0,
    std::int32_t accountCountAdj = 0);

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
accountReserve(
    ReadView const& view,
    AccountID const& id,
    beast::Journal j,
    std::int32_t ownerCountAdj = 0,
    std::int32_t accountCountAdj = 0)
{
    return accountReserve(view, view.read(keylet::account(id)), j, ownerCountAdj, accountCountAdj);
}

/** @brief Return the hypothetical reserve required by an account with the provided counters.
 *
 *  @param view The ledger view to read from
 *  @param ownerCount Number of objects for which the account will be responsible.
 *  @param accountCount Number of accounts for which the account will be responsible.
 *                      Defaults to 1, as normally every account is responsible for its own reserve.
 *                      Can be 0 if the account is sponsored.
 *                      Can be greater than 1 if the account is sponsoring other accounts.
 *  @return The hypothetical reserve amount
 */
XRPAmount
baseAccountReserve(ReadView const& view, std::int32_t ownerCount, std::int32_t accountCount = 1);

/** Check if an account has insufficient reserve.
 *
 *  @param view The ledger view to read from
 *  @param tx The transaction being processed
 *  @param accSle The account's ledger entry
 *  @param accBalance The account's balance
 *  @param sponsorSle The sponsor's ledger entry (if applicable)
 *  @param ownerCountAdj Adjustment to the owner count
 *  @param accountCountAdj Adjustment to the account count (default: 0)
 *  @param j Journal for logging (default: null sink)
 *  @return Transaction result code
 */
[[nodiscard]] TER
checkInsufficientReserve(
    ReadView const& view,
    STTx const& tx,
    SLE::const_ref accSle,
    STAmount const& accBalance,
    SLE::const_ref sponsorSle,
    std::int32_t ownerCountAdj,
    std::int32_t accountCountAdj = 0,
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

/** Adjust the owner counters of the account up or down. If sponsor provided adjust its counters
 *  too.
 *
 *  @param view The apply view for making changes
 *  @param accountSle The account's ledger entry
 *  @param sponsorSle The sponsor's ledger entry (if applicable)
 *  @param accountCountAdj Adjustment amount for the account count
 *  @param j Journal for logging (default: null sink)
 */
void
adjustOwnerCount(
    ApplyViewContext const& ctx,
    std::int32_t ownerCountAdj,
    beast::Journal j = beast::Journal{beast::Journal::getNullSink()});

void
adjustOwnerCount(
    ApplyView& view,
    ReserveContext const& reserveCtx,
    std::int32_t ownerCountAdj,
    beast::Journal j = beast::Journal{beast::Journal::getNullSink()});

void
adjustOwnerCount(
    ApplyView& view,
    AccountID const& account,
    SLE::pointer sponsorSle,
    std::int32_t ownerCountAdj,
    beast::Journal j = beast::Journal{beast::Journal::getNullSink()});

void
adjustOwnerCount(
    ApplyView& view,
    SLE::pointer accountSle,
    SLE::pointer sponsorSle,
    std::int32_t ownerCountAdj,
    beast::Journal j = beast::Journal{beast::Journal::getNullSink()});

/** Adjust the owner counters of the account up or down. If object has sponsor adjust its counters
 *  too. Used primarily just before deleting the object.
 *
 *  @param view The apply view for making changes
 *  @param accountSle The account's ledger entry
 *  @param objectSle The object's ledger entry
 *  @param ownerCountAdj Adjustment amount for the account count
 *  @param j Journal for logging (default: null sink)
 */
void
adjustOwnerCountObj(
    ApplyViewContext const& ctx,
    SLE::ref objectSle,
    std::int32_t ownerCountAdj,
    beast::Journal j = beast::Journal{beast::Journal::getNullSink()});

void
adjustOwnerCountObj(
    ApplyView& view,
    SLE::pointer ownerSle,
    SLE::ref objectSle,
    std::int32_t ownerCountAdj,
    beast::Journal j = beast::Journal{beast::Journal::getNullSink()});

void
adjustOwnerCountObj(
    ApplyView& view,
    AccountID const& ownerID,
    SLE::ref objectSle,
    std::int32_t ownerCountAdj,
    beast::Journal j = beast::Journal{beast::Journal::getNullSink()});

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
