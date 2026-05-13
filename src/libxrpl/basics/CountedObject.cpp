/** @file
 *  Implementation of the CountedObjects singleton registry.
 *
 *  CountedObjects maintains a lock-free linked list of Counter nodes — one
 *  per tracked type — whose live-instance counts are updated atomically by
 *  the CountedObject<T> CRTP mixin.  getCounts() is the only public query
 *  path; it is called exclusively from the get_counts admin RPC command and
 *  from test code, never from any ledger-critical path.
 */

#include <xrpl/basics/CountedObject.h>

#include <algorithm>

namespace xrpl {

/** Return the process-wide singleton, creating it on first call.
 *
 *  Uses the Meyer's singleton idiom: the function-local static is
 *  initialised exactly once in a thread-safe manner under C++11 and later.
 */
CountedObjects&
CountedObjects::getInstance() noexcept
{
    static CountedObjects kINSTANCE;

    return kINSTANCE;
}

/** Initialise the registry with an empty counter list. */
CountedObjects::CountedObjects() noexcept : count_(0), head_(nullptr)
{
}

/** Collect a diagnostic snapshot of live object counts.
 *
 *  Traverses the lock-free linked list of Counter nodes.  Because Counter
 *  nodes are never removed (they are static objects with program lifetime),
 *  the traversal cannot encounter a dangling pointer, but it may miss a
 *  Counter that was prepended after the walk started.  This is acceptable
 *  for a purely diagnostic facility.
 *
 *  The result is sorted alphabetically by type name so that successive
 *  snapshots can be diffed easily.
 *
 *  @param minimumThreshold Suppress entries whose live count is strictly
 *      below this value.  Pass 0 for an exhaustive view; the get_counts
 *      RPC handler defaults to 10 to reduce noise.
 *  @return A sorted list of (type-name, live-count) pairs for all tracked
 *      types whose current count meets the threshold.
 */
CountedObjects::List
CountedObjects::getCounts(int minimumThreshold) const
{
    List counts;

    // When other operations are concurrent, the count
    // might be temporarily less than the actual count.
    counts.reserve(count_.load());

    for (auto* ctr = head_.load(); ctr != nullptr; ctr = ctr->getNext())
    {
        if (ctr->getCount() >= minimumThreshold)
            counts.emplace_back(ctr->getName(), ctr->getCount());
    }

    std::ranges::sort(counts);

    return counts;
}

}  // namespace xrpl
