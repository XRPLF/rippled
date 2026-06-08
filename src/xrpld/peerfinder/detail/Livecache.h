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

class LivecacheBase
{
public:
    explicit LivecacheBase() = default;

protected:
    struct Element : boost::intrusive::list_base_hook<>
    {
        Element(Endpoint endpoint) : endpoint(std::move(endpoint))
        {
        }

        Endpoint endpoint;
    };

    using list_type =
        boost::intrusive::make_list<Element, boost::intrusive::constant_time_size<false>>::type;

public:
    /** A list of Endpoint at the same hops
        This is a lightweight wrapper around a reference to the underlying
        container.
    */
    template <bool IsConst>
    class Hop
    {
    public:
        // Iterator transformation to extract the endpoint from Element
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

        // move the element to the end of the container
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

/** The Livecache holds the short-lived relayed Endpoint messages.

    Since peers only advertise themselves when they have open slots,
    we want these messages to expire rather quickly after the peer becomes
    full.

    Addresses added to the cache are not connection-tested to see if
    they are connectable (with one small exception regarding neighbors).
    Therefore, these addresses are not suitable for persisting across
    launches or for bootstrapping, because they do not have verifiable
    and locally observed uptime and connectability information.
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

    /** Create the cache. */
    Livecache(clock_type& clock, beast::Journal journal, Allocator alloc = Allocator());

    //
    // Iteration by hops
    //
    // The range [begin, end) provides a sequence of list_type
    // where each list contains endpoints at a given hops.
    //

    class HopsT
    {
    private:
        // An endpoint at hops=0 represents the local node.
        // Endpoints coming in at maxHops are stored at maxHops +1,
        // but not given out (since they would exceed maxHops). They
        // are used for automatic connection attempts.
        //
        using Histogram = std::array<int, 1 + Tuning::kMaxHops + 1>;
        using lists_type = std::array<list_type, 1 + Tuning::kMaxHops + 1>;

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

        /** Shuffle each hop list. */
        void
        shuffle();

        [[nodiscard]] std::string
        histogram() const;

    private:
        explicit HopsT(Allocator const& alloc);

        void
        insert(Element& e);

        // Reinsert e at a new hops
        void
        reinsert(Element& e, std::uint32_t hops);

        void
        remove(Element& e);

        friend class Livecache;
        lists_type lists_;
        Histogram hist_{};
    } hops;

    /** Returns `true` if the cache is empty. */
    [[nodiscard]] bool
    empty() const
    {
        return cache_.empty();
    }

    /** Returns the number of entries in the cache. */
    typename cache_type::size_type
    size() const
    {
        return cache_.size();
    }

    /** Erase entries whose time has expired. */
    void
    expire();

    /** Creates or updates an existing Element based on a new message. */
    void
    insert(Endpoint const& ep);

    /** Output statistics. */
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
        cache_.clock().now() - Tuning::kLiveCacheSecondsToLive);
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
    // The caller already incremented hop, so if we got a
    // message at maxHops we will store it at maxHops + 1.
    // This means we won't give out the address to other peers
    // but we will use it to make connections and hand it out
    // when redirecting.
    //
    XRPL_ASSERT(
        ep.hops <= (Tuning::kMaxHops + 1),
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
        // Drop duplicates at higher hops
        std::size_t const excess(ep.hops - e.endpoint.hops);
        JLOG(journal_.trace()) << beast::Leftw(18) << "Livecache drop " << ep.address
                               << " at hops +" << excess;
        return;
    }

    cache_.touch(result.first);

    // Address already in the cache so update metadata
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
        cache_.clock().now() - Tuning::kLiveCacheSecondsToLive);
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
        e.endpoint.hops <= Tuning::kMaxHops + 1,
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
        numHops <= Tuning::kMaxHops + 1,
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
