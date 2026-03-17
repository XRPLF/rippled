#include <xrpl/basics/CountedObject.h>

#include <algorithm>

namespace xrpl {

CountedObjects&
CountedObjects::getInstance() noexcept
{
    static CountedObjects instance;

    return instance;
}

CountedObjects::CountedObjects() noexcept : count_(0), head_(nullptr)
{
}

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

    std::sort(counts.begin(), counts.end());

    return counts;
}

}  // namespace xrpl
