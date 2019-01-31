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

#include <ripple/basics/algorithm.h>
#include <ripple/shamap/SHAMap.h>
#include <algorithm>
#include <utility>
#include <vector>

namespace ripple {

template <class TxID, class Sequence>
class RCLCensorshipDetector
{
public:
    struct TxIDSeq
    {
        TxID txid;
        Sequence seq;

        TxIDSeq (TxID const& txid_, Sequence const& seq_)
        : txid (txid_)
        , seq (seq_)
        { }
    };

    friend bool operator< (TxIDSeq const& lhs, TxIDSeq const& rhs)
    {
        if (lhs.txid != rhs.txid)
            return lhs.txid < rhs.txid;
        return lhs.seq < rhs.seq;
    }

    friend bool operator< (TxIDSeq const& lhs, TxID const& rhs)
    {
        return lhs.txid < rhs;
    }

    friend bool operator< (TxID const& lhs, TxIDSeq const& rhs)
    {
        return lhs < rhs.txid;
    }

    using TxIDSeqVec = std::vector<TxIDSeq>;

private:
    TxIDSeqVec tracker_;

public:
    RCLCensorshipDetector() = default;

    /** Add transactions being proposed for the current consensus round.

        @param proposed The set of transactions that we are initially proposing
                        for this round.
    */
    void propose(TxIDSeqVec proposed)
    {
        // We want to remove any entries that we proposed in a previous round
        // that did not make it in yet if we are no longer proposing them.
        // And we also want to preserve the Sequence of entries that we proposed
        // in the last round and want to propose again.
        std::sort(proposed.begin(), proposed.end());
        generalized_set_intersection(proposed.begin(), proposed.end(),
            tracker_.cbegin(), tracker_.cend(),
            [](auto& x, auto const& y) {x.seq = y.seq;},
            [](auto const& x, auto const& y) {return x.txid < y.txid;});
        tracker_ = std::move(proposed);
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
        auto acceptTxid = accepted.begin();
        auto const ae = accepted.end();
        std::sort(acceptTxid, ae);

        // We want to remove all tracking entries for transactions that were
        // accepted as well as those which match the predicate.

        auto i = remove_if_intersect_or_match(tracker_.begin(), tracker_.end(),
                     accepted.begin(), accepted.end(),
                     [&pred](auto const& x) {return pred(x.txid, x.seq);},
                     std::less<void>{});
        tracker_.erase(i, tracker_.end());
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
