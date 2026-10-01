#pragma once

#include <xrpld/overlay/Message.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <queue>

namespace xrpl {

/**
 * Outbound message queue for one peer connection, with a consensus lane.
 *
 * Messages that carry consensus state (see Message::isPriority) wait in
 * their own lane and are popped before any bulk message, so a burst of
 * relayed transactions cannot delay the proposals and validations queued
 * behind it. Within each lane order is preserved.
 *
 * push, pop and empty are for the owning peer's strand only. bulkSize and
 * prioritySize are also read from PeerImp::json(), which runs on an RPC
 * thread, so the two are backed by atomic counters rather than the queues'
 * own size().
 */
class PeerSendQueue
{
public:
    void
    push(std::shared_ptr<Message> const& m)
    {
        if (m->isPriority())
        {
            priority_.push(m);
            priorityCount_.fetch_add(1, std::memory_order_relaxed);
        }
        else
        {
            bulk_.push(m);
            bulkCount_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    /**
     * Remove and return the next message to write: the priority lane first,
     * then bulk. Requires !empty().
     */
    std::shared_ptr<Message>
    pop()
    {
        bool const fromPriority = !priority_.empty();
        auto& lane = fromPriority ? priority_ : bulk_;
        auto m = std::move(lane.front());
        lane.pop();
        (fromPriority ? priorityCount_ : bulkCount_).fetch_sub(1, std::memory_order_relaxed);
        return m;
    }

    bool
    empty() const
    {
        return priority_.empty() && bulk_.empty();
    }

    /**
     * Bulk lane depth. This is the number the peer health checks use:
     * priority traffic is bounded by the validator set and must not count
     * toward a disconnect or a query refusal. Safe to call off the strand.
     */
    std::size_t
    bulkSize() const
    {
        return bulkCount_.load(std::memory_order_relaxed);
    }

    /**
     * Priority lane depth. Safe to call off the strand.
     */
    std::size_t
    prioritySize() const
    {
        return priorityCount_.load(std::memory_order_relaxed);
    }

private:
    std::queue<std::shared_ptr<Message>> priority_;
    std::queue<std::shared_ptr<Message>> bulk_;
    std::atomic<std::size_t> priorityCount_ = 0;
    std::atomic<std::size_t> bulkCount_ = 0;
};

}  // namespace xrpl
