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

#ifndef RIPPLE_VALIDATORS_VALIDATOR_H_INCLUDED
#define RIPPLE_VALIDATORS_VALIDATOR_H_INCLUDED

#include <ripple/common/seconds_clock.h> // VFALCO Doesn't belong here
#include <ripple/validators/impl/Count.h>
#include <beast/container/aged_unordered_map.h>
#include <beast/container/aged_map.h>
#include <beast/container/aged_container_utility.h>

namespace ripple {
namespace Validators {

/** Tracks statistics on a validator. */
class Validator
{
private:
    // State of a ledger.
    struct Entry
    {
        bool closed = false;    // `true` if the ledger was closed
        bool received = false;  // `true` if we got a validation
    };

    // Holds the Entry of all recent ledgers for this validator.
#if 1
    typedef beast::aged_unordered_map <RippleLedgerHash, Entry,
        std::chrono::seconds, beast::hardened_hash<>,
            RippleLedgerHash::key_equal> Table;
#else
    typedef beast::aged_map <RippleLedgerHash, Entry,
        std::chrono::seconds, std::less<>> Table;
#endif

    int refs_;      // Number of sources that reference this validator.
    Table table_;
    Count count_;

public:
    Validator()
        : refs_ (0)
        , table_ (get_seconds_clock ())
    {
    }

    /** Increment the number of references to this validator. */
    void
    addRef()
    {
        ++refs_;
    }

    /** Decrement the number of references to this validator.
        When the reference count reaches zero, the validator will
        be removed and no longer tracked.
    */
    bool
    release()
    {
        return (--refs_) == 0;
    }

    size_t
    size () const
    {
        return table_.size ();
    }

    /** Returns the composite performance statistics. */
    Count const&
    count () const
    {
        return count_;
    }

    /** Called upon receipt of a validation. */
    void
    on_validation (RippleLedgerHash const& ledgerHash)
    {
        //expire();
        auto const result (table_.insert (
            std::make_pair (ledgerHash, Entry())));
        auto& entry (result.first->second);
        if (entry.received)
            return;
        entry.received = true;
        if (entry.closed)
        {
            --count_.expected;
            ++count_.closed;
            table_.erase (result.first);
        }
        else
        {
            ++count_.received;
        }
    }

    /** Called when a ledger is closed. */
    void
    on_ledger (RippleLedgerHash const& ledgerHash)
    {
        //expire();
        auto const result (table_.insert (
            std::make_pair (ledgerHash, Entry())));
        auto& entry (result.first->second);
        if (entry.closed)
            return;
        entry.closed = true;
        if (entry.received)
        {
            --count_.received;
            ++count_.closed;
            table_.erase (result.first);
        }
        else
        {
            ++count_.expected;
        }
    }

    /** Prunes old entries. */
    void
    expire()
    {
        beast::expire (table_, std::chrono::minutes(5));
    }
};

}
}

#endif
