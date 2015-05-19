//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2012-2015 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_PENDINGSAVES_H_INCLUDED
#define RIPPLE_APP_PENDINGSAVES_H_INCLUDED

#include <ripple/protocol/Protocol.h>
#include <boost/container/flat_set.hpp>
#include <mutex>

namespace ripple {

/** Keeps track of which ledgers haven't been fully saved.

    During the ledger building process this collection will keep
    track of those ledgers that are being built but have not yet
    been completely written.
*/
class PendingSaves
{
private:
    std::mutex mutable mutex_;
    boost::container::flat_set<LedgerIndex> set_;

public:
    /** Add a ledger to the list.

        This is called when the ledger is built but before
        we have updated the SQLite indexes. Clients querying
        the indexes will not see results from this ledger.

        @return `true` If the ledger indexes was not
                already in the list.
    */
    bool
    insert (LedgerIndex seq)
    {
        std::lock_guard<
            std::mutex> lock(mutex_);
        return set_.insert(seq).second;
    }

    /** Remove a ledger from the list.

        This is called after the ledger has been fully saved,
        indicating that the SQLite indexes will produce correct
        results in response to client requests.
    */
    void
    erase (LedgerIndex seq)
    {
        std::lock_guard<
            std::mutex> lock(mutex_);
        set_.erase(seq);
    }

    /** Returns a copy of the current set. */
    auto
    getSnapshot() const ->
        decltype(set_)
    {
        std::lock_guard<
            std::mutex> lock(mutex_);
        return set_;
    }
};

}

#endif
