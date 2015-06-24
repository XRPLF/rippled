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

#include <ripple/protocol/STLedgerEntry.h>
#include <ripple/protocol/TER.h>
#include <ripple/core/Config.h>
#include <ripple/ledger/View.h>
#include <ripple/ledger/ViewAPIBasics.h>
#include <functional>
#include <boost/optional.hpp>

namespace ripple {

/** A view into a ledger's state items.

    The interface provides raw access for state item
    modification operations. There is no checkpointing
    or calculation of metadata.
*/
class BasicView
{
public:
    using key_type = uint256;
    using mapped_type =
        std::shared_ptr<SLE const>;

    BasicView() = default;
    BasicView(BasicView const&) = delete;
    BasicView& operator=(BasicView const&) = delete;
    virtual ~BasicView() = default;

    /** Determine if a state item exists.

        @note This can be more efficient than calling read.

        @return `true` if a SLE is associated with the
                specified key.
    */
    virtual
    bool
    exists (Keylet const& k) const = 0;

    /** Return the key of the next state item.

        This returns the key of the first state item
        whose key is greater than the specified key. If
        no such key is present, boost::none is returned.

        If `last` is engaged, returns boost::none when
        the key returned would be outside the open
        interval (key, last).
    */
    virtual
    boost::optional<uint256>
    succ (uint256 const& key, boost::optional<
        uint256> last = boost::none) const = 0;

    /** Return the state item associated with a key.

        Effects:
            If the key exists, gives the caller ownership
            of the non-modifiable corresponding SLE.

        @note While the returned SLE is `const` from the
              perspective of the caller, it can be changed
              by other callers through raw operations.

        @return `nullptr` if the key is not present or
                if the type does not match.
    */
    virtual
    std::shared_ptr<SLE const>
    read (Keylet const& k) const = 0;

    /** Unconditionally erase a state item.

        Requirements:
            key must exist
        
        Effects:
            The item associated with key is
            unconditionally removed.

        This can break invariants

        @return `true` if the key was found
    */
    virtual
    bool
    unchecked_erase (uint256 const& key) = 0;

    /** Unconditionally insert a state item.

        Requirements:
            The key must not already exist.

        Effects:
            The key is associated with the SLE.

            Ownership of the SLE is transferred
            to the view.

        This can break invariants.

        @note The key is taken from the SLE
    */
    virtual
    void
    unchecked_insert (std::shared_ptr<SLE>&& sle) = 0;

    /** Unconditionally replace a state item.

        Requirements:
            The key must exist.

        Effects:
            The key is associated with the SLE.

            Ownership of the SLE is transferred
            to the view.

        This can break invariants.

        @note The key is taken from the SLE
    */
    virtual
    void
    unchecked_replace (std::shared_ptr<SLE>&& sle) = 0;

    /** Return the parent view or nullptr.
        @note Changing views with children breaks invariants.
    */
    // VFALCO This is a bit of a hack used to walk the parent
    //        list to gain access to the underlying ledger.
    //        Its an alternative to exposing things like the
    //        previous ledger close time.
    virtual
    BasicView const*
    parent() const = 0;

    //--------------------------------------------------------------------------

    // Called to adjust returned balances
    // This is required to support PaymentView

    virtual
    STAmount
    balanceHook (AccountID const& account,
        AccountID const& issuer,
            STAmount const& amount) const
    {
        return amount;
    }
};

//------------------------------------------------------------------------------

/** A contextual view into a ledger's state items.

    This refinement of BasicView provides an interface where
    the SLE can be "checked out" for modifications and put
    back in an updated or removed state. Also added is an
    interface to provide contextual information necessary
    to calculate the results of transaction processing,
    including the metadata if the view is later applied to
    the parent (using an interface in the derived class).

    This allows the MetaView implementation to journal
    changes made to the state items in a ledger, with the
    option to apply those changes to the parent ledger
    or view or discard the changes without affecting the
    parent.

    Typical usage is to call read() for non-mutating
    operations. This can be done by calling any function that
    takes a BasicView parameter.

    For mutating operations the sequence is as follows:

        // Add a new value
        v.insert(sle);

        // Check out a value for modification
        sle = v.peek(k);

        // Indicate that changes were made
        v.update(sle)

        // Or, erase the value
        v.erase(sle)

    The invariant is that insert, update, and erase may not
    be called with any SLE which belongs to different View.
*/
class View : public BasicView
{
public:
    View() = default;
    View(BasicView const&) = delete;
    View& operator=(BasicView const&) = delete;
    virtual ~View() = default;

    /** Prepare to modify the SLE associated with key.

        Effects:
            Gives the caller ownership of the SLE associated
            with the specified key.

        The returned SLE may be used in a subsequent
        call to erase or update.

        The SLE must not be passed to any other View.

        @return `nullptr` if the key is not present
    */
    virtual
    std::shared_ptr<SLE>
    peek (Keylet const& k) = 0;

    /** Remove a peeked SLE.

        Requirements:
            `sle` was obtained from prior call to peek()
            on this instance of the View.

        Effects:
            The key is no longer associated with the SLE.
    */
    virtual
    void
    erase (std::shared_ptr<SLE> const& sle) = 0;

    /** Insert a new state SLE

        Requirements:
            `sle` was not obtained from any calls to
            peek() on any instances of View.

        Effects:
            assert if the key already exists

            The key in the state map is associated
            with the SLE.

            The View acquires ownership of the shared_ptr.

        @note The key is taken from the SLE
    */
    virtual
    void
    insert (std::shared_ptr<SLE> const& sle) = 0;

    /** Indicate changes to a peeked SLE

        Requirements:
            `sle` was obtained from prior call to peek()
            on this instance of the View.

        Effects:
            The View is notified that the SLE changed.

        @note The key is taken from the SLE
    */
    /** @{ */
    virtual
    void
    update (std::shared_ptr<SLE> const& sle) = 0;

    //--------------------------------------------------------------------------

    /** Returns `true` if the context is an open ledger.

        Open ledgers have different rules for what TER
        codes are returned when a transaction fails.
    */
    virtual
    bool
    openLedger() const = 0;

    //--------------------------------------------------------------------------

    // Called when a credit is made to an account
    // This is required to support PaymentView
    virtual
    void
    creditHook (AccountID const& from,
        AccountID const& to,
            STAmount const& amount)
    {
    }
};

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
    std::uint32_t uSrcQualityIn,
    std::uint32_t uSrcQualityOut);

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
    const STAmount & saAmount, bool bCheckIssuer);

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
