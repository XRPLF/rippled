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

    // Unfortunately necessary for DeferredCredits
    virtual
    STAmount
    deprecatedBalance (AccountID const& account,
        AccountID const& issuer, STAmount const& amount) const
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

    // Unfortunately necessary for DeferredCredits
    virtual
    void
    deprecatedCreditHint (AccountID const& from,
        AccountID const& to, STAmount const& amount)
    {
    }
};

} // ripple

#endif
