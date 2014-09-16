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

#ifndef RIPPLE_LOCALTRANSACTIONS_H
#define RIPPLE_LOCALTRANSACTIONS_H

namespace ripple {

// Track transactions issued by local clients
// Ensure we always apply them to our open ledger
// Hold them until we see them in a fully-validated ledger

class LocalTxs
{
public:

    virtual ~LocalTxs () = default;

    static std::unique_ptr<LocalTxs> New ();

    // Add a new local transaction
    virtual void push_back (LedgerIndex index, SerializedTransaction::ref txn) = 0;

    // Apply local transactions to a new open ledger
    virtual void apply (TransactionEngine&) = 0;

    // Remove obsolete transactions based on a new fully-valid ledger
    virtual void sweep (Ledger::ref validLedger) = 0;

    virtual std::size_t size () = 0;
};

} // ripple

#endif
