/** @file
 *  In-place union helper for `boost::container::flat_set`.
 *
 *  The payment-path engine accumulates sets of offer IDs that must be
 *  removed from the ledger (consumed, expired, or unfunded offers found
 *  during flow computation). Each strand and book step produces its own
 *  local removal set; `setUnion` merges those sets into a single
 *  accumulator so that cleanup can happen atomically after the flow
 *  completes, regardless of whether the overall payment succeeded.
 */

#pragma once

#include <boost/container/flat_set.hpp>

namespace xrpl {

/** Merge @p src into @p dst in place, computing `dst = dst ∪ src`.
 *
 *  Three optimizations make this efficient on the hot payment-flow path:
 *
 *  1. **Early exit** — returns immediately when @p src is empty, avoiding any
 *     allocation.  This is the common case when a strand traverses a path
 *     that encounters no bad offers.
 *
 *  2. **Single pre-allocation** — `dst.reserve(dst.size() + src.size())`
 *     reserves enough capacity for the worst case (zero overlap) before any
 *     insertions, preventing repeated reallocation of the underlying
 *     contiguous array.
 *
 *  3. **Merge-style insert** — passing `ordered_unique_range_t{}` tells
 *     `flat_set` that @p src is already sorted and deduplicated (guaranteed
 *     because @p src is itself a `flat_set`).  Boost performs an
 *     `inplace_merge`-style operation — O(n + m) — rather than inserting
 *     elements one at a time at O(n log n).
 *
 *  In practice @p T is always `uint256` (offer ledger keys), but the
 *  function is generic over any element type that `flat_set` supports.
 *
 *  @param dst Accumulator set; receives the union result in place.
 *  @param src Set of elements to fold into @p dst; left unchanged.
 */
template <class T>
void
setUnion(boost::container::flat_set<T>& dst, boost::container::flat_set<T> const& src)
{
    if (src.empty())
        return;

    dst.reserve(dst.size() + src.size());
    dst.insert(boost::container::ordered_unique_range_t{}, src.begin(), src.end());
}

}  // namespace xrpl
