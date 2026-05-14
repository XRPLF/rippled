/** @file
 *  Persistent bootstrap address cache for PeerFinder cold-start.
 *
 *  Maintains a ranked list of IP endpoints that survive restarts, allowing the
 *  node to immediately attempt connections to previously-reachable peers.
 *  Entries are ranked by valence (a streak counter) so high-reliability
 *  addresses are tried first. The cache is loaded from and flushed to a
 *  SQLite-backed `Store`, with writes batched behind a 60-second cooldown to
 *  avoid excessive I/O during connection storms.
 *
 *  @see Bootcache.h, Logic.h, Store.h
 */

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

/** Load persisted entries from the Store, replacing any current in-memory state.
 *
 *  Clears the cache before loading so the Store is always the authoritative
 *  source on startup. Duplicate entries returned by the Store are discarded
 *  with an error log. `prune()` is called after loading to enforce the
 *  `kBOOTCACHE_SIZE` cap in case the stored set grew beyond the limit while
 *  the node was offline.
 */
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

/** Add a dynamically-learned address to the cache with initial valence zero.
 *
 *  This path is taken for addresses received via peer gossip. The operation is
 *  idempotent: if the endpoint is already present the cache is unchanged and
 *  `false` is returned. On a new insert, `prune()` enforces the size cap and
 *  `flagForUpdate()` schedules a deferred write-back.
 *
 *  @param endpoint The IP endpoint to add.
 *  @return `true` if the endpoint was inserted; `false` if it already existed.
 */
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

/** Add a statically-configured bootstrap address with elevated priority.
 *
 *  Static addresses receive `kSTATIC_VALENCE` (32), placing them near the top
 *  of the sorted iteration order so the node preferentially connects to its
 *  own trusted seeds. If the address already exists with a lower valence the
 *  entry is erased and reinserted to enforce the minimum — historical failures
 *  cannot permanently demote a configured seed.
 *
 *  @param endpoint The IP endpoint from the node's static configuration.
 *  @return `true` if the entry was inserted or upgraded; `false` if it already
 *      existed at `kSTATIC_VALENCE` or higher.
 *  @note Bimap values are logically const after insertion, so a valence upgrade
 *      requires the erase-then-reinsert sequence used here.
 */
bool
Bootcache::insertStatic(beast::IP::Endpoint const& endpoint)
{
    auto result(map_.insert(value_type(endpoint, kSTATIC_VALENCE)));

    if (!result.second && (result.first->right.valence() < kSTATIC_VALENCE))
    {
        // An existing entry has too low a valence, replace it
        map_.erase(result.first);
        result = map_.insert(value_type(endpoint, kSTATIC_VALENCE));
    }

    if (result.second)
    {
        JLOG(journal_.trace()) << beast::Leftw(18) << "Bootcache insert " << endpoint;
        prune();
        flagForUpdate();
    }
    return result.second;
}

/** Record a successful outbound connection handshake for the given endpoint.
 *
 *  Increments the endpoint's valence streak counter. Before incrementing, the
 *  current valence is clamped to zero so that a failure streak is fully
 *  cleared before recording the first success — the resulting valence is
 *  always 1 after the transition from any negative value, not a cumulative
 *  sum. If the endpoint is not yet in the cache it is inserted with valence 1.
 *  The bimap's immutability invariant requires an erase-then-reinsert to
 *  change the valence of an existing entry.
 *
 *  @param endpoint The endpoint whose connection handshake just completed.
 *  @note Valence tracks a streak (consecutive outcomes), not a lifetime score.
 *      A peer that failed five times then succeeded once gets valence=1, not -4.
 */
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

/** Record a failed outbound connection attempt for the given endpoint.
 *
 *  Decrements the endpoint's valence streak counter. Mirrors the clamping
 *  logic in `onSuccess`: the current valence is clamped to zero before
 *  decrementing, so a success streak is fully cleared on the first failure and
 *  the resulting valence is -1, not the sum of past successes minus one. If
 *  the endpoint is not yet in the cache it is inserted with valence -1.
 *
 *  @param endpoint The endpoint whose connection attempt just failed.
 *  @note Valence tracks a streak (consecutive outcomes), not a lifetime score.
 *      A peer that succeeded ten times then failed once gets valence=-1, not 9.
 */
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

/** Trim the cache to `kBOOTCACHE_SIZE` by removing the lowest-valence entries.
 *
 *  Removes `kBOOTCACHE_PRUNE_PERCENT` (10%) of entries from the tail of the
 *  right-side multiset, which holds entries in ascending `Entry::operator<`
 *  order — i.e., descending valence — so the tail is always the least
 *  reliable addresses.
 *
 *  @note The loop walks a forward iterator backward from `right.end()` rather
 *      than using a reverse iterator because Boost bimap does not support
 *      erasing via reverse iterators cleanly.
 */
void
Bootcache::prune()
{
    if (size() <= Tuning::kBOOTCACHE_SIZE)
        return;

    auto count((size() * Tuning::kBOOTCACHE_PRUNE_PERCENT) / 100);
    decltype(count) pruned(0);

    // Work backwards because bimap doesn't handle
    // erasing using a reverse iterator very well.
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

/** Serialize the entire cache to the persistent Store, if an update is pending.
 *
 *  Snapshots the current bimap into a `vector<Store::Entry>` and calls
 *  `store_.save()`. Resets `needsUpdate_` and advances `whenUpdate_` by the
 *  cooldown period (`kBOOTCACHE_COOLDOWN_TIME` = 60 s) so that back-to-back
 *  mutation bursts result in at most one write per minute.
 *
 *  @note Called unconditionally from the destructor to guarantee the final
 *      in-memory state is always persisted on clean shutdown.
 */
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
    needsUpdate_ = false;
    whenUpdate_ = clock_.now() + Tuning::kBOOTCACHE_COOLDOWN_TIME;
}

/** Flush to the Store if an update is pending and the cooldown has elapsed.
 *
 *  Called from `periodicActivity()` on each maintenance tick. Does nothing if
 *  no mutation has occurred since the last write, or if the 60-second cooldown
 *  window has not yet expired.
 */
void
Bootcache::checkUpdate()
{
    if (needsUpdate_ && whenUpdate_ < clock_.now())
        update();
}

/** Mark the cache as dirty and attempt a write-back if the cooldown permits.
 *
 *  Every mutation that must survive a restart — inserts, static inserts, and
 *  connection outcome records — calls this method. Sets `needsUpdate_` and
 *  immediately delegates to `checkUpdate()`, which will flush only if the
 *  60-second cooldown has expired since the last write.
 */
void
Bootcache::flagForUpdate()
{
    needsUpdate_ = true;
    checkUpdate();
}

}  // namespace xrpl::PeerFinder
