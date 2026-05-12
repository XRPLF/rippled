#include <xrpl/basics/CountedObject.h>

#include <algorithm>

namespace xrpl {

CountedObjects&
CountedObjects::getInstance() noexcept
{
    static CountedObjects kInstance;

    return kInstance;
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

    std::ranges::sort(counts);

    return counts;
}

}  // namespace xrpl
