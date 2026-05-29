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
            // Only publish jvObj if this is the first occurrence
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
