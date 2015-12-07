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

#ifndef RIPPLE_LEDGER_APPLYVIEW_H_INCLUDED
#define RIPPLE_LEDGER_APPLYVIEW_H_INCLUDED

#include <ripple/ledger/RawView.h>
#include <ripple/ledger/ReadView.h>

namespace ripple {

enum ApplyFlags
{
    tapNONE             = 0x00,

    // Signature already checked
    tapNO_CHECK_SIGN    = 0x01,

    // Enable supressed features for testing.
    // This lets unit tests exercise code that
    // is not turned on for production.
    //
    tapENABLE_TESTING   = 0x02,

    // We expect the transaction to have a later
    // sequence number than the account in the ledger
    tapPOST_SEQ         = 0x04,

    // This is not the transaction's last pass
    // Transaction can be retried, soft failures allowed
    tapRETRY            = 0x20,

    // Transaction came from a privileged source
    tapUNLIMITED            = 0x400,
};

inline
ApplyFlags
operator|(ApplyFlags const& lhs,
    ApplyFlags const& rhs)
{
    return static_cast<ApplyFlags>(
        static_cast<int>(lhs) |
            static_cast<int>(rhs));
}

inline
ApplyFlags
operator&(ApplyFlags const& lhs,
    ApplyFlags const& rhs)
{
    return static_cast<ApplyFlags>(
        static_cast<int>(lhs) &
            static_cast<int>(rhs));
}

//------------------------------------------------------------------------------

/** Writeable view to a ledger, for applying a transaction.

    This refinement of ReadView provides an interface where
    the SLE can be "checked out" for modifications and put
    back in an updated or removed state. Also added is an
    interface to provide contextual information necessary
    to calculate the results of transaction processing,
    including the metadata if the view is later applied to
    the parent (using an interface in the derived class).
    The context info also includes values from the base
    ledger such as sequence number and the network time.

    This allows implementations to journal changes made to
    the state items in a ledger, with the option to apply
    those changes to the base or discard the changes without
    affecting the base.

    Typical usage is to call read() for non-mutating
    operations.

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
    be called with any SLE which belongs to different view.
*/
class ApplyView
    : public ReadView
{
public:

    ApplyView () = default;

    /** Returns the tx apply flags.

        Flags can affect the outcome of transaction
        processing. For example, transactions applied
        to an open ledger generate "local" failures,
        while transactions applied to the consensus
        ledger produce hard failures (and claim a fee).
    */
    virtual
    ApplyFlags
    flags() const = 0;

    /** Prepare to modify the SLE associated with key.

        Effects:

            Gives the caller ownership of a modifiable
            SLE associated with the specified key.

        The returned SLE may be used in a subsequent
        call to erase or update.

        The SLE must not be passed to any other ApplyView.

        @return `nullptr` if the key is not present
    */
    virtual
    std::shared_ptr<SLE>
    peek (Keylet const& k) = 0;

    /** Remove a peeked SLE.

        Requirements:

            `sle` was obtained from prior call to peek()
            on this instance of the RawView.

        Effects:

            The key is no longer associated with the SLE.
    */
    virtual
    void
    erase (std::shared_ptr<SLE> const& sle) = 0;

    /** Insert a new state SLE

        Requirements:

            `sle` was not obtained from any calls to
            peek() on any instances of RawView.

            The SLE's key must not already exist.

        Effects:

            The key in the state map is associated
            with the SLE.

            The RawView acquires ownership of the shared_ptr.

        @note The key is taken from the SLE
    */
    virtual
    void
    insert (std::shared_ptr<SLE> const& sle) = 0;

    /** Indicate changes to a peeked SLE

        Requirements:

            The SLE's key must exist.

            `sle` was obtained from prior call to peek()
            on this instance of the RawView.

        Effects:

            The SLE is updated

        @note The key is taken from the SLE
    */
    /** @{ */
    virtual
    void
    update (std::shared_ptr<SLE> const& sle) = 0;

    //--------------------------------------------------------------------------

    // Called when a credit is made to an account
    // This is required to support PaymentSandbox
    virtual
    void
    creditHook (AccountID const& from,
        AccountID const& to,
            STAmount const& amount)
    {
    }
};

} // ripple

#endif
