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
#include <optional>
#include <set>
#include <utility>

namespace xrpl {

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

// Note, depth parameter is used to limit the recursion depth
[[nodiscard]] bool
isVaultPseudoAccountFrozen(
    ReadView const& view,
    AccountID const& account,
    MPTIssue const& mptShare,
    std::uint8_t depth);

[[nodiscard]] bool
isLPTokenFrozen(
    ReadView const& view,
    AccountID const& account,
    Asset const& asset,
    Asset const& asset2);

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
    char const* reason);

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

[[nodiscard]] TER
dirLink(
    ApplyView& view,
    AccountID const& owner,
    SLE::pointer& object,
    SF_UINT64 const& node = sfOwnerNode);

/** Checks that can withdraw funds from an object to itself or a destination.
 *
 * The receiver may be either the submitting account (sfAccount) or a different
 * destination account (sfDestination).
 *
 *    - Checks that the receiver account exists.
 *    - If the receiver requires a destination tag, check that one exists, even
 *      if withdrawing to self.
 *    - If withdrawing to self, succeed.
 *    - If not, checks if the receiver requires deposit authorization, and if
 *      the sender has it.
 *    - Checks that the receiver will not exceed the limit (IOU trustline limit
 *      or MPT MaximumAmount).
 */
[[nodiscard]] TER
canWithdraw(
    ReadView const& view,
    AccountID const& from,
    AccountID const& to,
    SLE::const_ref toSle,
    STAmount const& amount,
    bool hasDestinationTag);

/** Checks that can withdraw funds from an object to itself or a destination.
 *
 * The receiver may be either the submitting account (sfAccount) or a different
 * destination account (sfDestination).
 *
 *    - Checks that the receiver account exists.
 *    - If the receiver requires a destination tag, check that one exists, even
 *      if withdrawing to self.
 *    - If withdrawing to self, succeed.
 *    - If not, checks if the receiver requires deposit authorization, and if
 *      the sender has it.
 *    - Checks that the receiver will not exceed the limit (IOU trustline limit
 *      or MPT MaximumAmount).
 */
[[nodiscard]] TER
canWithdraw(
    ReadView const& view,
    AccountID const& from,
    AccountID const& to,
    STAmount const& amount,
    bool hasDestinationTag);

/** Checks that can withdraw funds from an object to itself or a destination.
 *
 * The receiver may be either the submitting account (sfAccount) or a different
 * destination account (sfDestination).
 *
 *    - Checks that the receiver account exists.
 *    - If the receiver requires a destination tag, check that one exists, even
 *      if withdrawing to self.
 *    - If withdrawing to self, succeed.
 *    - If not, checks if the receiver requires deposit authorization, and if
 *      the sender has it.
 *    - Checks that the receiver will not exceed the limit (IOU trustline limit
 *      or MPT MaximumAmount).
 */
[[nodiscard]] TER
canWithdraw(ReadView const& view, STTx const& tx);

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

/** Deleter function prototype. Returns the status of the entry deletion
 * (if should not be skipped) and if the entry should be skipped. The status
 * is always tesSUCCESS if the entry should be skipped.
 */
using EntryDeleter =
    std::function<std::pair<TER, SkipEntry>(LedgerEntryType, uint256 const&, SLE::pointer&)>;
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

/** Has the specified time passed?

    @param now  the current time
    @param mark the cutoff point
    @return true if \a now refers to a time strictly after \a mark, else false.
*/
bool
after(NetClock::time_point now, std::uint32_t mark);

}  // namespace xrpl
