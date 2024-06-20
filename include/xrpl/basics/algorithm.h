//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

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

#ifndef RIPPLE_ALGORITHM_H_INCLUDED
#define RIPPLE_ALGORITHM_H_INCLUDED

#include <iterator>
#include <utility>

namespace ripple {

// Requires: [first1, last1) and [first2, last2) are ordered ranges according to
// comp.

// Effects: For each pair of elements {i, j} in the intersection of the sorted
// sequences [first1, last1) and [first2, last2), perform action(i, j).

// Note: This algorithm is evolved from std::set_intersection.
template <class InputIter1, class InputIter2, class Action, class Comp>
void
generalized_set_intersection(
    InputIter1 first1,
    InputIter1 last1,
    InputIter2 first2,
    InputIter2 last2,
    Action action,
    Comp comp)
{
    while (first1 != last1 && first2 != last2)
    {
        if (comp(*first1, *first2))  // if *first1 < *first2
            ++first1;                //     then reduce first range
        else
        {
            if (!comp(*first2, *first1))   // if *first1 == *first2
            {                              //     then this is an intersection
                action(*first1, *first2);  //     do the action
                ++first1;                  //     reduce first range
            }
            ++first2;  // Reduce second range because *first2 <= *first1
        }
    }
}

// Requires: [first1, last1) and [first2, last2) are ordered ranges according to
// comp.

// Effects: Eliminates all the elements i in the range [first1, last1) which are
// equivalent to some value in [first2, last2) or for which pred(i) returns
// true.

// Returns: A FwdIter1 E such that [first1, E) is the range of elements not
// removed by this algorithm.

// Note: This algorithm is evolved from std::remove_if and
// std::set_intersection.
template <class FwdIter1, class InputIter2, class Pred, class Comp>
FwdIter1
remove_if_intersect_or_match(
    FwdIter1 first1,
    FwdIter1 last1,
    InputIter2 first2,
    InputIter2 last2,
    Pred pred,
    Comp comp)
{
    // [original-first1, current-first1) is the set of elements to be preserved.
    // [current-first1, i) is the set of elements that have been removed.
    // [i, last1) is the set of elements not tested yet.

    // Test each *i in [first1, last1) against [first2, last2) and pred
    for (auto i = first1; i != last1;)
    {
        // if (*i is not in [first2, last2)
        if (first2 == last2 || comp(*i, *first2))
        {
            if (!pred(*i))
            {
                // *i should not be removed, so append it to the preserved set
                if (i != first1)
                    *first1 = std::move(*i);
                ++first1;
            }
            // *i has been fully tested, prepare for next i by
            // appending i to the removed set, whether or not
            // it has been moved from above.
            ++i;
        }
        else  // *i might be in [first2, last2) because *i >= *first2
        {
            if (!comp(*first2, *i))  // if *i == *first2
                ++i;                 //    then append *i to the removed set
            // All elements in [i, last1) are known to be greater than *first2,
            // so reduce the second range.
            ++first2;
        }
        // Continue to test *i against [first2, last2) and pred
    }
    return first1;
}

}  // namespace ripple

#endif
