/** @file ValidationTracker.cpp
    Implementation of the ValidationTracker class.
*/

#include <xrpld/telemetry/ValidationTracker.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Protocol.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>

namespace xrpl::telemetry {

void
ValidationTracker::recordOurValidation(uint256 const& ledgerHash, LedgerIndex seq)
{
    std::lock_guard const lock(mutex_);
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
}

void
ValidationTracker::recordNetworkValidation(uint256 const& ledgerHash, LedgerIndex seq)
{
    std::lock_guard const lock(mutex_);
    auto& evt = pending_[ledgerHash];
    if (evt.recordTime == TimePoint{})
    {
        evt.ledgerHash = ledgerHash;
        evt.seq = seq;
        evt.recordTime = Clock::now();
    }
    evt.networkValidated = true;
    totalValidationsChecked_.fetch_add(1, std::memory_order_relaxed);
}

void
ValidationTracker::reconcile()
{
    std::lock_guard const lock(mutex_);
    auto const now = Clock::now();

    for (auto& [hash, evt] : pending_)
    {
        if (!evt.reconciled && (now - evt.recordTime) >= kGracePeriod)
        {
            // Initial reconciliation after grace period.
            evt.reconciled = true;
            evt.agreed = evt.weValidated && evt.networkValidated;

            if (evt.agreed)
            {
                totalAgreements_.fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                totalMissed_.fetch_add(1, std::memory_order_relaxed);
            }

            WindowEvent const we{.time = now, .ledgerHash = evt.ledgerHash, .agreed = evt.agreed};
            window1h_.push_back(we);
            window24h_.push_back(we);
            window7d_.push_back(we);
        }
        else if (
            evt.reconciled && !evt.agreed && evt.weValidated && evt.networkValidated &&
            (now - evt.recordTime) <= kLateRepairWindow)
        {
            // Late repair: was a miss, now both flags set.
            evt.agreed = true;
            totalMissed_.fetch_sub(1, std::memory_order_relaxed);
            totalAgreements_.fetch_add(1, std::memory_order_relaxed);

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

    // Hard trim if still over limit -- remove reconciled entries that are
    // past the late-repair window first, then any reconciled entry as a
    // last resort.
    if (pending_.size() > kMaxPendingEvents)
    {
        // Pass 1: only entries past late-repair window.
        for (auto it = pending_.begin();
             it != pending_.end() && pending_.size() > kMaxPendingEvents;)
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
        // Pass 2: any reconciled entry if still over limit.
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
    std::lock_guard const lock(mutex_);
    if (window1h_.empty())
        return 0.0;
    auto const agreed = static_cast<double>(
        std::count_if(window1h_.begin(), window1h_.end(), [](auto const& e) { return e.agreed; }));
    return (agreed / static_cast<double>(window1h_.size())) * 100.0;
}

double
ValidationTracker::agreementPct24h() const
{
    std::lock_guard const lock(mutex_);
    if (window24h_.empty())
        return 0.0;
    auto const agreed = static_cast<double>(std::count_if(
        window24h_.begin(), window24h_.end(), [](auto const& e) { return e.agreed; }));
    return (agreed / static_cast<double>(window24h_.size())) * 100.0;
}

uint64_t
ValidationTracker::agreements1h() const
{
    std::lock_guard const lock(mutex_);
    return static_cast<uint64_t>(
        std::count_if(window1h_.begin(), window1h_.end(), [](auto const& e) { return e.agreed; }));
}

uint64_t
ValidationTracker::missed1h() const
{
    std::lock_guard const lock(mutex_);
    return static_cast<uint64_t>(
        std::count_if(window1h_.begin(), window1h_.end(), [](auto const& e) { return !e.agreed; }));
}

uint64_t
ValidationTracker::agreements24h() const
{
    std::lock_guard const lock(mutex_);
    return static_cast<uint64_t>(std::count_if(
        window24h_.begin(), window24h_.end(), [](auto const& e) { return e.agreed; }));
}

uint64_t
ValidationTracker::missed24h() const
{
    std::lock_guard const lock(mutex_);
    return static_cast<uint64_t>(std::count_if(
        window24h_.begin(), window24h_.end(), [](auto const& e) { return !e.agreed; }));
}

double
ValidationTracker::agreementPct7d() const
{
    std::lock_guard const lock(mutex_);
    if (window7d_.empty())
        return 0.0;
    auto const agreed = static_cast<double>(
        std::count_if(window7d_.begin(), window7d_.end(), [](auto const& e) { return e.agreed; }));
    return (agreed / static_cast<double>(window7d_.size())) * 100.0;
}

uint64_t
ValidationTracker::agreements7d() const
{
    std::lock_guard const lock(mutex_);
    return static_cast<uint64_t>(
        std::count_if(window7d_.begin(), window7d_.end(), [](auto const& e) { return e.agreed; }));
}

uint64_t
ValidationTracker::missed7d() const
{
    std::lock_guard const lock(mutex_);
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
    for (auto it = window.rbegin(); it != window.rend(); ++it)
    {
        if (!it->agreed && it->ledgerHash == hash)
        {
            it->agreed = true;
            return;
        }
    }
}

}  // namespace xrpl::telemetry
