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

#ifndef RIPPLE_PEERFINDER_HANDOUT_H_INCLUDED
#define RIPPLE_PEERFINDER_HANDOUT_H_INCLUDED

namespace ripple {
namespace PeerFinder {

namespace detail {

/** Tries to insert one object in the target.
    When an item is handed out it is moved to the end of the container.
    @return The number of objects handed out
*/
// VFALCO TODO specialization that handles std::list for SequenceContainer
//             using splice for optimization over erase/push_back
//
template <class Target, class HopContainer>
std::size_t handout_one (Target& t, HopContainer& h)
{
    assert (! t.full());
    for (auto hi (h.begin()); hi != h.end(); ++hi)
    {
        auto const& e (*hi);
        if (t.try_insert (e))
        {
            h.move_back (hi);
            return 1;
        }
    }
    return 0;
}

}

/** Distributes objects to targets according to business rules.
    A best effort is made to evenly distribute items in the sequence
    container list into the target sequence list.
*/
template <class TargetFwdIter, class SeqFwdIter>
void handout (TargetFwdIter first, TargetFwdIter last,
    SeqFwdIter seq_first, SeqFwdIter seq_last)
{
    for (;;)
    {
        std::size_t n (0);
        for (auto si (seq_first); si != seq_last; ++si)
        {
            auto c (*si);
            bool all_full (true);
            for (auto ti (first); ti != last; ++ti)
            {
                auto& t (*ti);
                if (! t.full())
                {
                    n += detail::handout_one (t, c);
                    all_full = false;
                }
            }
            if (all_full)
                return;
        }
        if (! n)
            break;
    }
}

}
}

#endif
