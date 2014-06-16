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

namespace ripple {
namespace Validators {

/** Tracks statistics on a validator. */
class Validator
{
private:
    /** State of a ledger. */
    struct Ledger
    {
        Ledger() : closed (false), received (false)
            { }

        bool closed;    // `true` if the ledger was closed
        bool received;  // `true` if we got a validation
    };

    /** Number of sources that reference this validator. */
    int m_refCount;

    /** Holds the state of all recent ledgers for this validator. */
    /** @{ */
    typedef CycledMap <RippleLedgerHash, Ledger, Count,
        beast::hardened_hash<RippleLedgerHash>,
        RippleLedgerHash::key_equal> LedgerMap;
    LedgerMap m_ledgers;
    /** @} */

public:
    Validator ()
        : m_refCount (0)
        , m_ledgers (ledgersPerValidator)
    {
    }

    /** Increment the number of references to this validator. */
    void addRef ()
        { ++m_refCount; }

    /** Decrement the number of references to this validator.
        When the reference count reaches zero, the validator will
        be removed and no longer tracked.
    */
    bool release ()
        { return (--m_refCount) == 0; }

    /** Returns the composite performance statistics. */
    Count count () const
        { return m_ledgers.front() + m_ledgers.back(); }

    /** Called upon receipt of a validation. */
    void receiveValidation (RippleLedgerHash const& ledgerHash)
    {
        std::pair <Ledger&, Count&> result (m_ledgers.insert (
            std::make_pair (ledgerHash, Ledger())));
        Ledger& ledger (result.first);
        Count& count (result.second);
        ledger.received = true;
        if (ledger.closed)
        {
            --count.expected;
            ++count.closed;
        }
        else
        {
            ++count.received;
        }
    }

    /** Called when a ledger is closed. */
    void ledgerClosed (RippleLedgerHash const& ledgerHash)
    {
        std::pair <Ledger&, Count&> result (m_ledgers.insert (
            std::make_pair (ledgerHash, Ledger())));
        Ledger& ledger (result.first);
        Count& count (result.second);
        ledger.closed = true;
        if (ledger.received)
        {
            --count.received;
            ++count.closed;
        }
        else
        {
            ++count.expected;
        }
    }
};

}
}

#endif
