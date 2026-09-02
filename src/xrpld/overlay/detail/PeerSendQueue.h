#pragma once

#include <xrpld/overlay/Message.h>

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
 * Not thread safe: the owning peer accesses it from its strand only.
 */
class PeerSendQueue
{
public:
    void
    push(std::shared_ptr<Message> const& m)
    {
        if (m->isPriority())
            priority_.push(m);
        else
            bulk_.push(m);
    }

    /**
     * Remove and return the next message to write: the priority lane first,
     * then bulk. Requires !empty().
     */
    std::shared_ptr<Message>
    pop()
    {
        auto& lane = priority_.empty() ? bulk_ : priority_;
        auto m = std::move(lane.front());
        lane.pop();
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
     * toward a disconnect or a query refusal.
     */
    std::size_t
    bulkSize() const
    {
        return bulk_.size();
    }

    std::size_t
    prioritySize() const
    {
        return priority_.size();
    }

private:
    std::queue<std::shared_ptr<Message>> priority_;
    std::queue<std::shared_ptr<Message>> bulk_;
};

}  // namespace xrpl
