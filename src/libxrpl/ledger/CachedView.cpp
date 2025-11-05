#include <xrpl/basics/TaggedCache.ipp>
#include <xrpl/ledger/CachedView.h>

namespace ripple {
namespace detail {

bool
CachedViewImpl::exists(Keylet const& k) const
{
    return read(k) != nullptr;
}

std::shared_ptr<SLE const>
CachedViewImpl::read(Keylet const& k) const
{
    static CountedObjects::Counter hits{"CachedView::hit"};
    static CountedObjects::Counter hitsexpired{"CachedView::hitExpired"};
    static CountedObjects::Counter misses{"CachedView::miss"};
    bool cacheHit = false;
    bool baseRead = false;

    auto const digest = [&]() -> std::optional<uint256> {
        {
            std::lock_guard lock(mutex_);
            auto const iter = map_.find(k.key);
            if (iter != map_.end())
            {
                cacheHit = true;
                return iter->second;
            }
        }
        return base_.digest(k.key);
    }();
    if (!digest)
        return nullptr;
    auto sle = cache_.fetch(*digest, [&]() {
        baseRead = true;
        return base_.read(k);
    });
    // If the sle is null, then a failure must have occurred in base_.read()
    XRPL_ASSERT(
        sle || baseRead,
        "ripple::CachedView::read : null SLE result from base");
    if (cacheHit && baseRead)
        hitsexpired.increment();
    else if (cacheHit)
        hits.increment();
    else
        misses.increment();

    if (!cacheHit)
    {
        // Avoid acquiring this lock unless necessary. It is only necessary if
        // the key was not found in the map_. The lock is needed to add the key
        // and digest.
        std::lock_guard lock(mutex_);
        map_.emplace(k.key, *digest);
    }
    if (!sle || !k.check(*sle))
        return nullptr;
    return sle;
}

}  // namespace detail
}  // namespace ripple
