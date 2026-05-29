#include <xrpl/ledger/TopOfBookCache.h>

#include <xrpl/protocol/Indexes.h>

#include <atomic>
#include <mutex>

namespace xrpl {

namespace {

// Operator-facing kill switch. Defaults to true; set false via setEnabled()
// to bypass the cache entirely (e.g. in case a workload exposes a bug, the
// node can be brought back to baseline succ() behavior without restart).
std::atomic<bool> gEnabled{true};

}  // namespace

bool
TopOfBookCache::enabled() noexcept
{
    return gEnabled.load(std::memory_order_relaxed);
}

void
TopOfBookCache::setEnabled(bool on) noexcept
{
    gEnabled.store(on, std::memory_order_relaxed);
}

TopOfBookCache::TopOfBookCache(TopOfBookCache const& other)
{
    std::lock_guard lock(other.mutex_);
    map_ = other.map_;
}

TopOfBookCache::TopOfBookCache(TopOfBookCache&& other)
{
    std::lock_guard lock(other.mutex_);
    map_ = std::move(other.map_);
}

std::optional<TopOfBookEntry>
TopOfBookCache::get(Book const& book) const
{
    std::lock_guard lock(mutex_);
    auto it = map_.find(book);
    if (it == map_.end())
    {
        misses_.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }
    hits_.fetch_add(1, std::memory_order_relaxed);
    return it->second;
}

void
TopOfBookCache::record(Book const& book, uint256 const& firstPageKey, LedgerIndex seq)
{
    std::lock_guard lock(mutex_);
    auto& entry = map_[book];
    entry.firstPageKey = firstPageKey;
    entry.bestQuality = getQuality(firstPageKey);
    entry.asOfLedger = seq;
}

void
TopOfBookCache::onOfferInsert(Book const& book, uint256 const& dirKey, LedgerIndex seq)
{
    std::lock_guard lock(mutex_);
    auto it = map_.find(book);
    if (it == map_.end())
    {
        // No cached top — defer to the next reader, which populates lazily.
        return;
    }
    // Lower keylet == better quality (pages share the book prefix, quality
    // bits are encoded in the low bytes).
    if (dirKey < it->second.firstPageKey)
    {
        it->second.firstPageKey = dirKey;
        it->second.bestQuality = getQuality(dirKey);
        it->second.asOfLedger = seq;
    }
}

void
TopOfBookCache::onOfferDelete(Book const& book, uint256 const& dirKey)
{
    std::lock_guard lock(mutex_);
    auto it = map_.find(book);
    if (it == map_.end())
        return;
    if (it->second.firstPageKey == dirKey)
    {
        map_.erase(it);
        invalidations_.fetch_add(1, std::memory_order_relaxed);
    }
}

void
TopOfBookCache::invalidate(Book const& book)
{
    std::lock_guard lock(mutex_);
    if (map_.erase(book) != 0)
        invalidations_.fetch_add(1, std::memory_order_relaxed);
}

void
TopOfBookCache::clear()
{
    std::lock_guard lock(mutex_);
    map_.clear();
}

std::size_t
TopOfBookCache::size() const
{
    std::lock_guard lock(mutex_);
    return map_.size();
}

}  // namespace xrpl
