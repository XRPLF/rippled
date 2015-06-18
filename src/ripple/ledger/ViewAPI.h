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

#ifndef RIPPLE_LEDGER_VIEWAPI_H_INCLUDED
#define RIPPLE_LEDGER_VIEWAPI_H_INCLUDED

#include <ripple/ledger/View.h>
#include <ripple/ledger/ViewAPIBasics.h>
#include <ripple/core/Config.h>
#include <ripple/protocol/TER.h>
#include <ripple/protocol/types.h>

namespace ripple {

/*
    C++ API for reading and writing information in a View.
*/

//------------------------------------------------------------------------------
//
// Observers
//
//------------------------------------------------------------------------------

/** Reflects the fee settings for a particular ledger. */
class Fees
{
private:
    std::uint64_t base_;        // Reference tx cost (drops)
    std::uint32_t units_;       // Reference fee units
    std::uint32_t reserve_;     // Reserve base (drops)
    std::uint32_t increment_;   // Reserve increment (drops)

public:
    Fees (BasicView const& view,
        Config const& config);

    /** Returns the account reserve given the owner count, in drops.

        The reserve is calculated as the reserve base plus
        the reserve increment times the number of increments.
    */
    std::uint64_t
    reserve (std::size_t ownerCount) const
    {
        return reserve_ + ownerCount * increment_;
    }
};

bool
isGlobalFrozen (BasicView const& view,
    AccountID const& issuer);

// Returns the amount an account can spend without going into debt.
//
// <-- saAmount: amount of currency held by account. May be negative.
STAmount
accountHolds (BasicView const& view,
    AccountID const& account, Currency const& currency,
        AccountID const& issuer, FreezeHandling zeroIfFrozen,
            Config const& config);

STAmount
accountFunds (BasicView const& view, AccountID const& id,
    STAmount const& saDefault, FreezeHandling freezeHandling,
        Config const& config);

/** Iterate all items in an account's owner directory. */
void
forEachItem (BasicView const& view, AccountID const& id,
    std::function<void (std::shared_ptr<SLE const> const&)> f);

/** Iterate all items after an item in an owner directory.
    @param after The key of the item to start after
    @param hint The directory page containing `after`
    @param limit The maximum number of items to return
    @return `false` if the iteration failed
*/
bool
forEachItemAfter (BasicView const& view, AccountID const& id,
    uint256 const& after, std::uint64_t const hint,
        unsigned int limit, std::function<
            bool (std::shared_ptr<SLE const> const&)> f);

std::uint32_t
rippleTransferRate (BasicView const& view,
    AccountID const& issuer);

std::uint32_t
rippleTransferRate (BasicView const& view,
    AccountID const& uSenderID,
        AccountID const& uReceiverID,
            AccountID const& issuer);

/** Returns `true` if the directory is empty
    @param key The key of the directory
*/
bool
dirIsEmpty (BasicView const& view,
    Keylet const& k);

// Return the first entry and advance uDirEntry.
// <-- true, if had a next entry.
// VFALCO Fix these clumsy routines with an iterator
bool
cdirFirst (BasicView const& view,
    uint256 const& uRootIndex,  // --> Root of directory.
    std::shared_ptr<SLE const>& sleNode,      // <-> current node
    unsigned int& uDirEntry,    // <-- next entry
    uint256& uEntryIndex);      // <-- The entry, if available. Otherwise, zero.

// Return the current entry and advance uDirEntry.
// <-- true, if had a next entry.
// VFALCO Fix these clumsy routines with an iterator
bool
cdirNext (BasicView const& view,
    uint256 const& uRootIndex,  // --> Root of directory
    std::shared_ptr<SLE const>& sleNode,      // <-> current node
    unsigned int& uDirEntry,    // <-> next entry
    uint256& uEntryIndex);      // <-- The entry, if available. Otherwise, zero.

//------------------------------------------------------------------------------
//
// Modifiers
//
//------------------------------------------------------------------------------

/** Adjust the owner count up or down. */
void
adjustOwnerCount (View& view,
    std::shared_ptr<SLE> const& sle,
        int amount);

// Return the first entry and advance uDirEntry.
// <-- true, if had a next entry.
// VFALCO Fix these clumsy routines with an iterator
bool
dirFirst (View& view,
    uint256 const& uRootIndex,  // --> Root of directory.
    std::shared_ptr<SLE>& sleNode,      // <-> current node
    unsigned int& uDirEntry,    // <-- next entry
    uint256& uEntryIndex);      // <-- The entry, if available. Otherwise, zero.

// Return the current entry and advance uDirEntry.
// <-- true, if had a next entry.
// VFALCO Fix these clumsy routines with an iterator
bool
dirNext (View& view,
    uint256 const& uRootIndex,  // --> Root of directory
    std::shared_ptr<SLE>& sleNode,      // <-> current node
    unsigned int& uDirEntry,    // <-> next entry
    uint256& uEntryIndex);      // <-- The entry, if available. Otherwise, zero.

// <--     uNodeDir: For deletion, present to make dirDelete efficient.
// -->   uRootIndex: The index of the base of the directory.  Nodes are based off of this.
// --> uLedgerIndex: Value to add to directory.
// Only append. This allow for things that watch append only structure to just monitor from the last node on ward.
// Within a node with no deletions order of elements is sequential.  Otherwise, order of elements is random.
TER
dirAdd (View& view,
    std::uint64_t&                      uNodeDir,      // Node of entry.
    uint256 const&                      uRootIndex,
    uint256 const&                      uLedgerIndex,
    std::function<void (SLE::ref, bool)> fDescriber);

TER
dirDelete (View& view,
    const bool           bKeepRoot,
    const std::uint64_t& uNodeDir,      // Node item is mentioned in.
    uint256 const&       uRootIndex,
    uint256 const&       uLedgerIndex,  // Item being deleted
    const bool           bStable,
    const bool           bSoft);

// VFALCO NOTE Both STAmount parameters should just
//             be "Amount", a unit-less number.
//
/** Create a trust line

    This can set an initial balance.
*/
TER
trustCreate (View& view,
    const bool      bSrcHigh,
    AccountID const&  uSrcAccountID,
    AccountID const&  uDstAccountID,
    uint256 const&  uIndex,             // --> ripple state entry
    SLE::ref        sleAccount,         // --> the account being set.
    const bool      bAuth,              // --> authorize account.
    const bool      bNoRipple,          // --> others cannot ripple through
    const bool      bFreeze,            // --> funds cannot leave
    STAmount const& saBalance,          // --> balance of account being set.
                                        // Issuer should be noAccount()
    STAmount const& saLimit,            // --> limit for account being set.
                                        // Issuer should be the account being set.
    const std::uint32_t uSrcQualityIn = 0,
    const std::uint32_t uSrcQualityOut = 0);

TER
trustDelete (View& view,
    std::shared_ptr<SLE> const& sleRippleState,
        AccountID const& uLowAccountID,
            AccountID const& uHighAccountID);

/** Delete an offer.

    Requirements:
        The passed `sle` be obtained from a prior
        call to view.peek()
*/
TER
offerDelete (View& view,
    std::shared_ptr<SLE> const& sle);

//------------------------------------------------------------------------------

//
// Money Transfers
//

// Direct send w/o fees:
// - Redeeming IOUs and/or sending sender's own IOUs.
// - Create trust line of needed.
// --> bCheckIssuer : normally require issuer to be involved.
TER
rippleCredit (View& view,
    AccountID const& uSenderID, AccountID const& uReceiverID,
    const STAmount & saAmount, bool bCheckIssuer = true);

TER
accountSend (View& view,
    AccountID const& from,
        AccountID const& to,
            const STAmount & saAmount);

TER 
issueIOU (View& view,
    AccountID const& account,
        STAmount const& amount,
            Issue const& issue);

TER
redeemIOU (View& view,
    AccountID const& account,
        STAmount const& amount,
            Issue const& issue);

TER
transferXRP (View& view,
    AccountID const& from,
        AccountID const& to,
            STAmount const& amount);

} // ripple

#endif
