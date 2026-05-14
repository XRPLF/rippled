/** @file
 *  Persistent bootstrap address cache for PeerFinder cold-start.
 *
 *  Defines `Bootcache`, the ranked IP endpoint cache that `Logic` consults
 *  when initiating outbound connections in the absence of live peer data.
 *  Entries are ranked by valence (a consecutive-outcome streak counter) and
 *  persisted to a `Store` (SQLite in production) with writes throttled behind
 *  a 60-second cooldown.
 *
 *  @see Bootcache.cpp, Logic.h, Store.h
 */

#pragma once

#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/peerfinder/detail/Store.h>

#include <xrpl/basics/comparators.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/PropertyStream.h>

#include <boost/bimap.hpp>
#include <boost/bimap/multiset_of.hpp>
#include <boost/bimap/unordered_set_of.hpp>
#include <boost/iterator/transform_iterator.hpp>

namespace xrpl::PeerFinder {

/** Persistent bootstrap address cache used by PeerFinder for cold-start connections.
 *
 *  Maintains a ranked set of IP endpoints that survives process restarts.
 *  `Logic` iterates this cache — from highest to lowest valence — when it
 *  needs outbound connections but the `Livecache` is empty (e.g., on first
 *  launch or after losing all peers).
 *
 *  Each entry carries a *valence* reputation score: a positive value records
 *  consecutive successful handshakes; a negative value records consecutive
 *  failures. The streak resets to zero before crossing the sign boundary, so
 *  a long history of successes does not shield a peer that suddenly starts
 *  refusing connections. Statically configured addresses start at
 *  `kSTATIC_VALENCE` (32) to ensure they are tried first and survive pruning.
 *
 *  The internal data structure is a `boost::bimap` with an unordered left
 *  side (O(1) lookup by endpoint) and a sorted right side (iteration in
 *  decreasing-valence order). Because bimap entries are logically immutable
 *  after insertion, valence updates use an erase-then-reinsert pattern.
 *
 *  Writes to the backing `Store` are debounced with a 60-second cooldown
 *  (`Tuning::kBOOTCACHE_COOLDOWN_TIME`). The destructor always flushes,
 *  bypassing the cooldown, to ensure the final state is persisted on shutdown.
 *  The cache is pruned to `Tuning::kBOOTCACHE_SIZE` (1000) entries on every
 *  insert and after `load()`.
 *
 *  @note Not thread-safe; all calls must be serialized by the caller
 *      (in practice, `Logic` guards access with its own recursive mutex).
 *  @see Logic.h, Store.h, Tuning.h
 */
class Bootcache
{
private:
    /** Valence wrapper stored on the right side of the bimap.
     *
     *  `operator<` sorts in *decreasing* valence order so that the most
     *  reliable endpoints appear at the front of the right-side multiset.
     */
    class Entry
    {
    public:
        Entry(int valence) : valence_(valence)
        {
        }

        int&
        valence()
        {
            return valence_;
        }

        [[nodiscard]] int
        valence() const
        {
            return valence_;
        }

        friend bool
        operator<(Entry const& lhs, Entry const& rhs)
        {
            return lhs.valence() > rhs.valence();
        }

    private:
        int valence_;
    };

    using left_t = boost::bimaps::unordered_set_of<
        beast::IP::Endpoint,
        boost::hash<beast::IP::Endpoint>,
        xrpl::equal_to<beast::IP::Endpoint>>;
    using right_t = boost::bimaps::multiset_of<Entry, xrpl::less<Entry>>;
    using map_type = boost::bimap<left_t, right_t>;
    using value_type = map_type::value_type;

    /** Functor that projects a right-side bimap entry to its `IP::Endpoint`.
     *
     *  Used with `boost::transform_iterator` to expose a valence-sorted range
     *  of endpoints to callers without leaking the bimap internals.
     */
    struct Transform
    {
        using first_argument_type = map_type::right_map::const_iterator::value_type const&;
        using result_type = beast::IP::Endpoint const&;

        explicit Transform() = default;

        beast::IP::Endpoint const&
        operator()(map_type::right_map::const_iterator::value_type const& v) const
        {
            return v.get_left();
        }
    };

private:
    map_type map_;

    Store& store_;
    clock_type& clock_;
    beast::Journal journal_;

    /** Time after which we can update the database again. */
    clock_type::time_point whenUpdate_;

    /** Set to true when a database update is needed. */
    bool needsUpdate_{false};

public:
    /** Valence assigned to statically configured addresses.
     *
     *  Ensures hand-configured seeds start near the top of the connection
     *  priority queue and are not displaced by pruning under normal operation.
     */
    static constexpr int kSTATIC_VALENCE = 32;

    /** Forward iterator over endpoints in decreasing-valence order.
     *
     *  Wraps the right-side bimap iterator via `Transform` so callers
     *  receive plain `beast::IP::Endpoint` references without needing to
     *  know about the bimap or `Entry` internals.
     */
    using iterator = boost::transform_iterator<Transform, map_type::right_map::const_iterator>;

    /** Alias for `iterator`; all iteration over this cache is read-only. */
    using const_iterator = iterator;

    /** Construct a Bootcache backed by the given store and clock.
     *
     *  @param store  Persistence back-end; must outlive this object.
     *  @param clock  Monotonic clock used to enforce the write cooldown.
     *  @param journal Logging sink.
     */
    Bootcache(Store& store, clock_type& clock, beast::Journal journal);

    /** Flush any pending write to the store unconditionally, then destroy. */
    ~Bootcache();

    /** Returns `true` if the cache holds no entries. */
    [[nodiscard]] bool
    empty() const;

    /** Returns the number of entries currently in the cache. */
    [[nodiscard]] map_type::size_type
    size() const;

    /** @{ */
    /** Return an iterator to the first endpoint in decreasing-valence order. */
    [[nodiscard]] const_iterator
    begin() const;
    [[nodiscard]] const_iterator
    cbegin() const;
    /** Return a past-the-end iterator for the valence-sorted endpoint range. */
    [[nodiscard]] const_iterator
    end() const;
    [[nodiscard]] const_iterator
    cend() const;
    /** Erase all entries and mark the cache as needing a store update. */
    void
    clear();
    /** @} */

    /** Replace in-memory state with entries loaded from the backing Store.
     *
     *  Clears the cache before loading so the Store is always the authoritative
     *  source on startup. Duplicate entries returned by the Store are discarded
     *  with an error log. `prune()` is called after loading to enforce the
     *  `kBOOTCACHE_SIZE` cap in case the stored set grew beyond the limit while
     *  the node was offline.
     */
    void
    load();

    /** Add a dynamically-learned address to the cache with initial valence zero.
     *
     *  Intended for addresses received via peer gossip. The operation is
     *  idempotent: if the endpoint is already present the cache is unchanged.
     *  On a new insert, `prune()` enforces the size cap and a deferred
     *  write-back is scheduled via `flagForUpdate()`.
     *
     *  @param endpoint The IP endpoint to add.
     *  @return `true` if the endpoint was inserted; `false` if it already existed.
     */
    bool
    insert(beast::IP::Endpoint const& endpoint);

    /** Add a statically configured bootstrap address with elevated priority.
     *
     *  Assigns `kSTATIC_VALENCE` (32), placing the entry near the top of the
     *  sorted iteration order. If the endpoint is already present with a lower
     *  valence, the old entry is replaced so that historical failures cannot
     *  permanently demote a configured seed.
     *
     *  @param endpoint The IP endpoint from the node's static configuration.
     *  @return `true` if the entry was inserted or upgraded to `kSTATIC_VALENCE`;
     *      `false` if it already existed at `kSTATIC_VALENCE` or higher.
     *  @note Bimap values are logically const after insertion, so a valence
     *      upgrade requires the erase-then-reinsert pattern used internally.
     */
    bool
    insertStatic(beast::IP::Endpoint const& endpoint);

    /** Record a successful outbound connection handshake for the given endpoint.
     *
     *  Increments the endpoint's consecutive-success streak. Any prior failure
     *  streak is first clamped to zero, so the resulting valence after
     *  transitioning from negative is always 1 — not a cumulative sum. If the
     *  endpoint is not yet in the cache it is inserted with valence 1.
     *
     *  @param endpoint The endpoint whose connection handshake just completed.
     *  @note Valence tracks a streak, not a lifetime score. A peer that failed
     *      five times then succeeded once gets valence=1, not −4.
     */
    void
    onSuccess(beast::IP::Endpoint const& endpoint);

    /** Record a failed outbound connection attempt for the given endpoint.
     *
     *  Decrements the endpoint's consecutive-failure streak. Any prior success
     *  streak is first clamped to zero, so the resulting valence after
     *  transitioning from positive is always −1. If the endpoint is not yet in
     *  the cache it is inserted with valence −1.
     *
     *  @param endpoint The endpoint whose connection attempt just failed.
     *  @note Valence tracks a streak, not a lifetime score. A peer that
     *      succeeded ten times then failed once gets valence=−1, not 9.
     */
    void
    onFailure(beast::IP::Endpoint const& endpoint);

    /** Flush any pending write to the Store if the 60-second cooldown has elapsed.
     *
     *  Called by `Logic` on each maintenance-timer tick. Does nothing if no
     *  mutation has occurred since the last write, or if the cooldown window
     *  has not yet expired.
     */
    void
    periodicActivity();

    /** Write the current cache contents to a property stream for diagnostics.
     *
     *  Emits an `"entries"` array under `map`, where each element contains
     *  `"endpoint"` (string) and `"valence"` (int32) fields. Entries appear
     *  in decreasing-valence order.
     *
     *  @param map The property stream map to write into.
     */
    void
    onWrite(beast::PropertyStream::Map& map);

private:
    /** Remove the lowest-valence entries until the cache is within the size cap. */
    void
    prune();
    /** Serialize the cache to the Store unconditionally; advances the cooldown timer. */
    void
    update();
    /** Flush to the Store if `needsUpdate_` is set and the cooldown has elapsed. */
    void
    checkUpdate();
    /** Mark the cache dirty and immediately attempt a throttled write-back. */
    void
    flagForUpdate();
};

}  // namespace xrpl::PeerFinder
