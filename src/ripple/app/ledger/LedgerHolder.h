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

#ifndef RIPPLE_APP_LEDGER_LEDGERHOLDER_H_INCLUDED
#define RIPPLE_APP_LEDGER_LEDGERHOLDER_H_INCLUDED

#include <ripple/basics/contract.h>
#include <mutex>

namespace ripple {

// Can std::atomic<std::shared_ptr>> make this lock free?

// VFALCO NOTE This class can be replaced with atomic<shared_ptr<...>>

/** Hold a ledger in a thread-safe way.

    VFALCO TODO The constructor should require a valid ledger, this
                way the object always holds a value. We can use the
                genesis ledger in all cases.
*/
class LedgerHolder
{
public:
    // Update the held ledger
    void set (std::shared_ptr<Ledger const> ledger)
    {
        if(! ledger)
            LogicError("LedgerHolder::set with nullptr");
        if(! ledger->isImmutable())
            LogicError("LedgerHolder::set with mutable Ledger");
        std::lock_guard sl (m_lock);
        m_heldLedger = std::move(ledger);
    }

    // Return the (immutable) held ledger
    std::shared_ptr<Ledger const> get ()
    {
        std::lock_guard sl (m_lock);
        return m_heldLedger;
    }

    bool empty ()
    {
        std::lock_guard sl (m_lock);
        return m_heldLedger == nullptr;
    }

private:
    std::mutex m_lock;
    std::shared_ptr<Ledger const> m_heldLedger;
};

} // ripple

#endif
