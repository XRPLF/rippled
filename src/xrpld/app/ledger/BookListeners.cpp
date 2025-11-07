#include <xrpld/app/ledger/OrderBookDB.h>

namespace ripple {

void
BookListeners::addSubscriber(InfoSub::ref sub)
{
    std::lock_guard sl(mLock);
    mListeners[sub->getSeq()] = sub;
}

void
BookListeners::removeSubscriber(std::uint64_t seq)
{
    std::lock_guard sl(mLock);
    mListeners.erase(seq);
}

void
BookListeners::publish(
    MultiApiJson const& jvObj,
    hash_set<std::uint64_t>& havePublished)
{
    std::lock_guard sl(mLock);
    auto it = mListeners.cbegin();

    while (it != mListeners.cend())
    {
        InfoSub::pointer p = it->second.lock();

        if (p)
        {
            // Only publish jvObj if this is the first occurence
            if (havePublished.emplace(p->getSeq()).second)
            {
                jvObj.visit(
                    p->getApiVersion(),  //
                    [&](Json::Value const& jv) { p->send(jv, true); });
            }
            ++it;
        }
        else
            it = mListeners.erase(it);
    }
}

}  // namespace ripple
