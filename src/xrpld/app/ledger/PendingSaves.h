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
#include <condition_variable>
#include <map>
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
    std::map<LedgerIndex, bool> map_;
    std::condition_variable await_;

public:
    /** Start working on a ledger

        This is called prior to updating the SQLite indexes.

        @return 'true' if work should be done
    */
    bool
    startWork(LedgerIndex seq)
    {
        std::lock_guard lock(mutex_);

        auto it = map_.find(seq);

        if ((it == map_.end()) || it->second)
        {
            // Work done or another thread is doing it
            return false;
        }

        it->second = true;
        return true;
    }

    /** Finish working on a ledger

        This is called after updating the SQLite indexes.
        The tracking of the work in progress is removed and
        threads awaiting completion are notified.
    */
    void
    finishWork(LedgerIndex seq)
    {
        std::lock_guard lock(mutex_);

        map_.erase(seq);
        await_.notify_all();
    }

    /** Return `true` if a ledger is in the progress of being saved. */
    bool
    pending(LedgerIndex seq)
    {
        std::lock_guard lock(mutex_);
        return map_.find(seq) != map_.end();
    }

    /** Check if a ledger should be dispatched

        Called to determine whether work should be done or
        dispatched. If work is already in progress and the
        call is synchronous, wait for work to be completed.

        @return 'true' if work should be done or dispatched
    */
    bool
    shouldWork(LedgerIndex seq, bool isSynchronous)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        do
        {
            auto it = map_.find(seq);

            if (it == map_.end())
            {
                map_.emplace(seq, false);
                return true;
            }

            if (!isSynchronous)
            {
                // Already dispatched
                return false;
            }

            if (!it->second)
            {
                // Scheduled, but not dispatched
                return true;
            }

            // Already in progress, just need to wait
            await_.wait(lock);

        } while (true);
    }

    /** Get a snapshot of the pending saves

        Each entry in the returned map corresponds to a ledger
        that is in progress or dispatched. The boolean indicates
        whether work is currently in progress.
    */
    std::map<LedgerIndex, bool>
    getSnapshot() const
    {
        std::lock_guard lock(mutex_);

        return map_;
    }
};

}  // namespace ripple

#endif
