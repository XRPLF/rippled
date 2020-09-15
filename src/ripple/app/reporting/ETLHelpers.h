//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_APP_REPORTING_ETLHELPERS_H_INCLUDED
#define RIPPLE_APP_REPORTING_ETLHELPERS_H_INCLUDED
#include <ripple/app/main/Application.h>
#include <ripple/ledger/ReadView.h>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <sstream>

namespace ripple {

/// This datastructure is used to keep track of the sequence of the most recent
/// ledger validated by the network. There are two methods that will wait until
/// certain conditions are met. This datastructure is able to be "stopped". When
/// the datastructure is stopped, any threads currently waiting are unblocked.
/// Any later calls to methods of this datastructure will not wait. Once the
/// datastructure is stopped, the datastructure remains stopped for the rest of
/// its lifetime.
class NetworkValidatedLedgers
{
    // max sequence validated by network
    std::optional<uint32_t> max_;

    mutable std::mutex m_;

    std::condition_variable cv_;

    bool stopping_ = false;

public:
    /// Notify the datastructure that idx has been validated by the network
    /// @param idx sequence validated by network
    void
    push(uint32_t idx)
    {
        std::lock_guard lck(m_);
        if (!max_ || idx > *max_)
            max_ = idx;
        cv_.notify_all();
    }

    /// Get most recently validated sequence. If no ledgers are known to have
    /// been validated, this function waits until the next ledger is validated
    /// @return sequence of most recently validated ledger. empty optional if
    /// the datastructure has been stopped
    std::optional<uint32_t>
    getMostRecent()
    {
        std::unique_lock lck(m_);
        cv_.wait(lck, [this]() { return max_ || stopping_; });
        return max_;
    }

    /// Waits for the sequence to be validated by the network
    /// @param sequence to wait for
    /// @return true if sequence was validated, false otherwise
    /// a return value of false means the datastructure has been stopped
    bool
    waitUntilValidatedByNetwork(uint32_t sequence)
    {
        std::unique_lock lck(m_);
        cv_.wait(lck, [sequence, this]() {
            return (max_ && sequence <= *max_) || stopping_;
        });
        return !stopping_;
    }

    /// Puts the datastructure in the stopped state
    /// Future calls to this datastructure will not block
    /// This operation cannot be reversed
    void
    stop()
    {
        std::lock_guard lck(m_);
        stopping_ = true;
        cv_.notify_all();
    }
};

/// Generic thread-safe queue with an optional maximum size
/// Note, we can't use a lockfree queue here, since we need the ability to wait
/// for an element to be added or removed from the queue. These waits are
/// blocking calls.
template <class T>
class ThreadSafeQueue
{
    std::queue<T> queue_;

    mutable std::mutex m_;
    std::condition_variable cv_;
    std::optional<uint32_t> maxSize_;

public:
    /// @param maxSize maximum size of the queue. Calls that would cause the
    /// queue to exceed this size will block until free space is available
    explicit ThreadSafeQueue(uint32_t maxSize) : maxSize_(maxSize)
    {
    }

    /// Create a queue with no maximum size
    ThreadSafeQueue() = default;

    /// @param elt element to push onto queue
    /// if maxSize is set, this method will block until free space is available
    void
    push(T const& elt)
    {
        std::unique_lock lck(m_);
        // if queue has a max size, wait until not full
        if (maxSize_)
            cv_.wait(lck, [this]() { return queue_.size() <= *maxSize_; });
        queue_.push(elt);
        cv_.notify_all();
    }

    /// @param elt element to push onto queue. elt is moved from
    /// if maxSize is set, this method will block until free space is available
    void
    push(T&& elt)
    {
        std::unique_lock lck(m_);
        // if queue has a max size, wait until not full
        if (maxSize_)
            cv_.wait(lck, [this]() { return queue_.size() <= *maxSize_; });
        queue_.push(std::move(elt));
        cv_.notify_all();
    }

    /// @return element popped from queue. Will block until queue is non-empty
    T
    pop()
    {
        std::unique_lock lck(m_);
        cv_.wait(lck, [this]() { return !queue_.empty(); });
        T ret = std::move(queue_.front());
        queue_.pop();
        // if queue has a max size, unblock any possible pushers
        if (maxSize_)
            cv_.notify_all();
        return ret;
    }
};

/// Parititions the uint256 keyspace into numMarkers partitions, each of equal
/// size.
inline std::vector<uint256>
getMarkers(size_t numMarkers)
{
    assert(numMarkers <= 256);

    unsigned char incr = 256 / numMarkers;

    std::vector<uint256> markers;
    markers.reserve(numMarkers);
    uint256 base{0};
    for (size_t i = 0; i < numMarkers; ++i)
    {
        markers.push_back(base);
        base.data()[0] += incr;
    }
    return markers;
}

}  // namespace ripple
#endif
