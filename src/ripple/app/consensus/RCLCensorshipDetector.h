//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_CONSENSUS_RCLCENSORSHIPDETECTOR_H_INCLUDED
#define RIPPLE_APP_CONSENSUS_RCLCENSORSHIPDETECTOR_H_INCLUDED

#include <ripple/shamap/SHAMap.h>
#include <algorithm>
#include <map>

namespace ripple {

template <class TxID, class Sequence>
class RCLCensorshipDetector
{
private:
    std::map<TxID, Sequence> tracker_;

    /** Removes all elements satisfying specific criteria from the tracker

        @param pred A predicate which returns true for tracking entries that
                    should be removed. The predicate must be callable as:
                        bool pred(TxID const&, Sequence)
                    It must return true for entries that should be removed.

        @note This could be replaced with std::erase_if when it becomes part
              of the standard. For example:

                prune ([](TxID const& id, Sequence seq)
                    {
                        return id.isZero() || seq == 314159;
                    });

              would become:

                std::erase_if(tracker_.begin(), tracker_.end(),
                    [](auto const& e)
                    {
                        return e.first.isZero() || e.second == 314159;
                    }
    */
    template <class Predicate>
    void prune(Predicate&& pred)
    {
        auto t = tracker_.begin();

        while (t != tracker_.end())
        {
            if (pred(t->first, t->second))
                t = tracker_.erase(t);
            else
                t = std::next(t);
        }
    }

public:
    RCLCensorshipDetector() = default;

    /** Add transactions being proposed for the current consensus round.

        @param seq      The sequence number of the ledger being built.
        @param proposed The set of transactions that we are initially proposing
                        for this round.
    */
    void propose(
        Sequence seq,
        std::vector<TxID> proposed)
    {
        std::sort (proposed.begin(), proposed.end());

        // We want to remove any entries that we proposed in a previous round
        // that did not make it in yet if we are no longer proposing them.
        prune ([&proposed](TxID const& id, Sequence seq)
            {
                return !std::binary_search(proposed.begin(), proposed.end(), id);
            });

        // Track the entries that we are proposing in this round.
        for (auto const& p : proposed)
            tracker_.emplace(p, seq); // FIXME C++17: use try_emplace
    }

    /** Determine which transactions made it and perform censorship detection.

        This function is called when the server is proposing and a consensus
        round it participated in completed.

        @param accepted The set of transactions that the network agreed
                        should be included in the ledger being built.
        @param pred     A predicate invoked for every transaction we've proposed
                        but which hasn't yet made it. The predicate must be
                        callable as:
                            bool pred(TxID const&, Sequence)
                        It must return true for entries that should be removed.
    */
    template <class Predicate>
    void check(
        std::vector<TxID> accepted,
        Predicate&& pred)
    {
        std::sort (accepted.begin(), accepted.end());

        // We want to remove all tracking entries for transactions that were
        // accepted as well as those which match the predicate.
        prune ([&pred, &accepted](TxID const& id, Sequence seq)
            {
                if (std::binary_search(accepted.begin(), accepted.end(), id))
                    return true;

                return pred(id, seq);
            });
    }

    /** Removes all elements from the tracker

        Typically, this function might be called after we reconnect to the
        network following an outage, or after we start tracking the network.
    */
    void reset()
    {
        tracker_.clear();
    }
};

}

#endif
