#include <xrpl/basics/CountedObject.h>

#include <algorithm>

namespace ripple {

CountedObjects&
CountedObjects::getInstance() noexcept
{
    static CountedObjects instance;

    return instance;
}

CountedObjects::CountedObjects() noexcept : m_count(0), m_head(nullptr)
{
}

CountedObjects::List
CountedObjects::getCounts(int minimumThreshold) const
{
    List counts;

    // When other operations are concurrent, the count
    // might be temporarily less than the actual count.
    counts.reserve(m_count.load());

    for (auto* ctr = m_head.load(); ctr != nullptr; ctr = ctr->getNext())
    {
        if (ctr->getCount() >= minimumThreshold)
            counts.emplace_back(ctr->getName(), ctr->getCount());
    }

    std::sort(counts.begin(), counts.end());

    return counts;
}

}  // namespace ripple
