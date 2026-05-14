#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>

namespace xrpl {

/** Controls whether `cleanupOnAccountDelete()` adjusts the directory iterator
 *  after a deletion.
 *
 *  When `No`, the iterator position is decremented to compensate for the
 *  element shift caused by the deletion.  When `Yes`, the entry was
 *  intentionally left in place by the deleter, so no adjustment is made.
 */
enum class SkipEntry : bool { No = false, Yes };

//------------------------------------------------------------------------------
//
// Observers
//
//------------------------------------------------------------------------------

/** Determines whether the given expiration time has passed.

    In the XRP Ledger, expiration times are defined as the number of whole
    seconds after the "XRPL epoch" which, for historical reasons, is set
    to January 1, 2000 (00:00 UTC).

    This is like the way the Unix epoch works, except the XRPL epoch is
    precisely 946,684,800 seconds after the Unix Epoch.

    See https://xrpl.org/basic-data-types.html#specifying-time

    Expiration is defined in terms of the close time of the parent ledger,
    because we definitively know the time that it closed (since consensus
    agrees on time) but we do not know the closing time of the ledger that
    is under construction.

    @param view The ledger whose parent time is used as the clock.
    @param exp The optional expiration time we want to check.

    @returns `true` if `exp` is in the past; `false` otherwise.
 */
[[nodiscard]] bool
hasExpired(ReadView const& view, std::optional<std::uint32_t> const& exp);

/** Determines whether a vault pseudo-account's MPT share token is indirectly
 *  frozen because the vault's underlying asset is frozen.
 *
 *  Traverses: MPT issuance → issuer account root → vault object → vault asset,
 *  then delegates to `isAnyFrozen()`.  Returns `false` immediately if the
 *  `featureSingleAssetVault` amendment is not enabled.
 *
 *  @param view The ledger state to inspect.
 *  @param account The account whose holdings are being queried.
 *  @param mptShare The MPT share token issued by the vault pseudo-account.
 *  @param depth Recursion depth guard; returns `true` (conservatively frozen)
 *      if `depth >= kMAX_ASSET_CHECK_DEPTH`.
 *  @return `true` if the underlying asset is frozen for `account`; `false`
 *      otherwise or if the amendment is not enabled.
 */
[[nodiscard]] bool
isVaultPseudoAccountFrozen(
    ReadView const& view,
    AccountID const& account,
    MPTIssue const& mptShare,
    int depth);

/** Determines whether LP tokens for an AMM pool are frozen for an account.
 *
 *  LP tokens are considered frozen if *either* constituent asset of the pool
 *  is frozen for `account`.
 *
 *  @param view The ledger state to inspect.
 *  @param account The account whose holdings are being queried.
 *  @param asset The first asset of the AMM pool.
 *  @param asset2 The second asset of the AMM pool.
 *  @return `true` if either `asset` or `asset2` is frozen for `account`.
 */
[[nodiscard]] bool
isLPTokenFrozen(
    ReadView const& view,
    AccountID const& account,
    Asset const& asset,
    Asset const& asset2);

/** Returns the set of amendment hashes currently enabled on the ledger.
 *
 *  Reads from the singleton `keylet::amendments()` SLE.  If no amendments
 *  SLE exists or none are yet enabled, returns an empty set.
 *
 *  @param view The ledger state to query.
 *  @return A `std::set<uint256>` containing every enabled amendment hash.
 */
[[nodiscard]] std::set<uint256>
getEnabledAmendments(ReadView const& view);

/** Maps amendment hashes to the `NetClock::time_point` at which each first
 *  achieved validator supermajority.  Used by the amendment governance process
 *  to enforce the two-week waiting period before activation.
 */
using majorityAmendments_t = std::map<uint256, NetClock::time_point>;

/** Returns amendments that have achieved validator supermajority but are not
 *  yet enabled.
 *
 *  Reads the `sfMajorities` array from the singleton `keylet::amendments()`
 *  SLE and converts each entry's `sfCloseTime` to a `NetClock::time_point`.
 *  Returns an empty map if no SLE exists or no majority amendments are pending.
 *
 *  @param view The ledger state to query.
 *  @return A `majorityAmendments_t` mapping each amendment hash to the time
 *      at which it first achieved supermajority.
 */
[[nodiscard]] majorityAmendments_t
getMajorityAmendments(ReadView const& view);

/** Returns the hash of a past ledger by sequence number via the skip list.
 *
 *  Implements a three-tier lookup:
 *  1. **Trivial**: `seq == ledger.seq()` → returns the ledger's own hash;
 *     `seq == ledger.seq() - 1` → returns `parentHash` directly.
 *  2. **Within 256**: Reads the rolling `keylet::skip()` object, which stores
 *     the hashes of the previous ≤ 256 ledgers, and indexes by offset.
 *  3. **Aligned deep history**: For sequences that are multiples of 256, reads
 *     the permanent `LedgerHashes` page at `keylet::skip(seq)` and indexes into
 *     it.  Non-aligned sequences beyond the 256-ledger rolling window are not
 *     reachable and return `std::nullopt`.
 *
 *  @param ledger  The view from whose skip list the search starts.
 *  @param seq     The target ledger sequence number.
 *  @param journal Used to log warnings when the skip list is incomplete or the
 *      requested sequence is out of range.
 *  @return The hash of ledger `seq`, or `std::nullopt` if it cannot be
 *      determined from the available skip-list data.
 */
[[nodiscard]] std::optional<uint256>
hashOfSeq(ReadView const& ledger, LedgerIndex seq, beast::Journal journal);

/** Computes the nearest 256-aligned ledger sequence ≥ `requested`.
 *
 *  Every ledger whose sequence is a multiple of 256 permanently stores a
 *  `LedgerHashes` page (`keylet::skip(seq)`) containing the hashes of
 *  the preceding 256 ledgers.  That page is retrievable via the skip list,
 *  making it the ideal starting point for resolving an arbitrary past hash.
 *  The expression `(requested + 255) & (~255)` rounds up to the next 256
 *  boundary in a single instruction.
 *
 *  @param requested The target ledger sequence number.
 *  @return The smallest value ≥ `requested` that is divisible by 256.
 */
inline LedgerIndex
getCandidateLedger(LedgerIndex requested)
{
    return (requested + 255) & (~255);
}

/** Returns `false` if `testLedger` is provably on a different chain than
 *  `validLedger`.
 *
 *  Uses `hashOfSeq()` to walk the skip list of whichever ledger is later and
 *  confirms that the earlier ledger's hash appears in that list.  A mismatch
 *  proves a fork.  When the skip list is incomplete or the sequences are too
 *  far apart to compare, the function conservatively returns `true` (cannot
 *  prove incompatibility).  Diagnostic lines are written to `s` on mismatch.
 *
 *  Use this overload when both ledger objects are available.
 *
 *  @param validLedger The authoritative ledger.
 *  @param testLedger  The candidate ledger being verified.
 *  @param s           Journal stream for diagnostic messages on mismatch.
 *  @param reason      Short label prepended to log messages for context.
 *  @return `false` if a fork is proven; `true` otherwise.
 */
[[nodiscard]] bool
areCompatible(
    ReadView const& validLedger,
    ReadView const& testLedger,
    beast::Journal::Stream& s,
    char const* reason);

/** Returns `false` if `testLedger` is provably on a different chain than the
 *  ledger identified by `(validHash, validIndex)`.
 *
 *  Use this overload when the authoritative ledger object has not been fully
 *  loaded but its identity is known from consensus.
 *
 *  @param validHash   Hash of the authoritative ledger.
 *  @param validIndex  Sequence number of the authoritative ledger.
 *  @param testLedger  The candidate ledger being verified.
 *  @param s           Journal stream for diagnostic messages on mismatch.
 *  @param reason      Short label prepended to log messages for context.
 *  @return `false` if a fork is proven; `true` otherwise.
 */
[[nodiscard]] bool
areCompatible(
    uint256 const& validHash,
    LedgerIndex validIndex,
    ReadView const& testLedger,
    beast::Journal::Stream& s,
    char const* reason);

//------------------------------------------------------------------------------
//
// Modifiers
//
//------------------------------------------------------------------------------

/** Inserts an SLE into an account's owner directory and records the page.
 *
 *  Calls `view.dirInsert()` to append `object` to `owner`'s owner directory,
 *  then writes the assigned page number back into `object`'s `node` field.
 *
 *  @param view The mutable ledger view.
 *  @param owner The account whose owner directory receives the entry.
 *  @param object The SLE being linked; updated in-place with the page number.
 *  @param node The field on `object` that receives the directory page number;
 *      defaults to `sfOwnerNode`.
 *  @return `tecDIR_FULL` if the owner directory has no room; `tesSUCCESS`
 *      otherwise.
 */
[[nodiscard]] TER
dirLink(
    ApplyView& view,
    AccountID const& owner,
    std::shared_ptr<SLE>& object,
    SF_UINT64 const& node = sfOwnerNode);

/** Checks whether funds can be withdrawn from `from` to `to` given a
 *  pre-fetched destination SLE.
 *
 *  This is the innermost overload; use it when the caller already holds `toSle`
 *  to avoid a redundant ledger read.  Rules enforced in order:
 *  -  `toSle` must be non-null (destination account must exist).
 *  -  If `lsfRequireDestTag` is set, `hasDestinationTag` must be `true` even
 *     for self-sends.
 *  -  If `from == to`, succeed immediately.
 *  -  If `lsfDepositAuth` is set, `from` must have a pre-authorized
 *     `DepositPreauth` entry under `to`.
 *  -  For IOU amounts, the withdrawal must not push `to` past its trust-line
 *     credit limit.  MPT transfers skip this check because they move existing
 *     supply rather than creating new tokens.
 *
 *  @param view             Ledger state to query.
 *  @param from             Source account (e.g., vault or broker pseudo-account).
 *  @param to               Destination account.
 *  @param toSle            Pre-fetched SLE for `to`; may be null.
 *  @param amount           Asset and quantity being transferred.
 *  @param hasDestinationTag Whether the transaction includes `sfDestinationTag`.
 *  @return `tesSUCCESS`, or a `tec` code: `tecNO_DST` (account absent),
 *      `tecDST_TAG_NEEDED` (tag missing), `tecNO_PERMISSION` (deposit auth
 *      denied), or `tecNO_LINE` (IOU limit exceeded).
 */
[[nodiscard]] TER
canWithdraw(
    ReadView const& view,
    AccountID const& from,
    AccountID const& to,
    SLE::const_ref toSle,
    STAmount const& amount,
    bool hasDestinationTag);

/** Checks whether funds can be withdrawn from `from` to `to`.
 *
 *  Looks up the destination account SLE and delegates to the six-argument
 *  overload.  See that overload for the full rule set.
 *
 *  @param view             Ledger state to query.
 *  @param from             Source account.
 *  @param to               Destination account.
 *  @param amount           Asset and quantity being transferred.
 *  @param hasDestinationTag Whether the transaction includes `sfDestinationTag`.
 *  @return `tesSUCCESS` or a `tec` code; see the six-argument overload.
 */
[[nodiscard]] TER
canWithdraw(
    ReadView const& view,
    AccountID const& from,
    AccountID const& to,
    STAmount const& amount,
    bool hasDestinationTag);

/** Checks whether the withdrawal described by `tx` is permitted.
 *
 *  Extracts `sfAccount`, `sfDestination` (defaults to `sfAccount` when absent),
 *  `sfAmount`, and the presence of `sfDestinationTag` from the transaction, then
 *  delegates to the five-argument overload.  Intended for use in preclaim.
 *
 *  @param view Ledger state to query.
 *  @param tx   The withdrawal transaction (e.g., `VaultWithdraw` or
 *      `LoanBrokerCoverWithdraw`).
 *  @return `tesSUCCESS` or a `tec` code; see the six-argument overload.
 */
[[nodiscard]] TER
canWithdraw(ReadView const& view, STTx const& tx);

/** Executes the physical asset transfer from a pseudo-account to a destination.
 *
 *  When `dstAcct == senderAcct` (self-withdrawal), calls `addEmptyHolding()`
 *  to lazily create a trust line or MPToken record if one does not already
 *  exist (`tecDUPLICATE` is silently tolerated).  For third-party
 *  destinations, calls `verifyDepositPreauth()` to enforce deposit
 *  authorisation and prune any expired credential objects as a side-effect.
 *
 *  Before transferring, asserts via `accountHolds()` that `sourceAcct` holds
 *  at least `amount`; a shortfall surfaces as `tefINTERNAL` rather than an
 *  overdraft.  On success, calls `accountSend()` with `WaiveTransferFee::Yes`.
 *
 *  @param view The mutable ledger view.
 *  @param tx The originating transaction (used by `verifyDepositPreauth`).
 *  @param senderAcct The transaction submitter / withdrawal beneficiary.
 *  @param dstAcct The account that will receive the funds.
 *  @param sourceAcct The pseudo-account (vault, loan broker) holding the funds.
 *  @param priorBalance The XRP balance of `senderAcct` before the transaction,
 *      used for reserve calculation when creating an empty holding.
 *  @param amount The asset and quantity to transfer.
 *  @param j Journal for diagnostic logging.
 *  @return `tesSUCCESS` on success; `tefINTERNAL` if the source has
 *      insufficient balance; any TER propagated from `verifyDepositPreauth` or
 *      `accountSend` otherwise.
 */
[[nodiscard]] TER
doWithdraw(
    ApplyView& view,
    STTx const& tx,
    AccountID const& senderAcct,
    AccountID const& dstAcct,
    AccountID const& sourceAcct,
    XRPAmount priorBalance,
    STAmount const& amount,
    beast::Journal j);

/** Callback invoked by `cleanupOnAccountDelete()` for each owner-directory entry.
 *
 *  Returns a pair:
 *  - `TER` — `tesSUCCESS` if the entry was handled or intentionally skipped;
 *    any other code aborts the cleanup loop immediately.
 *  - `SkipEntry` — `Yes` if the entry was left in place (iterator must not be
 *    decremented); `No` if the entry was removed (iterator must be decremented
 *    to compensate for the index shift).
 *
 *  The `TER` value is always `tesSUCCESS` when `SkipEntry` is `Yes`.
 */
using EntryDeleter = std::function<
    std::pair<TER, SkipEntry>(LedgerEntryType, uint256 const&, std::shared_ptr<SLE>&)>;

/** Iterates an account's owner directory and removes entries via `deleter`.
 *
 *  Used by `DeleteAccount` and AMM account deletion.  Traversal uses the
 *  `dirFirst`/`dirNext` exposed-cursor pattern; after each successful removal
 *  the cursor is decremented by one to compensate for the index shift that
 *  occurs when an element is erased mid-iteration.  When the deleter leaves an
 *  entry in place (`SkipEntry::Yes`), the cursor is not adjusted.
 *
 *  When `maxNodesToDelete` is supplied and the limit is reached before the
 *  directory is empty, `tecINCOMPLETE` is returned, signaling the caller that
 *  the account-delete transaction must be retried in a future ledger.
 *
 *  @param view              Mutable ledger view.
 *  @param ownerDirKeylet    Keylet of the account's owner directory root.
 *  @param deleter           Callback invoked once per directory entry.
 *  @param j                 Journal for diagnostic logging.
 *  @param maxNodesToDelete  Optional cap on entries processed per call.
 *      When absent, all entries are consumed in a single invocation.
 *  @return `tesSUCCESS` when the directory is fully processed;
 *      `tecINCOMPLETE` if `maxNodesToDelete` is exhausted with entries
 *      remaining; `tefBAD_LEDGER` if a ledger invariant is violated.
 */
[[nodiscard]] TER
cleanupOnAccountDelete(
    ApplyView& view,
    Keylet const& ownerDirKeylet,
    EntryDeleter const& deleter,
    beast::Journal j,
    std::optional<std::uint16_t> maxNodesToDelete = std::nullopt);

/** Has the specified time passed?

    @param now  the current time
    @param mark the cutoff point
    @return true if \a now refers to a time strictly after \a mark, else false.
*/
bool
after(NetClock::time_point now, std::uint32_t mark);

}  // namespace xrpl
