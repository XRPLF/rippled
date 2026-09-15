/**
 * @file ValidationTracker.cpp
 * Implementation of the ValidationTracker class.
 */

#include <xrpld/telemetry/ValidationTracker.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Protocol.h>

#include <boost/smart_ptr/make_shared.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace xrpl::telemetry {

ValidationTracker::TimePoint
ValidationTracker::steadyNow()
{
    return Clock::now();
}

// ---- writer side, called from consensus and ledger threads -----------------

void
ValidationTracker::recordOurValidation(uint256 const& ledgerHash, LedgerIndex seq)
{
    totalValidationsSent_.fetch_add(1, std::memory_order_relaxed);

    // The producer stamps the time. The grace period is measured from when the
    // event happened, so stamping at drain time would smear it by a cycle.
    if (!ourRing_.push(ledgerHash, seq, now_()))
        droppedEvents_.fetch_add(1, std::memory_order_relaxed);
}

void
ValidationTracker::recordNetworkValidation(uint256 const& ledgerHash, LedgerIndex seq)
{
    totalValidationsChecked_.fetch_add(1, std::memory_order_relaxed);

    if (!networkRing_.push(ledgerHash, seq, now_()))
        droppedEvents_.fetch_add(1, std::memory_order_relaxed);
}

// ---- reducer, called from the metrics reader -------------------------------

void
ValidationTracker::reconcile()
{
    if (reducing_.test_and_set(std::memory_order_acquire))
        return;

    auto const now = now_();
    drainRings();
    decidePending(now);
    advanceWindows(minuteOf(now));
    publish();

    reducing_.clear(std::memory_order_release);
}

void
ValidationTracker::drainRings()
{
    ourRing_.drain([this](Slot const& s) { note(s, true); });
    networkRing_.drain([this](Slot const& s) { note(s, false); });
}

void
ValidationTracker::note(Slot const& s, bool ours)
{
    auto const it = pending_.find(s.hash);
    if (it == pending_.end())
    {
        // A hash already counted must not open a fresh entry, or the same
        // ledger reaches the totals twice.
        if (tallied_.contains(s.hash))
            return;

        auto& evt = pending_[s.hash];
        evt.recordTime = s.at;
        evt.minute = minuteOf(s.at);
        (ours ? evt.weValidated : evt.networkValidated) = true;
        return;
    }

    (ours ? it->second.weValidated : it->second.networkValidated) = true;
}

void
ValidationTracker::decidePending(TimePoint now)
{
    for (auto& [hash, evt] : pending_)
    {
        if (!evt.decided)
        {
            if (now - evt.recordTime < kGracePeriod)
                continue;

            evt.decided = true;
            evt.agreed = evt.weValidated && evt.networkValidated;
            noteTallied(hash);
            addToWindows(evt.minute, evt.agreed);
            (evt.agreed ? totalAgreements_ : totalMissed_).fetch_add(1, std::memory_order_relaxed);
        }
        else if (
            !evt.agreed && evt.weValidated && evt.networkValidated &&
            now - evt.recordTime <= kLateRepairWindow)
        {
            // A miss whose other half arrived late. Only `agreed` moves; the
            // total was already counted in this event's bucket.
            evt.agreed = true;
            repairInWindows(evt.minute);
            totalMissed_.fetch_sub(1, std::memory_order_relaxed);
            totalAgreements_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // Nothing can be repaired past the window, so the entry is dead weight.
    auto const cutoff = now - kLateRepairWindow;
    for (auto it = pending_.begin(); it != pending_.end();)
    {
        it = (it->second.decided && it->second.recordTime < cutoff) ? pending_.erase(it)
                                                                    : std::next(it);
    }
}

void
ValidationTracker::noteTallied(uint256 const& ledgerHash)
{
    if (!tallied_.insert(ledgerHash).second)
        return;

    talliedOrder_.push_back(ledgerHash);
    while (talliedOrder_.size() > kMaxTalliedEvents)
    {
        tallied_.erase(talliedOrder_.front());
        talliedOrder_.pop_front();
    }
}

// ---- buckets and window counters ------------------------------------------

void
ValidationTracker::addToWindows(std::uint64_t minute, bool agreed)
{
    advanceWindows(minute);

    auto& b = buckets_[minute % kBuckets7d];
    ++b.total;
    if (agreed)
        ++b.agreed;

    for (auto* w : {&c1h_, &c24h_, &c7d_})
    {
        ++w->total;
        if (agreed)
            ++w->agreed;
    }
}

void
ValidationTracker::repairInWindows(std::uint64_t minute)
{
    // No window tail check is needed: the repair window is shorter than the
    // shortest window, so a repairable event is still inside all three.
    static_assert(
        kLateRepairWindow < std::chrono::minutes(kBuckets1h),
        "a repairable event must still be inside the shortest window");

    ++buckets_[minute % kBuckets7d].agreed;
    ++c1h_.agreed;
    ++c24h_.agreed;
    ++c7d_.agreed;
}

void
ValidationTracker::advanceWindows(std::uint64_t minute)
{
    if (!started_)
    {
        started_ = true;
        tail1h_ = tail24h_ = tail7d_ = minute;
        newestMinute_ = minute;
        return;
    }

    if (minute <= newestMinute_)
        return;

    auto const previousNewest = newestMinute_;
    newestMinute_ = minute;

    // Nothing was recorded for longer than the whole grid, so every bucket
    // still holding a count is stale. Clear them together instead of one at a
    // time. The comparison is against the last minute WRITTEN, not against a
    // window tail: under steady traffic a tail always sits exactly one
    // grid-length back, and comparing to it would wipe live data every minute
    // once the grid fills.
    if (minute - previousNewest >= kBuckets7d)
    {
        buckets_.fill(Bucket{});
        c1h_ = c24h_ = c7d_ = WindowCount{};
        tail1h_ = tail24h_ = tail7d_ = minute;
        return;
    }

    retireWindow(tail1h_, oldestInWindow(minute, kBuckets1h), c1h_, false);
    retireWindow(tail24h_, oldestInWindow(minute, kBuckets24h), c24h_, false);
    retireWindow(tail7d_, oldestInWindow(minute, kBuckets7d), c7d_, true);
}

void
ValidationTracker::retireWindow(
    std::uint64_t& tail,
    std::uint64_t target,
    WindowCount& count,
    bool clear)
{
    while (tail < target)
    {
        auto& b = buckets_[tail % kBuckets7d];
        count.total -= b.total;
        count.agreed -= b.agreed;
        if (clear)
            b = Bucket{};
        ++tail;
    }
}

std::uint64_t
ValidationTracker::oldestInWindow(std::uint64_t minute, std::size_t span)
{
    return minute + 1 >= span ? minute + 1 - span : 0;
}

std::uint64_t
ValidationTracker::minuteOf(TimePoint t)
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::minutes>(t.time_since_epoch()).count());
}

// ---- snapshot publication and reads ---------------------------------------

void
ValidationTracker::publish()
{
    published_.store(
        boost::make_shared<Snapshot const>(Snapshot{.w1h = c1h_, .w24h = c24h_, .w7d = c7d_}));
}

ValidationTracker::Snapshot
ValidationTracker::read() const
{
    // Holding the shared_ptr keeps this snapshot alive for as long as the
    // caller needs it, so the reducer can never write the values being read.
    auto const s = published_.load();
    return s ? *s : Snapshot{};
}

double
ValidationTracker::pct(WindowCount const& w)
{
    if (w.total == 0)
        return 0.0;
    return (static_cast<double>(w.agreed) / static_cast<double>(w.total)) * 100.0;
}

double
ValidationTracker::agreementPct1h() const
{
    return pct(read().w1h);
}

double
ValidationTracker::agreementPct24h() const
{
    return pct(read().w24h);
}

double
ValidationTracker::agreementPct7d() const
{
    return pct(read().w7d);
}

std::uint64_t
ValidationTracker::agreements1h() const
{
    return read().w1h.agreed;
}

std::uint64_t
ValidationTracker::missed1h() const
{
    return read().w1h.missed();
}

std::uint64_t
ValidationTracker::agreements24h() const
{
    return read().w24h.agreed;
}

std::uint64_t
ValidationTracker::missed24h() const
{
    return read().w24h.missed();
}

std::uint64_t
ValidationTracker::agreements7d() const
{
    return read().w7d.agreed;
}

std::uint64_t
ValidationTracker::missed7d() const
{
    return read().w7d.missed();
}

std::uint64_t
ValidationTracker::totalAgreements() const
{
    return totalAgreements_.load(std::memory_order_relaxed);
}

std::uint64_t
ValidationTracker::totalMissed() const
{
    return totalMissed_.load(std::memory_order_relaxed);
}

std::uint64_t
ValidationTracker::totalValidationsSent() const
{
    return totalValidationsSent_.load(std::memory_order_relaxed);
}

std::uint64_t
ValidationTracker::totalValidationsChecked() const
{
    return totalValidationsChecked_.load(std::memory_order_relaxed);
}

std::uint64_t
ValidationTracker::droppedEvents() const
{
    return droppedEvents_.load(std::memory_order_relaxed);
}

}  // namespace xrpl::telemetry
