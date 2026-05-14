/** @file
 *  Implements `BookListeners`: per-book subscriber fan-out for WebSocket
 *  order-book subscriptions.
 *
 *  Each `BookListeners` instance holds the set of `InfoSub` weak pointers that
 *  care about a specific currency-pair `Book`. `OrderBookDB` owns all instances
 *  and calls `publish()` for each affected book when a transaction is accepted.
 *
 *  Three design choices worth noting:
 *  - **Weak-pointer storage**: subscribers are kept as `InfoSub::wptr` so that
 *    `BookListeners` never extends a connection's lifetime.
 *  - **Lazy GC**: dead entries are evicted inside `publish()` when
 *    `weak_ptr::lock()` fails; no separate sweep is needed.
 *  - **Cross-book deduplication**: the caller threads a single
 *    `hash_set<uint64_t>` through every `publish()` call for a transaction so
 *    a client subscribed to multiple affected books receives the message only
 *    once.
 */
#include <xrpl/ledger/BookListeners.h>

#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/MultiApiJson.h>
#include <xrpl/server/InfoSub.h>

#include <cstdint>
#include <mutex>

namespace xrpl {

void
BookListeners::addSubscriber(InfoSub::ref sub)
{
    std::scoped_lock const sl(lock_);
    listeners_[sub->getSeq()] = sub;
}

void
BookListeners::removeSubscriber(std::uint64_t seq)
{
    std::scoped_lock const sl(lock_);
    listeners_.erase(seq);
}

void
BookListeners::publish(MultiApiJson const& jvObj, hash_set<std::uint64_t>& havePublished)
{
    std::scoped_lock const sl(lock_);
    auto it = listeners_.cbegin();

    while (it != listeners_.cend())
    {
        InfoSub::pointer p = it->second.lock();

        if (p)
        {
            if (havePublished.emplace(p->getSeq()).second)
            {
                jvObj.visit(
                    p->getApiVersion(),  //
                    [&](json::Value const& jv) { p->send(jv, true); });
            }
            ++it;
        }
        else
        {
            it = listeners_.erase(it);
        }
    }
}

}  // namespace xrpl
