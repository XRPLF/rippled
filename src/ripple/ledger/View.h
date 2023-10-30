//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_LEDGER_VIEW_H_INCLUDED
#define RIPPLE_LEDGER_VIEW_H_INCLUDED

#include <ripple/beast/utility/Journal.h>
#include <ripple/core/Config.h>
#include <ripple/ledger/ApplyView.h>
#include <ripple/ledger/OpenView.h>
#include <ripple/ledger/RawView.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/Rate.h>
#include <ripple/protocol/STLedgerEntry.h>
#include <ripple/protocol/STObject.h>
#include <ripple/protocol/STTx.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/TER.h>
#include <functional>
#include <map>
#include <memory>
#include <utility>

#include <vector>

namespace ripple {

enum class WaiveTransferFee : bool { No = false, Yes };
enum class SkipEntry : bool { No = false, Yes };

//------------------------------------------------------------------------------
//
// Observers
//
//------------------------------------------------------------------------------

/** Determines whether the given expiration time has passed.

    In the XRP Ledger, expiration times are defined as the number of whole
    seconds after the "Ripple Epoch" which, for historical reasons, is set
    to January 1, 2000 (00:00 UTC).

    This is like the way the Unix epoch works, except the Ripple Epoch is
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

/** Controls the treatment of frozen account balances */
enum FreezeHandling { fhIGNORE_FREEZE, fhZERO_IF_FROZEN };

[[nodiscard]] bool
isGlobalFrozen(ReadView const& view, AccountID const& issuer);

[[nodiscard]] bool
isIndividualFrozen(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer);

[[nodiscard]] inline bool
isIndividualFrozen(
    ReadView const& view,
    AccountID const& account,
    Issue const& issue)
{
    return isIndividualFrozen(view, account, issue.currency, issue.account);
}

[[nodiscard]] bool
isFrozen(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer);

[[nodiscard]] inline bool
isFrozen(ReadView const& view, AccountID const& account, Issue const& issue)
{
    return isFrozen(view, account, issue.currency, issue.account);
}

// Returns the amount an account can spend without going into debt.
//
// <-- saAmount: amount of currency held by account. May be negative.
[[nodiscard]] STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer,
    FreezeHandling zeroIfFrozen,
    beast::Journal j);

[[nodiscard]] STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    Issue const& issue,
    FreezeHandling zeroIfFrozen,
    beast::Journal j);

// Returns the amount an account can spend of the currency type saDefault, or
// returns saDefault if this account is the issuer of the currency in
// question. Should be used in favor of accountHolds when questioning how much
// an account can spend while also allowing currency issuers to spend
// unlimited amounts of their own currency (since they can always issue more).
[[nodiscard]] STAmount
accountFunds(
    ReadView const& view,
    AccountID const& id,
    STAmount const& saDefault,
    FreezeHandling freezeHandling,
    beast::Journal j);

// Return the account's liquid (not reserved) XRP.  Generally prefer
// calling accountHolds() over this interface.  However, this interface
// allows the caller to temporarily adjust the owner count should that be
// necessary.
//
// @param ownerCountAdj positive to add to count, negative to reduce count.
[[nodiscard]] XRPAmount
xrpLiquid(
    ReadView const& view,
    AccountID const& id,
    std::int32_t ownerCountAdj,
    beast::Journal j);

/** Iterate all items in the given directory. */
void
forEachItem(
    ReadView const& view,
    Keylet const& root,
    std::function<void(std::shared_ptr<SLE const> const&)> const& f);

/** Iterate all items after an item in the given directory.
    @param after The key of the item to start after
    @param hint The directory page containing `after`
    @param limit The maximum number of items to return
    @return `false` if the iteration failed
*/
bool
forEachItemAfter(
    ReadView const& view,
    Keylet const& root,
    uint256 const& after,
    std::uint64_t const hint,
    unsigned int limit,
    std::function<bool(std::shared_ptr<SLE const> const&)> const& f);

/** Iterate all items in an account's owner directory. */
inline void
forEachItem(
    ReadView const& view,
    AccountID const& id,
    std::function<void(std::shared_ptr<SLE const> const&)> const& f)
{
    return forEachItem(view, keylet::ownerDir(id), f);
}

/** Iterate all items after an item in an owner directory.
    @param after The key of the item to start after
    @param hint The directory page containing `after`
    @param limit The maximum number of items to return
    @return `false` if the iteration failed
*/
inline bool
forEachItemAfter(
    ReadView const& view,
    AccountID const& id,
    uint256 const& after,
    std::uint64_t const hint,
    unsigned int limit,
    std::function<bool(std::shared_ptr<SLE const> const&)> const& f)
{
    return forEachItemAfter(view, keylet::ownerDir(id), after, hint, limit, f);
}

[[nodiscard]] Rate
transferRate(ReadView const& view, AccountID const& issuer);

/** Returns `true` if the directory is empty
    @param key The key of the directory
*/
[[nodiscard]] bool
dirIsEmpty(ReadView const& view, Keylet const& k);

// Return the list of enabled amendments
[[nodiscard]] std::set<uint256>
getEnabledAmendments(ReadView const& view);

// Return a map of amendments that have achieved majority
using majorityAmendments_t = std::map<uint256, NetClock::time_point>;
[[nodiscard]] majorityAmendments_t
getMajorityAmendments(ReadView const& view);

/** Return the hash of a ledger by sequence.
    The hash is retrieved by looking up the "skip list"
    in the passed ledger. As the skip list is limited
    in size, if the requested ledger sequence number is
    out of the range of ledgers represented in the skip
    list, then std::nullopt is returned.
    @return The hash of the ledger with the
            given sequence number or std::nullopt.
*/
[[nodiscard]] std::optional<uint256>
hashOfSeq(ReadView const& ledger, LedgerIndex seq, beast::Journal journal);

/** Find a ledger index from which we could easily get the requested ledger

    The index that we return should meet two requirements:
        1) It must be the index of a ledger that has the hash of the ledger
            we are looking for. This means that its sequence must be equal to
            greater than the sequence that we want but not more than 256 greater
            since each ledger contains the hashes of the 256 previous ledgers.

        2) Its hash must be easy for us to find. This means it must be 0 mod 256
            because every such ledger is permanently enshrined in a LedgerHashes
            page which we can easily retrieve via the skip list.
*/
inline LedgerIndex
getCandidateLedger(LedgerIndex requested)
{
    return (requested + 255) & (~255);
}

/** Return false if the test ledger is provably incompatible
    with the valid ledger, that is, they could not possibly
    both be valid. Use the first form if you have both ledgers,
    use the second form if you have not acquired the valid ledger yet
*/
[[nodiscard]] bool
areCompatible(
    ReadView const& validLedger,
    ReadView const& testLedger,
    beast::Journal::Stream& s,
    const char* reason);

[[nodiscard]] bool
areCompatible(
    uint256 const& validHash,
    LedgerIndex validIndex,
    ReadView const& testLedger,
    beast::Journal::Stream& s,
    const char* reason);

//------------------------------------------------------------------------------
//
// Modifiers
//
//------------------------------------------------------------------------------

/** Adjust the owner count up or down. */
void
adjustOwnerCount(
    ApplyView& view,
    std::shared_ptr<SLE> const& sle,
    std::int32_t amount,
    beast::Journal j);

/** @{ */
/** Returns the first entry in the directory, advancing the index

    @deprecated These are legacy function that are considered deprecated
                and will soon be replaced with an iterator-based model
                that is easier to use. You should not use them in new code.

    @param view The view against which to operate
    @param root The root (i.e. first page) of the directory to iterate
    @param page The current page
    @param index The index inside the current page
    @param entry The entry at the current index

    @return true if the directory isn't empty; false otherwise
 */
bool
cdirFirst(
    ReadView const& view,
    uint256 const& root,
    std::shared_ptr<SLE const>& page,
    unsigned int& index,
    uint256& entry);

bool
dirFirst(
    ApplyView& view,
    uint256 const& root,
    std::shared_ptr<SLE>& page,
    unsigned int& index,
    uint256& entry);
/** @} */

/** @{ */
/** Returns the next entry in the directory, advancing the index

    @deprecated These are legacy function that are considered deprecated
                and will soon be replaced with an iterator-based model
                that is easier to use. You should not use them in new code.

    @param view The view against which to operate
    @param root The root (i.e. first page) of the directory to iterate
    @param page The current page
    @param index The index inside the current page
    @param entry The entry at the current index

    @return true if the directory isn't empty; false otherwise
 */
bool
cdirNext(
    ReadView const& view,
    uint256 const& root,
    std::shared_ptr<SLE const>& page,
    unsigned int& index,
    uint256& entry);

bool
dirNext(
    ApplyView& view,
    uint256 const& root,
    std::shared_ptr<SLE>& page,
    unsigned int& index,
    uint256& entry);
/** @} */

[[nodiscard]] std::function<void(SLE::ref)>
describeOwnerDir(AccountID const& account);

// VFALCO NOTE Both STAmount parameters should just
//             be "Amount", a unit-less number.
//
/** Create a trust line

    This can set an initial balance.
*/
[[nodiscard]] TER
trustCreate(
    ApplyView& view,
    const bool bSrcHigh,
    AccountID const& uSrcAccountID,
    AccountID const& uDstAccountID,
    uint256 const& uIndex,      // --> ripple state entry
    SLE::ref sleAccount,        // --> the account being set.
    const bool bAuth,           // --> authorize account.
    const bool bNoRipple,       // --> others cannot ripple through
    const bool bFreeze,         // --> funds cannot leave
    STAmount const& saBalance,  // --> balance of account being set.
                                // Issuer should be noAccount()
    STAmount const& saLimit,    // --> limit for account being set.
                                // Issuer should be the account being set.
    std::uint32_t uSrcQualityIn,
    std::uint32_t uSrcQualityOut,
    beast::Journal j);

[[nodiscard]] TER
trustDelete(
    ApplyView& view,
    std::shared_ptr<SLE> const& sleRippleState,
    AccountID const& uLowAccountID,
    AccountID const& uHighAccountID,
    beast::Journal j);

/** Delete an offer.

    Requirements:
        The passed `sle` be obtained from a prior
        call to view.peek()
*/
// [[nodiscard]] // nodiscard commented out so Flow, BookTip and others compile.
TER
offerDelete(ApplyView& view, std::shared_ptr<SLE> const& sle, beast::Journal j);

//------------------------------------------------------------------------------

//
// Money Transfers
//

// Direct send w/o fees:
// - Redeeming IOUs and/or sending sender's own IOUs.
// - Create trust line of needed.
// --> bCheckIssuer : normally require issuer to be involved.
// [[nodiscard]] // nodiscard commented out so DirectStep.cpp compiles.
TER
rippleCredit(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    const STAmount& saAmount,
    bool bCheckIssuer,
    beast::Journal j);

[[nodiscard]] TER
accountSend(
    ApplyView& view,
    AccountID const& from,
    AccountID const& to,
    const STAmount& saAmount,
    beast::Journal j,
    WaiveTransferFee waiveFee = WaiveTransferFee::No);

[[nodiscard]] TER
issueIOU(
    ApplyView& view,
    AccountID const& account,
    STAmount const& amount,
    Issue const& issue,
    beast::Journal j);

[[nodiscard]] TER
redeemIOU(
    ApplyView& view,
    AccountID const& account,
    STAmount const& amount,
    Issue const& issue,
    beast::Journal j);

[[nodiscard]] TER
transferXRP(
    ApplyView& view,
    AccountID const& from,
    AccountID const& to,
    STAmount const& amount,
    beast::Journal j);

/** Check if the account requires authorization.
 *   Return tecNO_AUTH or tecNO_LINE if it does
 *   and tesSUCCESS otherwise.
 */
[[nodiscard]] TER
requireAuth(ReadView const& view, Issue const& issue, AccountID const& account);

/** Deleter function prototype. Returns the status of the entry deletion
 * (if should not be skipped) and if the entry should be skipped. The status
 * is always tesSUCCESS if the entry should be skipped.
 */
using EntryDeleter = std::function<std::pair<TER, SkipEntry>(
    LedgerEntryType,
    uint256 const&,
    std::shared_ptr<SLE>&)>;
/** Cleanup owner directory entries on account delete.
 * Used for a regular and AMM accounts deletion. The caller
 * has to provide the deleter function, which handles details of
 * specific account-owned object deletion.
 * @return tecINCOMPLETE indicates maxNodesToDelete
 * are deleted and there remains more nodes to delete.
 */
[[nodiscard]] TER
cleanupOnAccountDelete(
    ApplyView& view,
    Keylet const& ownerDirKeylet,
    EntryDeleter const& deleter,
    beast::Journal j,
    std::optional<std::uint16_t> maxNodesToDelete = std::nullopt);

/** Delete trustline to AMM. The passed `sle` must be obtained from a prior
 * call to view.peek(). Fail if neither side of the trustline is AMM or
 * if ammAccountID is seated and is not one of the trustline's side.
 */
[[nodiscard]] TER
deleteAMMTrustLine(
    ApplyView& view,
    std::shared_ptr<SLE> sleState,
    std::optional<AccountID> const& ammAccountID,
    beast::Journal j);

/** Has the specified time passed?

    @param now  the current time
    @param mark the cutoff point
    @return true if \a now refers to a time strictly after \a mark, else false.
*/
bool
after(NetClock::time_point now, std::uint32_t mark);

}  // namespace ripple

#endif
