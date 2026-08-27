/**
 * @file ValidationTracker.cpp
 * Implementation of the ValidationTracker class.
 */

#include <xrpld/telemetry/ValidationTracker.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Protocol.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <ranges>

namespace xrpl::telemetry {

void
ValidationTracker::recordOurValidation(uint256 const& ledgerHash, LedgerIndex seq)
{
    std::scoped_lock const lock(mutex_);
    auto& evt = pending_[ledgerHash];
    if (evt.recordTime == TimePoint{})
    {
        // First time seeing this ledger hash -- initialize.
        evt.ledgerHash = ledgerHash;
        evt.seq = seq;
        evt.recordTime = Clock::now();
    }
    evt.weValidated = true;
    totalValidationsSent_.fetch_add(1, std::memory_order_relaxed);
    boundPending(ledgerHash);
}

void
ValidationTracker::recordNetworkValidation(uint256 const& ledgerHash, LedgerIndex seq)
{
    std::scoped_lock const lock(mutex_);
    auto& evt = pending_[ledgerHash];
    if (evt.recordTime == TimePoint{})
    {
        evt.ledgerHash = ledgerHash;
        evt.seq = seq;
        evt.recordTime = Clock::now();
    }
    evt.networkValidated = true;
    totalValidationsChecked_.fetch_add(1, std::memory_order_relaxed);
    boundPending(ledgerHash);
}

void
ValidationTracker::classifyPending(LedgerEvent& evt, TimePoint now)
{
    evt.reconciled = true;
    evt.agreed = evt.weValidated && evt.networkValidated;

    if (evt.agreed)
    {
        totalAgreements_.fetch_add(1, std::memory_order_relaxed);
        totalAgreementsGross_.fetch_add(1, std::memory_order_relaxed);
    }
    else
    {
        totalMissed_.fetch_add(1, std::memory_order_relaxed);
        totalMissedGross_.fetch_add(1, std::memory_order_relaxed);
    }

    WindowEvent const we{.time = now, .ledgerHash = evt.ledgerHash, .agreed = evt.agreed};
    window1h_.push_back(we);
    window24h_.push_back(we);
    window7d_.push_back(we);
}

void
ValidationTracker::boundPending(uint256 const& justRecorded)
{
    if (pending_.size() <= kMaxPendingEvents)
        return;

    auto oldest = pending_.end();
    for (auto it = pending_.begin(); it != pending_.end(); ++it)
    {
        if (it->first == justRecorded)
            continue;
        if (oldest == pending_.end() || it->second.recordTime < oldest->second.recordTime)
            oldest = it;
    }

    if (oldest == pending_.end())
        return;

    // Classify before erasing. An event contributes to the agreement and miss
    // totals only when it is classified, so dropping an unclassified one would
    // lose it outright -- the ledger would be counted neither as an agreement
    // nor as a miss, and the lifetime totals would silently under-report.
    //
    // This forces the classification earlier than the grace period, using
    // whichever flags have arrived. That is the cost of being over the bound at
    // all: a validation that would have arrived during the remaining grace
    // window now lands after its event is gone, so it can neither complete the
    // agreement nor repair it later. Being over the bound means reconcile() is
    // not running, so the alternative is unbounded growth.
    if (!oldest->second.reconciled)
        classifyPending(oldest->second, Clock::now());

    pending_.erase(oldest);
}

std::size_t
ValidationTracker::pendingCount() const
{
    std::scoped_lock const lock(mutex_);
    return pending_.size();
}

void
ValidationTracker::reconcile()
{
    std::scoped_lock const lock(mutex_);
    auto const now = Clock::now();

    for (auto& [hash, evt] : pending_)
    {
        if (!evt.reconciled && (now - evt.recordTime) >= kGracePeriod)
        {
            // Initial reconciliation after grace period. The gross tallies are
            // moved once, here, at first classification -- see the
            // counting-decision note in the repair branch below.
            classifyPending(evt, now);
        }
        else if (
            evt.reconciled && !evt.agreed && evt.weValidated && evt.networkValidated &&
            (now - evt.recordTime) <= kLateRepairWindow)
        {
            // Late repair: was a miss, now both flags set. Adjust the NET
            // totals (used by the windowed agreement gauge) so the live view
            // reflects the repair.
            evt.agreed = true;
            totalMissed_.fetch_sub(1, std::memory_order_relaxed);
            totalAgreements_.fetch_add(1, std::memory_order_relaxed);

            // Counting decision (initial-classification only): the gross
            // tallies (totalAgreementsGross_ / totalMissedGross_) that back the
            // monotonic Prometheus _total counters are deliberately NOT touched
            // here. Each ledger is counted once, at first classification; a
            // repair must not decrement missed (a _total may never decrease)
            // nor add a second agreement (which would double-count the ledger).

            // Flip the corresponding window entries from miss to agreement.
            repairWindowEntry(window1h_, evt.ledgerHash);
            repairWindowEntry(window24h_, evt.ledgerHash);
            repairWindowEntry(window7d_, evt.ledgerHash);
        }
    }

    evictStaleWindows(now);
    evictOldPending(now);
}

void
ValidationTracker::evictStaleWindows(TimePoint now)
{
    auto const cutoff1h = now - kWindow1h;
    while (!window1h_.empty() && window1h_.front().time < cutoff1h)
        window1h_.pop_front();

    auto const cutoff24h = now - kWindow24h;
    while (!window24h_.empty() && window24h_.front().time < cutoff24h)
        window24h_.pop_front();

    auto const cutoff7d = now - kWindow7d;
    while (!window7d_.empty() && window7d_.front().time < cutoff7d)
        window7d_.pop_front();
}

void
ValidationTracker::evictOldPending(TimePoint now)
{
    auto const cutoff = now - kLateRepairWindow;
    for (auto it = pending_.begin(); it != pending_.end();)
    {
        if (it->second.reconciled && it->second.recordTime < cutoff)
        {
            it = pending_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Hard trim if still over limit. The loop above already removed every
    // reconciled entry older than the late-repair window, so here we drop
    // any remaining reconciled entry as a last resort.
    if (pending_.size() > kMaxPendingEvents)
    {
        for (auto it = pending_.begin();
             it != pending_.end() && pending_.size() > kMaxPendingEvents;)
        {
            if (it->second.reconciled)
            {
                it = pending_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}

double
ValidationTracker::agreementPct1h() const
{
    std::scoped_lock const lock(mutex_);
    if (window1h_.empty())
        return 0.0;
    auto const agreed = static_cast<double>(
        std::count_if(window1h_.begin(), window1h_.end(), [](auto const& e) { return e.agreed; }));
    return (agreed / static_cast<double>(window1h_.size())) * 100.0;
}

double
ValidationTracker::agreementPct24h() const
{
    std::scoped_lock const lock(mutex_);
    if (window24h_.empty())
        return 0.0;
    auto const agreed = static_cast<double>(std::count_if(
        window24h_.begin(), window24h_.end(), [](auto const& e) { return e.agreed; }));
    return (agreed / static_cast<double>(window24h_.size())) * 100.0;
}

uint64_t
ValidationTracker::agreements1h() const
{
    std::scoped_lock const lock(mutex_);
    return static_cast<uint64_t>(
        std::count_if(window1h_.begin(), window1h_.end(), [](auto const& e) { return e.agreed; }));
}

uint64_t
ValidationTracker::missed1h() const
{
    std::scoped_lock const lock(mutex_);
    return static_cast<uint64_t>(
        std::count_if(window1h_.begin(), window1h_.end(), [](auto const& e) { return !e.agreed; }));
}

uint64_t
ValidationTracker::agreements24h() const
{
    std::scoped_lock const lock(mutex_);
    return static_cast<uint64_t>(std::count_if(
        window24h_.begin(), window24h_.end(), [](auto const& e) { return e.agreed; }));
}

uint64_t
ValidationTracker::missed24h() const
{
    std::scoped_lock const lock(mutex_);
    return static_cast<uint64_t>(std::count_if(
        window24h_.begin(), window24h_.end(), [](auto const& e) { return !e.agreed; }));
}

double
ValidationTracker::agreementPct7d() const
{
    std::scoped_lock const lock(mutex_);
    if (window7d_.empty())
        return 0.0;
    auto const agreed = static_cast<double>(
        std::count_if(window7d_.begin(), window7d_.end(), [](auto const& e) { return e.agreed; }));
    return (agreed / static_cast<double>(window7d_.size())) * 100.0;
}

uint64_t
ValidationTracker::agreements7d() const
{
    std::scoped_lock const lock(mutex_);
    return static_cast<uint64_t>(
        std::count_if(window7d_.begin(), window7d_.end(), [](auto const& e) { return e.agreed; }));
}

uint64_t
ValidationTracker::missed7d() const
{
    std::scoped_lock const lock(mutex_);
    return static_cast<uint64_t>(
        std::count_if(window7d_.begin(), window7d_.end(), [](auto const& e) { return !e.agreed; }));
}

uint64_t
ValidationTracker::totalAgreements() const
{
    return totalAgreements_.load(std::memory_order_relaxed);
}

uint64_t
ValidationTracker::totalMissed() const
{
    return totalMissed_.load(std::memory_order_relaxed);
}

uint64_t
ValidationTracker::totalAgreementsEver() const
{
    return totalAgreementsGross_.load(std::memory_order_relaxed);
}

uint64_t
ValidationTracker::totalMissedEver() const
{
    return totalMissedGross_.load(std::memory_order_relaxed);
}

uint64_t
ValidationTracker::totalValidationsSent() const
{
    return totalValidationsSent_.load(std::memory_order_relaxed);
}

uint64_t
ValidationTracker::totalValidationsChecked() const
{
    return totalValidationsChecked_.load(std::memory_order_relaxed);
}

void
ValidationTracker::repairWindowEntry(std::deque<WindowEvent>& window, uint256 const& hash)
{
    // Scan backwards since late repairs target recently added entries.
    for (auto& event : std::views::reverse(window))
    {
        if (!event.agreed && event.ledgerHash == hash)
        {
            event.agreed = true;
            return;
        }
    }
}

}  // namespace xrpl::telemetry
