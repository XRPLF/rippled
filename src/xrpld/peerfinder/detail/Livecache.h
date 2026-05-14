/** @file
 *  Short-lived peer endpoint cache partitioned by hop count.
 *
 *  `Livecache` stores endpoint advertisements received via `mtENDPOINTS`
 *  gossip messages. Entries expire after `Tuning::kLIVE_CACHE_SECONDS_TO_LIVE`
 *  (30 seconds) — peers only advertise when they have open slots, so a stale
 *  advertisement is actively misleading.
 *
 *  Contrast with `Bootcache`, which persists verified addresses across
 *  restarts. `Livecache` is the hot, real-time view of who has open slots
 *  right now; `Bootcache` is the cold historical record of reachable peers.
 */

#pragma once

#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/peerfinder/detail/Tuning.h>
#include <xrpld/peerfinder/detail/iosformat.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/random.h>
#include <xrpl/beast/container/aged_map.h>
#include <xrpl/beast/utility/maybe_const.h>

#include <boost/intrusive/list.hpp>
#include <boost/iterator/transform_iterator.hpp>

#include <algorithm>
#include <utility>

namespace xrpl::PeerFinder {

template <class>
class Livecache;

namespace detail {

/** Non-template base for `Livecache` holding type definitions shared across
 *  all allocator specializations.
 *
 *  Separating these types from the template avoids redundant instantiation
 *  and allows `Hop` to be used without knowing the allocator.
 */
class LivecacheBase
{
public:
    explicit LivecacheBase() = default;

protected:
    /** Combined storage node for the aged map and a hop-count intrusive list.
     *
     *  Inheriting from `list_base_hook<>` embeds the doubly-linked list
     *  pointers directly in the struct, so each `Element` can simultaneously
     *  be a value in the `aged_map` and a node in one of the per-hop
     *  `list_type` containers with no additional heap allocation.
     */
    struct Element : boost::intrusive::list_base_hook<>
    {
        Element(Endpoint endpoint) : endpoint(std::move(endpoint))
        {
        }

        Endpoint endpoint;
    };

    /** Intrusive doubly-linked list of `Element` objects.
     *
     *  `constant_time_size<false>` is chosen because `size()` is never
     *  called on individual hop lists; omitting the counter saves one
     *  word per list.
     */
    using list_type =
        boost::intrusive::make_list<Element, boost::intrusive::constant_time_size<false>>::type;

public:
    /** Read-only (or mutable, depending on `IsConst`) view of all endpoints
     *  at a single hop distance.
     *
     *  `Hop` is a lightweight reference wrapper around a `list_type`. It
     *  exposes `Endpoint const&` through `boost::transform_iterator` so
     *  callers never see the intrusive-list machinery. The `IsConst` parameter
     *  is threaded through `beast::MaybeConst` to produce either a `const`
     *  or mutable reference to the underlying list from a single template.
     *
     *  @note `moveBack()` requires a `const_cast` internally because the
     *      iterator type is always const even though the storage is mutable.
     *      This is safe: the `Element` objects are owned by the `aged_map`
     *      and are never truly const.
     */
    template <bool IsConst>
    class Hop
    {
    public:
        /** Functor that projects an `Element` to its contained `Endpoint`.
         *
         *  Used as the transformation in `boost::transform_iterator` so that
         *  iteration over the intrusive list yields `Endpoint const&` values.
         */
        struct Transform
        {
            using first_argument = Element;
            using result_type = Endpoint;

            explicit Transform() = default;

            Endpoint const&
            operator()(Element const& e) const
            {
                return e.endpoint;
            }
        };

    public:
        using iterator = boost::transform_iterator<Transform, typename list_type::const_iterator>;

        using const_iterator = iterator;

        using reverse_iterator =
            boost::transform_iterator<Transform, typename list_type::const_reverse_iterator>;

        using const_reverse_iterator = reverse_iterator;

        [[nodiscard]] iterator
        begin() const
        {
            return iterator(list_.get().cbegin(), Transform());
        }

        [[nodiscard]] iterator
        cbegin() const
        {
            return iterator(list_.get().cbegin(), Transform());
        }

        [[nodiscard]] iterator
        end() const
        {
            return iterator(list_.get().cend(), Transform());
        }

        [[nodiscard]] iterator
        cend() const
        {
            return iterator(list_.get().cend(), Transform());
        }

        [[nodiscard]] reverse_iterator
        rbegin() const
        {
            return reverse_iterator(list_.get().crbegin(), Transform());
        }

        [[nodiscard]] reverse_iterator
        crbegin() const
        {
            return reverse_iterator(list_.get().crbegin(), Transform());
        }

        [[nodiscard]] reverse_iterator
        rend() const
        {
            return reverse_iterator(list_.get().crend(), Transform());
        }

        [[nodiscard]] reverse_iterator
        crend() const
        {
            return reverse_iterator(list_.get().crend(), Transform());
        }

        /** Move the element at `pos` to the tail of this hop bucket.
         *
         *  Called by `Handouts` after an endpoint is handed out to a peer,
         *  ensuring that recently-distributed addresses recede to the back
         *  and other entries get priority on the next handout round.
         *
         *  @param pos Iterator to the element to move.
         */
        void
        moveBack(const_iterator pos)
        {
            auto& e(const_cast<Element&>(*pos.base()));
            list_.get().erase(list_.get().iterator_to(e));
            list_.get().push_back(e);
        }

    private:
        explicit Hop(typename beast::MaybeConst<IsConst, list_type>::type& list) : list_(list)
        {
        }

        friend class LivecacheBase;

        std::reference_wrapper<typename beast::MaybeConst<IsConst, list_type>::type> list_;
    };

protected:
    // Work-around to call Hop's private constructor from Livecache
    template <bool IsConst>
    static Hop<IsConst>
    makeHop(typename beast::MaybeConst<IsConst, list_type>::type& list)
    {
        return Hop<IsConst>(list);
    }
};

}  // namespace detail

//------------------------------------------------------------------------------

/** Short-lived cache of relayed peer endpoint advertisements.
 *
 *  Populated from `mtENDPOINTS` gossip messages. Entries expire after
 *  `Tuning::kLIVE_CACHE_SECONDS_TO_LIVE` (30 s) because a peer only
 *  advertises itself when it has open connection slots; stale entries
 *  represent peers that are now full and should not be contacted.
 *
 *  Addresses are stored without connectivity verification, so this cache
 *  is unsuitable for bootstrapping across restarts — use `Bootcache` for
 *  that. `Livecache` is the real-time view of peers with open slots.
 *
 *  Entries are partitioned into `hops` buckets (one per hop distance 0–7).
 *  `Logic` calls `hops.shuffle()` before every handout to prevent a
 *  malicious peer from biasing topology by spamming its own address to the
 *  front of the list.
 *
 *  @tparam Allocator Standard allocator forwarded to the internal
 *      `beast::aged_map`. Defaults to `std::allocator<char>`. Tests inject
 *      a custom allocator for deterministic memory tracking.
 */
template <class Allocator = std::allocator<char>>
class Livecache : protected detail::LivecacheBase
{
private:
    using cache_type = beast::aged_map<
        beast::IP::Endpoint,
        Element,
        std::chrono::steady_clock,
        std::less<beast::IP::Endpoint>,
        Allocator>;

    beast::Journal journal_;
    cache_type cache_;

public:
    using allocator_type = Allocator;

    /** Construct the cache.
     *
     *  @param clock  Steady clock used by the internal `aged_map` for TTL
     *      tracking. In production this is the process-wide `steady_clock`;
     *      tests inject a `TestStopwatch` for deterministic expiration.
     *  @param journal Logging sink for insert/expire/refresh trace messages.
     *  @param alloc  Allocator forwarded to the `aged_map`. Defaults to
     *      `std::allocator<char>`.
     */
    Livecache(clock_type& clock, beast::Journal journal, Allocator alloc = Allocator());

    /** Hop-partitioned index over the entries in `cache_`.
     *
     *  Iterating over `hops` yields a sequence of `Hop<IsConst>` views, one
     *  per hop distance. Array index 0 is the local node; indices 1 through
     *  `kMAX_HOPS` are normal relayable endpoints; index `kMAX_HOPS + 1` is
     *  an overflow bucket for endpoints that arrived at `kMAX_HOPS` and had
     *  their hop count incremented before insertion. Overflow endpoints are
     *  used for outbound connection attempts and redirect responses but are
     *  never propagated further — doing so would push them past the hop limit.
     *
     *  @note Always call `shuffle()` before iterating for handout. New
     *      entries are always pushed to the front (`push_front`), so without
     *      a shuffle a malicious peer can bias which addresses other nodes
     *      connect to by repeatedly advertising its own address.
     */
    class HopsT
    {
    private:
        using Histogram = std::array<int, 1 + Tuning::kMAX_HOPS + 1>;
        using lists_type = std::array<list_type, 1 + Tuning::kMAX_HOPS + 1>;

        /** Functor that wraps a `list_type` reference in a `Hop<IsConst>` view.
         *
         *  Used as the transformation in the outer `boost::transform_iterator`
         *  so that iterating over `HopsT` yields `Hop` objects rather than
         *  raw `list_type` references.
         */
        template <bool IsConst>
        struct Transform
        {
            using first_argument = typename lists_type::value_type;
            using result_type = Hop<IsConst>;

            explicit Transform() = default;

            Hop<IsConst>
            operator()(typename beast::MaybeConst<IsConst, typename lists_type::value_type>::type&
                           list) const
            {
                return makeHop<IsConst>(list);
            }
        };

    public:
        using iterator = boost::transform_iterator<Transform<false>, typename lists_type::iterator>;

        using const_iterator =
            boost::transform_iterator<Transform<true>, typename lists_type::const_iterator>;

        using reverse_iterator =
            boost::transform_iterator<Transform<false>, typename lists_type::reverse_iterator>;

        using const_reverse_iterator =
            boost::transform_iterator<Transform<true>, typename lists_type::const_reverse_iterator>;

        iterator
        begin()
        {
            return iterator(lists_.begin(), Transform<false>());
        }

        [[nodiscard]] const_iterator
        begin() const
        {
            return const_iterator(lists_.cbegin(), Transform<true>());
        }

        [[nodiscard]] const_iterator
        cbegin() const
        {
            return const_iterator(lists_.cbegin(), Transform<true>());
        }

        iterator
        end()
        {
            return iterator(lists_.end(), Transform<false>());
        }

        [[nodiscard]] const_iterator
        end() const
        {
            return const_iterator(lists_.cend(), Transform<true>());
        }

        [[nodiscard]] const_iterator
        cend() const
        {
            return const_iterator(lists_.cend(), Transform<true>());
        }

        reverse_iterator
        rbegin()
        {
            return reverse_iterator(lists_.rbegin(), Transform<false>());
        }

        [[nodiscard]] const_reverse_iterator
        rbegin() const
        {
            return const_reverse_iterator(lists_.crbegin(), Transform<true>());
        }

        [[nodiscard]] const_reverse_iterator
        crbegin() const
        {
            return const_reverse_iterator(lists_.crbegin(), Transform<true>());
        }

        reverse_iterator
        rend()
        {
            return reverse_iterator(lists_.rend(), Transform<false>());
        }

        [[nodiscard]] const_reverse_iterator
        rend() const
        {
            return const_reverse_iterator(lists_.crend(), Transform<true>());
        }

        [[nodiscard]] const_reverse_iterator
        crend() const
        {
            return const_reverse_iterator(lists_.crend(), Transform<true>());
        }

        /** Randomize the order of every hop bucket.
         *
         *  Copies each bucket's elements into a temporary vector, shuffles
         *  with `defaultPrng()`, and rebuilds the list. Must be called before
         *  any handout operation (`redirect`, `autoconnect`,
         *  `buildEndpointsForPeers`) to prevent topology bias from a peer
         *  that repeatedly advertises itself to the front of the list.
         */
        void
        shuffle();

        /** Return a comma-separated count of entries per hop bucket.
         *
         *  The string has one integer per bucket (indices 0 through
         *  `kMAX_HOPS + 1`), e.g. `"0, 3, 5, 2, 0, 0, 0, 0"`. Used by
         *  `onWrite` for diagnostics and by unit tests to verify distribution.
         *
         *  @return Comma-separated histogram string.
         */
        [[nodiscard]] std::string
        histogram() const;

    private:
        explicit HopsT(Allocator const& alloc);

        /** Add `e` to the front of its hop bucket and update the histogram. */
        void
        insert(Element& e);

        /** Move `e` from its current hop bucket to the bucket for `hops`,
         *  updating the histogram accordingly.
         *
         *  Called when a duplicate address is re-advertised at a lower hop
         *  count than the stored value.
         */
        void
        reinsert(Element& e, std::uint32_t hops);

        /** Unlink `e` from its hop bucket and update the histogram.
         *
         *  Called by `Livecache::expire()` before erasing the element from
         *  the aged map.
         */
        void
        remove(Element& e);

        friend class Livecache;
        lists_type lists_;
        Histogram hist_{};
    } hops;

    /** Return `true` if the cache contains no entries. */
    [[nodiscard]] bool
    empty() const
    {
        return cache_.empty();
    }

    /** Return the number of unique addresses currently in the cache. */
    typename cache_type::size_type
    size() const
    {
        return cache_.size();
    }

    /** Remove all entries older than `Tuning::kLIVE_CACHE_SECONDS_TO_LIVE`.
     *
     *  Scans the `aged_map` in chronological order (oldest first) and stops
     *  as soon as an entry within the TTL window is found. For each expired
     *  entry, removes it from its hop bucket before erasing it from the map.
     *  Called by `Logic::once_per_second()`.
     */
    void
    expire();

    /** Insert or refresh an endpoint advertisement.
     *
     *  Three cases:
     *  - **New address**: added to the map and placed in the hop bucket for
     *    `ep.hops`.
     *  - **Duplicate at a higher hop count**: silently dropped. A closer
     *    path already exists; the more-distant advertisement carries no new
     *    information.
     *  - **Duplicate at the same or lower hop count**: the entry's TTL is
     *    refreshed via `touch()`. If the new hop count is strictly lower, the
     *    element is moved to the correct bucket via `reinsert()`.
     *
     *  @param ep The endpoint to insert. `ep.hops` must be ≤
     *      `Tuning::kMAX_HOPS + 1`; the caller (Logic::onEndpoints) is
     *      responsible for incrementing hop count before calling this method,
     *      so an address received at `kMAX_HOPS` arrives here at
     *      `kMAX_HOPS + 1` and lands in the overflow bucket.
     *  @pre `ep.hops <= Tuning::kMAX_HOPS + 1` (asserted).
     */
    void
    insert(Endpoint const& ep);

    /** Write cache statistics and entry details to a `PropertyStream`.
     *
     *  Emits `size`, a `hist` histogram string, and an `entries` array
     *  where each element contains the address, hop count, and remaining
     *  TTL in clock ticks.
     *
     *  @param map Destination property map (e.g. for the `/crawl` endpoint).
     */
    void
    onWrite(beast::PropertyStream::Map& map);
};

//------------------------------------------------------------------------------

template <class Allocator>
Livecache<Allocator>::Livecache(clock_type& clock, beast::Journal journal, Allocator alloc)
    : journal_(journal), cache_(clock, alloc), hops(alloc)
{
}

template <class Allocator>
void
Livecache<Allocator>::expire()
{
    std::size_t n(0);
    typename cache_type::time_point const expired(
        cache_.clock().now() - Tuning::kLIVE_CACHE_SECONDS_TO_LIVE);
    for (auto iter(cache_.chronological.begin());
         iter != cache_.chronological.end() && iter.when() <= expired;)
    {
        Element& e(iter->second);
        hops.remove(e);
        iter = cache_.erase(iter);
        ++n;
    }
    if (n > 0)
    {
        JLOG(journal_.debug()) << beast::Leftw(18) << "Livecache expired " << n
                               << ((n > 1) ? " entries" : " entry");
    }
}

template <class Allocator>
void
Livecache<Allocator>::insert(Endpoint const& ep)
{
    XRPL_ASSERT(
        ep.hops <= (Tuning::kMAX_HOPS + 1),
        "xrpl::PeerFinder::Livecache::insert : maximum input hops");
    auto result = cache_.emplace(ep.address, ep);
    Element& e(result.first->second);
    if (result.second)
    {
        hops.insert(e);
        JLOG(journal_.debug()) << beast::Leftw(18) << "Livecache insert " << ep.address
                               << " at hops " << ep.hops;
        return;
    }
    if (!result.second && (ep.hops > e.endpoint.hops))
    {
        std::size_t const excess(ep.hops - e.endpoint.hops);
        JLOG(journal_.trace()) << beast::Leftw(18) << "Livecache drop " << ep.address
                               << " at hops +" << excess;
        return;
    }

    cache_.touch(result.first);

    if (ep.hops < e.endpoint.hops)
    {
        hops.reinsert(e, ep.hops);
        JLOG(journal_.debug()) << beast::Leftw(18) << "Livecache update " << ep.address
                               << " at hops " << ep.hops;
    }
    else
    {
        JLOG(journal_.trace()) << beast::Leftw(18) << "Livecache refresh " << ep.address
                               << " at hops " << ep.hops;
    }
}

template <class Allocator>
void
Livecache<Allocator>::onWrite(beast::PropertyStream::Map& map)
{
    typename cache_type::time_point const expired(
        cache_.clock().now() - Tuning::kLIVE_CACHE_SECONDS_TO_LIVE);
    map["size"] = size();
    map["hist"] = hops.histogram();
    beast::PropertyStream::Set set("entries", map);
    for (auto iter(cache_.cbegin()); iter != cache_.cend(); ++iter)
    {
        auto const& e(iter->second);
        beast::PropertyStream::Map item(set);
        item["hops"] = e.endpoint.hops;
        item["address"] = e.endpoint.address.toString();
        std::stringstream ss;
        ss << (iter.when() - expired).count();
        item["expires"] = ss.str();
    }
}

//------------------------------------------------------------------------------

template <class Allocator>
void
Livecache<Allocator>::HopsT::shuffle()
{
    for (auto& list : lists_)
    {
        std::vector<std::reference_wrapper<Element>> v;
        v.reserve(list.size());
        std::ranges::copy(list, std::back_inserter(v));
        std::shuffle(v.begin(), v.end(), defaultPrng());
        list.clear();
        for (auto& e : v)
            list.push_back(e);
    }
}

template <class Allocator>
std::string
Livecache<Allocator>::HopsT::histogram() const
{
    std::string s;
    for (auto const& h : hist_)
    {
        if (!s.empty())
            s += ", ";
        s += std::to_string(h);
    }
    return s;
}

template <class Allocator>
Livecache<Allocator>::HopsT::HopsT(Allocator const& alloc)
{
    std::ranges::fill(hist_, 0);
}

template <class Allocator>
void
Livecache<Allocator>::HopsT::insert(Element& e)
{
    XRPL_ASSERT(
        e.endpoint.hops <= Tuning::kMAX_HOPS + 1,
        "xrpl::PeerFinder::Livecache::HopsT::insert : maximum input hops");
    // This has security implications without a shuffle
    lists_[e.endpoint.hops].push_front(e);
    ++hist_[e.endpoint.hops];
}

template <class Allocator>
void
Livecache<Allocator>::HopsT::reinsert(Element& e, std::uint32_t numHops)
{
    XRPL_ASSERT(
        numHops <= Tuning::kMAX_HOPS + 1,
        "xrpl::PeerFinder::Livecache::HopsT::reinsert : maximum hops input");

    auto& list = lists_[e.endpoint.hops];
    list.erase(list.iterator_to(e));

    --hist_[e.endpoint.hops];

    e.endpoint.hops = numHops;
    insert(e);
}

template <class Allocator>
void
Livecache<Allocator>::HopsT::remove(Element& e)
{
    --hist_[e.endpoint.hops];

    auto& list = lists_[e.endpoint.hops];
    list.erase(list.iterator_to(e));
}

}  // namespace xrpl::PeerFinder
