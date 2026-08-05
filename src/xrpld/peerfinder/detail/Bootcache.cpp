#include <xrpld/peerfinder/detail/Bootcache.h>

#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/peerfinder/detail/Store.h>
#include <xrpld/peerfinder/detail/Tuning.h>
#include <xrpld/peerfinder/detail/iosformat.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/PropertyStream.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace xrpl::PeerFinder {

Bootcache::Bootcache(Store& store, clock_type& clock, beast::Journal journal)
    : store_(store), clock_(clock), journal_(journal), whenUpdate_(clock_.now())

{
}

Bootcache::~Bootcache()
{
    update();
}

bool
Bootcache::empty() const
{
    return map_.empty();
}

Bootcache::map_type::size_type
Bootcache::size() const
{
    return map_.size();
}

Bootcache::const_iterator
Bootcache::begin() const
{
    return const_iterator(map_.right.begin());
}

Bootcache::const_iterator
Bootcache::cbegin() const
{
    return const_iterator(map_.right.begin());
}

Bootcache::const_iterator
Bootcache::end() const
{
    return const_iterator(map_.right.end());
}

Bootcache::const_iterator
Bootcache::cend() const
{
    return const_iterator(map_.right.end());
}

void
Bootcache::clear()
{
    map_.clear();
    needsUpdate_ = true;
}

//--------------------------------------------------------------------------

void
Bootcache::load()
{
    clear();
    auto const n(store_.load([this](beast::IP::Endpoint const& endpoint, int valence) {
        auto const result(this->map_.insert(value_type(endpoint, valence)));
        if (!result.second)
        {
            JLOG(this->journal_.error()) << beast::Leftw(18) << "Bootcache discard " << endpoint;
        }
    }));

    if (n > 0)
    {
        JLOG(journal_.info()) << beast::Leftw(18) << "Bootcache loaded " << n
                              << ((n > 1) ? " addresses" : " address");
        prune();
    }
}

bool
Bootcache::insert(beast::IP::Endpoint const& endpoint)
{
    auto const result(map_.insert(value_type(endpoint, 0)));
    if (result.second)
    {
        JLOG(journal_.trace()) << beast::Leftw(18) << "Bootcache insert " << endpoint;
        prune();
        flagForUpdate();
    }
    return result.second;
}

bool
Bootcache::insertStatic(beast::IP::Endpoint const& endpoint)
{
    auto result(map_.insert(value_type(endpoint, kStaticValence)));

    if (!result.second && (result.first->right.valence() < kStaticValence))
    {
        // An existing entry has too low a valence, replace it
        map_.erase(result.first);
        result = map_.insert(value_type(endpoint, kStaticValence));
    }

    if (result.second)
    {
        JLOG(journal_.trace()) << beast::Leftw(18) << "Bootcache insert " << endpoint;
        prune();
        flagForUpdate();
    }
    return result.second;
}

void
Bootcache::onSuccess(beast::IP::Endpoint const& endpoint)
{
    auto result(map_.insert(value_type(endpoint, 1)));
    if (result.second)
    {
        prune();
    }
    else
    {
        Entry entry(result.first->right);
        entry.valence() = std::max(entry.valence(), 0);
        ++entry.valence();
        map_.erase(result.first);
        result = map_.insert(value_type(endpoint, entry));
        XRPL_ASSERT(result.second, "xrpl::PeerFinder::Bootcache::onSuccess : endpoint inserted");
    }
    Entry const& entry(result.first->right);
    JLOG(journal_.info()) << beast::Leftw(18) << "Bootcache connect " << endpoint << " with "
                          << entry.valence() << ((entry.valence() > 1) ? " successes" : " success");
    flagForUpdate();
}

void
Bootcache::onFailure(beast::IP::Endpoint const& endpoint)
{
    auto result(map_.insert(value_type(endpoint, -1)));
    if (result.second)
    {
        prune();
    }
    else
    {
        Entry entry(result.first->right);
        entry.valence() = std::min(entry.valence(), 0);
        --entry.valence();
        map_.erase(result.first);
        result = map_.insert(value_type(endpoint, entry));
        XRPL_ASSERT(result.second, "xrpl::PeerFinder::Bootcache::onFailure : endpoint inserted");
    }
    Entry const& entry(result.first->right);
    auto const n(std::abs(entry.valence()));
    JLOG(journal_.debug()) << beast::Leftw(18) << "Bootcache failed " << endpoint << " with " << n
                           << ((n > 1) ? " attempts" : " attempt");
    flagForUpdate();
}

void
Bootcache::periodicActivity()
{
    checkUpdate();
}

//--------------------------------------------------------------------------

void
Bootcache::onWrite(beast::PropertyStream::Map& map)
{
    beast::PropertyStream::Set entries("entries", map);
    for (auto iter = map_.right.begin(); iter != map_.right.end(); ++iter)
    {
        beast::PropertyStream::Map entry(entries);
        entry["endpoint"] = iter->get_left().toString();
        entry["valence"] = std::int32_t(iter->get_right().valence());
    }
}

// Checks the cache size and prunes if its over the limit.
void
Bootcache::prune()
{
    if (size() <= Tuning::kBootcacheSize)
        return;

    // Calculate the amount to remove
    auto count((size() * Tuning::kBootcachePrunePercent) / 100);
    decltype(count) pruned(0);

    // Work backwards because bimap doesn't handle
    // erasing using a reverse iterator very well.
    //
    for (auto iter(map_.right.end()); count-- > 0 && iter != map_.right.begin(); ++pruned)
    {
        --iter;
        beast::IP::Endpoint const& endpoint(iter->get_left());
        Entry const& entry(iter->get_right());
        JLOG(journal_.trace()) << beast::Leftw(18) << "Bootcache pruned" << endpoint
                               << " at valence " << entry.valence();
        iter = map_.right.erase(iter);
    }

    JLOG(journal_.debug()) << beast::Leftw(18) << "Bootcache pruned " << pruned << " entries total";
}

// Updates the Store with the current set of entries if needed.
void
Bootcache::update()
{
    if (!needsUpdate_)
        return;
    std::vector<Store::Entry> list;
    list.reserve(map_.size());
    for (auto const& e : map_)
    {
        Store::Entry se;
        se.endpoint = e.get_left();
        se.valence = e.get_right().valence();
        list.push_back(se);
    }
    store_.save(list);
    // Reset the flag and cooldown timer
    needsUpdate_ = false;
    whenUpdate_ = clock_.now() + Tuning::kBootcacheCooldownTime;
}

// Checks the clock and calls update if we are off the cooldown.
void
Bootcache::checkUpdate()
{
    if (needsUpdate_ && whenUpdate_ < clock_.now())
        update();
}

// Called when changes to an entry will affect the Store.
void
Bootcache::flagForUpdate()
{
    needsUpdate_ = true;
    checkUpdate();
}

}  // namespace xrpl::PeerFinder
