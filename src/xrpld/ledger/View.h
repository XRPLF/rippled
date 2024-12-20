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

#include <xrpld/core/Config.h>
#include <xrpld/ledger/ApplyView.h>
#include <xrpld/ledger/OpenView.h>
#include <xrpld/ledger/RawView.h>
#include <xrpld/ledger/ReadView.h>
#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>

#include <functional>
#include <map>
#include <memory>
#include <type_traits>
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

/** Controls the treatment of unauthorized MPT balances */
enum AuthHandling { ahIGNORE_AUTH, ahZERO_IF_UNAUTHORIZED };

[[nodiscard]] bool
isGlobalFrozen(ReadView const& view, AccountID const& issuer);

[[nodiscard]] bool
isGlobalFrozen(ReadView const& view, MPTIssue const& mptIssue);

[[nodiscard]] bool
isGlobalFrozen(ReadView const& view, Asset const& asset);

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
isIndividualFrozen(
    ReadView const& view,
    AccountID const& account,
    MPTIssue const& mptIssue);

[[nodiscard]] inline bool
isIndividualFrozen(
    ReadView const& view,
    AccountID const& account,
    Asset const& asset)
{
    return std::visit(
        [&](auto const& issue) {
            return isIndividualFrozen(view, account, issue);
        },
        asset.value());
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

[[nodiscard]] bool
isFrozen(
    ReadView const& view,
    AccountID const& account,
    MPTIssue const& mptIssue);

[[nodiscard]] inline bool
isFrozen(ReadView const& view, AccountID const& account, Asset const& asset)
{
    return std::visit(
        [&](auto const& issue) { return isFrozen(view, account, issue); },
        asset.value());
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

[[nodiscard]] STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    MPTIssue const& mptIssue,
    FreezeHandling zeroIfFrozen,
    AuthHandling zeroIfUnauthorized,
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

/** Returns IOU issuer transfer fee as Rate. Rate specifies
 * the fee as fractions of 1 billion. For example, 1% transfer rate
 * is represented as 1,010,000,000.
 * @param issuer The IOU issuer
 */
[[nodiscard]] Rate
transferRate(ReadView const& view, AccountID const& issuer);

/** Returns MPT transfer fee as Rate. Rate specifies
 * the fee as fractions of 1 billion. For example, 1% transfer rate
 * is represented as 1,010,000,000.
 * @param issuanceID MPTokenIssuanceID of MPTTokenIssuance object
 */
[[nodiscard]] Rate
transferRate(ReadView const& view, MPTID const& issuanceID);

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

/** Calls static rippleCreditIOU if saAmount represents Issue.
 * Calls static rippleCreditMPT if saAmount represents MPTIssue.
 */
TER
rippleCredit(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    bool bCheckIssuer,
    beast::Journal j);

/** Calls static accountSendIOU if saAmount represents Issue.
 * Calls static accountSendMPT if saAmount represents MPTIssue.
 */
[[nodiscard]] TER
accountSend(
    ApplyView& view,
    AccountID const& from,
    AccountID const& to,
    STAmount const& saAmount,
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

//------------------------------------------------------------------------------

//
// Trustline Locking and Transfer (PaychanAndEscrowForTokens)
//

// In functions white require a `RunType`
// pass DryRun (don't apply changes) or WetRun (do apply changes)
// to allow compile time evaluation of which types and calls to use

// For all functions below that take a Dry/Wet run parameter
// View may be ReadView const or ApplyView for DryRuns.
// View *must* be ApplyView for a WetRun.
// Passed SLEs must be non-const for WetRun.
#define DryRun RunType<bool, true>()
#define WetRun RunType<bool, false>()
template <class T, T V>
struct RunType
{
    // see:
    // http://alumni.media.mit.edu/~rahimi/compile-time-flags/
    constexpr
    operator T() const
    {
        static_assert(std::is_same<bool, T>::value);
        return V;
    }

    constexpr T
    operator!() const
    {
        static_assert(std::is_same<bool, T>::value);
        return !(V);
    }
};

// allow party lists to be logged easily
template <class T>
std::ostream&
operator<<(std::ostream& lhs, std::vector<T> const& rhs)
{
    lhs << "{";
    for (int i = 0; i < rhs.size(); ++i)
        lhs << rhs[i] << (i < rhs.size() - 1 ? ", " : "");
    lhs << "}";
    return lhs;
}
// Return true iff the acc side of line is in default state
bool
isTrustDefault(
    std::shared_ptr<SLE> const& acc,    // side to check
    std::shared_ptr<SLE> const& line);  // line to check

/** Lock or unlock a TrustLine balance.
    If positive deltaAmt lock the amount.
    If negative deltaAmt unlock the amount.
*/
template <class V, class S, class R>
[[nodiscard]] TER
trustAdjustLockedBalance(
    V& view,
    S& sleLine,
    STAmount const& deltaAmt,
    int deltaLockCount,  // if +1 lockCount is increased, -1 is decreased, 0
                         // unchanged
    beast::Journal const& j,
    R dryRun)
{
    static_assert(
        (std::is_same<V, ReadView const>::value &&
         std::is_same<S, std::shared_ptr<SLE const>>::value) ||
        (std::is_same<V, ApplyView>::value &&
         std::is_same<S, std::shared_ptr<SLE>>::value));

    // dry runs are explicit in code, but really the view type determines
    // what occurs here, so this combination is invalid.

    static_assert(!(std::is_same<V, ReadView const>::value && !dryRun));

    if (!view.rules().enabled(featurePaychanAndEscrowForTokens))
        return tefINTERNAL;

    if (!sleLine)
        return tecINTERNAL;

    auto const issuer = deltaAmt.getIssuer();

    STAmount lowLimit = sleLine->getFieldAmount(sfLowLimit);

    // the account which is modifying the LockedBalance is always
    // the side that isn't the issuer, so if the low side is the
    // issuer then the high side is the account.
    bool const high = lowLimit.getIssuer() == issuer;

    std::vector<AccountID> parties{
        high ? sleLine->getFieldAmount(sfHighLimit).getIssuer()
             : lowLimit.getIssuer()};

    // check for freezes & auth
    {
        TER result = trustTransferAllowed(view, parties, deltaAmt.issue(), j);

        JLOG(j.trace())
            << "trustAdjustLockedBalance: trustTransferAllowed result="
            << result;

        if (!isTesSuccess(result))
            return result;
    }

    // pull the TL balance from the account's perspective
    STAmount balance = high ? -(*sleLine)[sfBalance] : (*sleLine)[sfBalance];

    // this would mean somehow the issuer is trying to lock balance
    if (balance < beast::zero)
        return tecINTERNAL;

    if (deltaAmt == beast::zero)
        return tesSUCCESS;

    // can't lock or unlock a zero balance
    if (balance == beast::zero)
    {
        JLOG(j.trace()) << "trustAdjustLockedBalance failed, zero balance";
        return tecUNFUNDED_PAYMENT;
    }

    STAmount priorLockedBalance{sfLockedBalance, deltaAmt.issue()};
    if (sleLine->isFieldPresent(sfLockedBalance))
        priorLockedBalance =
            high ? -(*sleLine)[sfLockedBalance] : (*sleLine)[sfLockedBalance];

    uint32_t priorLockCount = 0;
    if (sleLine->isFieldPresent(sfLockCount))
        priorLockCount = sleLine->getFieldU32(sfLockCount);

    uint32_t finalLockCount = priorLockCount + deltaLockCount;
    STAmount finalLockedBalance = priorLockedBalance + deltaAmt;

    if (finalLockedBalance > balance)
    {
        JLOG(j.trace()) << "trustAdjustLockedBalance: "
                        << "lockedBalance(" << finalLockedBalance
                        << ") > balance(" << balance << ") = true\n";
        return tecUNFUNDED_PAYMENT;
    }

    if (finalLockedBalance < beast::zero)
        return tecINTERNAL;

    // check if there is significant precision loss
    if (!isAddable(balance, deltaAmt) ||
        !isAddable(priorLockedBalance, deltaAmt) ||
        !isAddable(finalLockedBalance, balance))
        return tecPRECISION_LOSS;

    // sanity check possible overflows on the lock counter
    if ((deltaLockCount > 0 && priorLockCount > finalLockCount) ||
        (deltaLockCount < 0 && priorLockCount < finalLockCount) ||
        (deltaLockCount == 0 && priorLockCount != finalLockCount))
        return tecINTERNAL;

    // we won't update any SLEs if it is a dry run
    if (dryRun)
        return tesSUCCESS;

    if constexpr (
        std::is_same<V, ApplyView>::value &&
        std::is_same<S, std::shared_ptr<SLE>>::value)
    {
        if (finalLockedBalance == beast::zero || finalLockCount == 0)
        {
            sleLine->makeFieldAbsent(sfLockedBalance);
            sleLine->makeFieldAbsent(sfLockCount);
        }
        else
        {
            sleLine->setFieldAmount(
                sfLockedBalance,
                high ? -finalLockedBalance : finalLockedBalance);
            sleLine->setFieldU32(sfLockCount, finalLockCount);
        }

        view.update(sleLine);
    }

    return tesSUCCESS;
}

/** Check if a set of accounts can freely exchange the specified token.
    Read only, does not change any ledger object.
    May be called with ApplyView or ReadView.
    (including unlocking) is forbidden by any flag or condition.
    If parties contains 1 entry then noRipple is not a bar to xfer.
    If parties contains more than 1 entry then any party with noRipple
    on issuer side is a bar to xfer.
*/
template <class V>
[[nodiscard]] TER
trustTransferAllowed(
    V& view,
    std::vector<AccountID> const& parties,
    Issue const& issue,
    beast::Journal const& j)
{
    static_assert(
        std::is_same<V, ReadView const>::value ||
        std::is_same<V, ApplyView>::value);

    typedef typename std::conditional<
        std::is_same<V, ApplyView>::value,
        std::shared_ptr<SLE>,
        std::shared_ptr<SLE const>>::type SLEPtr;

    if (isFakeXRP(issue.currency))
        return tecNO_PERMISSION;

    auto const sleIssuerAcc = view.read(keylet::account(issue.account));

    bool lockedBalanceAllowed =
        view.rules().enabled(featurePaychanAndEscrowForTokens);

    // missing issuer is always a bar to xfer
    if (!sleIssuerAcc)
        return tecNO_ISSUER;

    // issuer global freeze is always a bar to xfer
    if (isGlobalFrozen(view, issue.account))
        return tecFROZEN;

    uint32_t issuerFlags = sleIssuerAcc->getFieldU32(sfFlags);

    bool requireAuth = issuerFlags & lsfRequireAuth;

    for (AccountID const& p : parties)
    {
        if (p == issue.account)
            continue;

        auto const line =
            view.read(keylet::line(p, issue.account, issue.currency));
        if (!line)
        {
            if (requireAuth)
            {
                // the line doesn't exist, i.e. it is in default state
                // default state means the line has not been authed
                // therefore if auth is required by issuer then
                // this is now a bar to xfer
                return tecNO_AUTH;
            }

            // missing line is a line in default state, this is not
            // a general bar to xfer, however additional conditions
            // do attach to completing an xfer into a default line
            // but these are checked in trustTransferLockedBalance at
            // the point of transfer.
            continue;
        }

        // sanity check the line, insane lines are a bar to xfer
        {
            // these "strange" old lines, if they even exist anymore are
            // always a bar to xfer
            if (line->getFieldAmount(sfLowLimit).getIssuer() ==
                line->getFieldAmount(sfHighLimit).getIssuer())
                return tecINTERNAL;

            if (line->isFieldPresent(sfLockedBalance))
            {
                if (!lockedBalanceAllowed)
                {
                    JLOG(j.warn()) << "trustTransferAllowed: "
                                   << "sfLockedBalance found on line when "
                                      "amendment not enabled";
                    return tecINTERNAL;
                }

                STAmount lockedBalance = line->getFieldAmount(sfLockedBalance);
                STAmount balance = line->getFieldAmount(sfBalance);

                if (lockedBalance.getCurrency() != balance.getCurrency())
                {
                    JLOG(j.warn()) << "trustTansferAllowed: "
                                   << "lockedBalance currency did not match "
                                      "balance currency";
                    return tecINTERNAL;
                }
            }
        }

        // check the bars to xfer ... these are:
        // any TL in the set has noRipple on the issuer's side
        // any TL in the set has a freeze on the issuer's side
        // any TL in the set has RequireAuth and the TL lacks lsf*Auth
        {
            bool pHigh = p > issue.account;

            auto const flagIssuerNoRipple{
                pHigh ? lsfLowNoRipple : lsfHighNoRipple};
            auto const flagIssuerFreeze{pHigh ? lsfLowFreeze : lsfHighFreeze};
            auto const flagIssuerAuth{pHigh ? lsfLowAuth : lsfHighAuth};

            uint32_t flags = line->getFieldU32(sfFlags);

            if (flags & flagIssuerFreeze)
            {
                JLOG(j.trace()) << "trustTransferAllowed: "
                                << "parties=[" << parties << "], "
                                << "issuer: " << issue.account << " "
                                << "has freeze on party: " << p;
                return tecFROZEN;
            }

            // if called with more than one party then any party
            // that has a noripple on the issuer side of their tl
            // blocks any possible xfer
            if (parties.size() > 1 && (flags & flagIssuerNoRipple))
            {
                JLOG(j.trace()) << "trustTransferAllowed: "
                                << "parties=[" << parties << "], "
                                << "issuer: " << issue.account << " "
                                << "has noRipple on party: " << p;
                return tecPATH_DRY;
            }

            // every party involved must be on an authed trustline if
            // the issuer has specified lsfRequireAuth
            if (requireAuth && !(flags & flagIssuerAuth))
            {
                JLOG(j.trace()) << "trustTransferAllowed: "
                                << "parties=[" << parties << "], "
                                << "issuer: " << issue.account << " "
                                << "requires TL auth which "
                                << "party: " << p << " "
                                << "does not possess.";
                return tecNO_AUTH;
            }
        }
    }

    return tesSUCCESS;
}

/** Transfer a locked balance from one TL to an unlocked balance on another
    or create a line at the destination if the actingAcc has permission to.
    Used for resolving payment instruments that use locked TL balances.
*/
template <class V, class S, class R>
[[nodiscard]] TER
trustTransferLockedBalance(
    V& view,
    AccountID const& actingAccID,  // the account whose tx is actioning xfer
    S& sleSrcAcc,
    S& sleDstAcc,
    STAmount const& amount,  // issuer, currency are in this field
    int deltaLockCount,      // -1 decrement, +1 increment, 0 unchanged
    beast::Journal const& j,
    R dryRun)
{
    typedef typename std::conditional<
        std::is_same<V, ApplyView>::value && !dryRun,
        std::shared_ptr<SLE>,
        std::shared_ptr<SLE const>>::type SLEPtr;

    auto peek = [&](Keylet& k) {
        if constexpr (std::is_same<V, ApplyView>::value && !dryRun)
            return view.peek(k);
        else
            return view.read(k);
    };

    static_assert(std::is_same<V, ApplyView>::value || dryRun);

    if (!view.rules().enabled(featurePaychanAndEscrowForTokens))
        return tefINTERNAL;

    if (!sleSrcAcc || !sleDstAcc)
    {
        JLOG(j.warn()) << "trustTransferLockedBalance without sleSrc/sleDst";
        return tecINTERNAL;
    }

    if (amount <= beast::zero)
    {
        JLOG(j.warn()) << "trustTransferLockedBalance with non-positive amount";
        return tecINTERNAL;
    }

    auto issuerAccID = amount.getIssuer();
    auto currency = amount.getCurrency();
    auto srcAccID = sleSrcAcc->getAccountID(sfAccount);
    auto dstAccID = sleDstAcc->getAccountID(sfAccount);

    bool srcHigh = srcAccID > issuerAccID;
    bool dstHigh = dstAccID > issuerAccID;

    // check for freezing, auth, no ripple and TL sanity
    {
        TER result = trustTransferAllowed(
            view, {srcAccID, dstAccID}, {currency, issuerAccID}, j);

        JLOG(j.trace())
            << "trustTransferLockedBalance: trustTransferAlowed result="
            << result;
        if (!isTesSuccess(result))
            return result;
    }

    // ensure source line exists
    Keylet klSrcLine{keylet::line(srcAccID, issuerAccID, currency)};
    SLEPtr sleSrcLine = peek(klSrcLine);

    if (!sleSrcLine)
        return tecNO_LINE;

    // can't transfer a locked balance that does not exist
    if (!sleSrcLine->isFieldPresent(sfLockedBalance) ||
        !sleSrcLine->isFieldPresent(sfLockCount))
    {
        JLOG(j.trace()) << "trustTransferLockedBalance could not find "
                           "sfLockedBalance/sfLockCount on source line";
        return tecUNFUNDED_PAYMENT;
    }

    // decrement source balance
    {
        STAmount priorBalance =
            srcHigh ? -((*sleSrcLine)[sfBalance]) : (*sleSrcLine)[sfBalance];

        STAmount priorLockedBalance = srcHigh
            ? -((*sleSrcLine)[sfLockedBalance])
            : (*sleSrcLine)[sfLockedBalance];

        uint32_t priorLockCount = (*sleSrcLine)[sfLockCount];

        AccountID srcIssuerAccID =
            sleSrcLine->getFieldAmount(srcHigh ? sfLowLimit : sfHighLimit)
                .getIssuer();

        // check they have sufficient funds
        if (amount > priorLockedBalance)
        {
            JLOG(j.trace())
                << "trustTransferLockedBalance amount > lockedBalance: "
                << "amount=" << amount
                << " lockedBalance=" << priorLockedBalance;
            return tecUNFUNDED_PAYMENT;
        }

        STAmount finalBalance = priorBalance - amount;

        STAmount finalLockedBalance = priorLockedBalance - amount;

        uint32_t finalLockCount = priorLockCount + deltaLockCount;

        // check if there is significant precision loss
        if (!isAddable(priorBalance, amount) ||
            !isAddable(priorLockedBalance, amount))
            return tecPRECISION_LOSS;

        // sanity check possible overflows on the lock counter
        if ((deltaLockCount > 0 && priorLockCount > finalLockCount) ||
            (deltaLockCount < 0 && priorLockCount < finalLockCount) ||
            (deltaLockCount == 0 && priorLockCount != finalLockCount))
            return tecINTERNAL;

        // this should never happen but defensively check it here before
        // updating sle
        if (finalBalance < beast::zero || finalLockedBalance < beast::zero)
        {
            JLOG(j.warn()) << "trustTransferLockedBalance results in a "
                              "negative balance on source line";
            return tecINTERNAL;
        }

        if constexpr (!dryRun)
        {
            sleSrcLine->setFieldAmount(
                sfBalance, srcHigh ? -finalBalance : finalBalance);

            if (finalLockedBalance == beast::zero || finalLockCount == 0)
            {
                sleSrcLine->makeFieldAbsent(sfLockedBalance);
                sleSrcLine->makeFieldAbsent(sfLockCount);
            }
            else
            {
                sleSrcLine->setFieldAmount(
                    sfLockedBalance,
                    srcHigh ? -finalLockedBalance : finalLockedBalance);
                sleSrcLine->setFieldU32(sfLockCount, finalLockCount);
            }
        }
    }

    // dstLow XNOR srcLow tells us if we need to flip the balance amount
    // on the destination line
    bool flipDstAmt = !((dstHigh && srcHigh) || (!dstHigh && !srcHigh));

    // compute transfer fee, if any
    auto xferRate = transferRate(view, issuerAccID);

    // the destination will sometimes get less depending on xfer rate
    // with any difference in tokens burned
    auto dstAmt = xferRate == parityRate
        ? amount
        : multiplyRound(amount, xferRate, amount.issue(), true);

    // check for a destination line
    Keylet klDstLine = keylet::line(dstAccID, issuerAccID, currency);
    SLEPtr sleDstLine = peek(klDstLine);

    if (!sleDstLine)
    {
        // in most circumstances a missing destination line is a deal breaker
        if (actingAccID != dstAccID && srcAccID != dstAccID)
            return tecNO_PERMISSION;

        STAmount dstBalanceDrops = sleDstAcc->getFieldAmount(sfBalance);

        // no dst line exists, we might be able to create one...
        if (std::uint32_t const ownerCount = {sleDstAcc->at(sfOwnerCount)};
            dstBalanceDrops < view.fees().accountReserve(ownerCount + 1))
            return tecNO_LINE_INSUF_RESERVE;

        // yes we can... we will

        if constexpr (!dryRun)
        {
            // clang-format off
            if (TER const ter = trustCreate(
                    view,
                    !dstHigh,                       // is dest low?
                    issuerAccID,                    // source
                    dstAccID,                       // destination
                    klDstLine.key,                  // ledger index
                    sleDstAcc,                      // Account to add to
                    false,                          // authorize account
                    (sleDstAcc->getFlags() & lsfDefaultRipple) == 0,
                    false,                          // freeze trust line
                    flipDstAmt ? -dstAmt : dstAmt,  // initial balance
                    Issue(currency, dstAccID),      // limit of zero
                    0,                              // quality in
                    0,                              // quality out
                    j);                             // journal
                !isTesSuccess(ter))
            {
                return ter;
            }
        }
        // clang-format on
    }
    else
    {
        // the dst line does exist, and it would have been checked above
        // in trustTransferAllowed for NoRipple and Freeze flags

        // check the limit
        STAmount dstLimit =
            dstHigh ? (*sleDstLine)[sfHighLimit] : (*sleDstLine)[sfLowLimit];

        STAmount priorBalance =
            dstHigh ? -((*sleDstLine)[sfBalance]) : (*sleDstLine)[sfBalance];

        STAmount finalBalance = priorBalance + dstAmt;

        if (finalBalance < priorBalance)
        {
            JLOG(j.warn()) << "trustTransferLockedBalance resulted in a "
                              "lower/equal final balance on dest line";
            return tecINTERNAL;
        }

        if (finalBalance > dstLimit && actingAccID != dstAccID)
        {
            JLOG(j.trace()) << "trustTransferLockedBalance would increase dest "
                               "line above limit without permission";
            return tecPATH_DRY;
        }

        // check if there is significant precision loss
        if (!isAddable(priorBalance, dstAmt))
            return tecPRECISION_LOSS;

        if constexpr (!dryRun)
            sleDstLine->setFieldAmount(
                sfBalance, dstHigh ? -finalBalance : finalBalance);
    }

    if constexpr (!dryRun)
    {
        static_assert(std::is_same<V, ApplyView>::value);

        // check if source line ended up in default state and adjust owner count
        // if it did
        if (isTrustDefault(sleSrcAcc, sleSrcLine))
        {
            uint32_t flags = sleSrcLine->getFieldU32(sfFlags);
            LedgerSpecificFlags fReserve{
                srcHigh ? lsfHighReserve : lsfLowReserve};
            if (flags & fReserve)
            {
                sleSrcLine->setFieldU32(sfFlags, flags & ~fReserve);
                adjustOwnerCount(view, sleSrcAcc, -1, j);
                view.update(sleSrcAcc);
            }
        }

        view.update(sleSrcLine);

        // a destination line already existed and was updated
        if (sleDstLine)
            view.update(sleDstLine);
    }
    return tesSUCCESS;
}

/** Check if the account lacks required authorization.
 *   Return tecNO_AUTH or tecNO_LINE if it does
 *   and tesSUCCESS otherwise.
 */
[[nodiscard]] TER
requireAuth(ReadView const& view, Issue const& issue, AccountID const& account);
[[nodiscard]] TER
requireAuth(
    ReadView const& view,
    MPTIssue const& mptIssue,
    AccountID const& account);

/** Check if the destination account is allowed
 *  to receive MPT. Return tecNO_AUTH if it doesn't
 *  and tesSUCCESS otherwise.
 */
[[nodiscard]] TER
canTransfer(
    ReadView const& view,
    MPTIssue const& mptIssue,
    AccountID const& from,
    AccountID const& to);

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

}  // namespace ripple

#endif
