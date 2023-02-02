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

#ifndef RIPPLE_APP_LEDGER_LOCALTXS_H_INCLUDED
#define RIPPLE_APP_LEDGER_LOCALTXS_H_INCLUDED

#include <ripple/app/misc/CanonicalTXSet.h>
#include <ripple/ledger/ReadView.h>
#include <memory>

namespace ripple {

/** Tracks transactions so we can apply them to the open ledger until seen in a
    fully validated ledger.

    This code prevents scenarios like the following:

    1) A client submits a transaction.
    2) The transaction gets into the ledger this server believes will be the
       consensus ledger.
    3) The server builds a succeeding open ledger without the transaction
       because it is in the prior ledger.
    4) The local consensus ledger is not the majority ledger (due to network
       conditions, Byzantine fault, etc.) and the majority ledger does not
       include the transaction.
    5) The server builds a new open ledger that does not include the transaction
       or have it in a prior ledger.
    6) The client submits another transaction and gets a terPRE_SEQ preliminary
       result.
    7) The server does not relay that second transaction, at least not yet.

    With this code, when step 5 happens, the first transaction will be applied
    to that open ledger so the second transaction will succeed normally at step
    6. Transactions remain tracked and test-applied to all new open ledgers
    until seen in a fully-validated ledger
 */

class LocalTxs
{
public:
    virtual ~LocalTxs() = default;

    /** Add a new local transaction. */
    virtual void
    track(std::shared_ptr<STTx const> const& txn, LedgerIndex index) = 0;

    /** Return the set of local transactions. */
    virtual CanonicalTXSet
    getTransactions() = 0;

    /** Remove obsolete transactions based on a new fully-valid ledger. */
    virtual void
    sweep(ReadView const& view) = 0;

    virtual std::size_t
    size() = 0;
};

std::unique_ptr<LocalTxs>
make_LocalTxs();

}  // namespace ripple

#endif
