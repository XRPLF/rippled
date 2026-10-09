#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/basics/random.h>
#include <xrpl/beast/container/aged_map.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/PropertyStream.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/beast/utility/maybe_const.h>
#include <xrpl/peerfinder/Types.h>
#include <xrpl/peerfinder/detail/Tuning.h>

#include <boost/intrusive/list.hpp>
#include <boost/iterator/transform_iterator.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <ios>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::peer_finder {

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

    using ListType =
        boost::intrusive::make_list<Element, boost::intrusive::constant_time_size<false>>::type;

public:
    /**
     * A list of Endpoint at the same hops
     * This is a lightweight wrapper around a reference to the underlying
     * container.
     */
    template <bool IsConst>
    class Hop
    {
    public:
        // Iterator transformation to extract the endpoint from Element
        struct Transform
        {
            using FirstArgument = Element;
            using ResultType = Endpoint;

            explicit Transform() = default;

            Endpoint const&
            operator()(Element const& e) const
            {
                return e.endpoint;
            }
        };

    public:
        using Iterator = boost::transform_iterator<Transform, ListType::const_iterator>;

        using ConstIterator = Iterator;

        using ReverseIterator =
            boost::transform_iterator<Transform, ListType::const_reverse_iterator>;

        using ConstReverseIterator = ReverseIterator;

        [[nodiscard]] Iterator
        begin() const
        {
            return Iterator(list_.get().cbegin(), Transform());
        }

        [[nodiscard]] Iterator
        cbegin() const
        {
            return Iterator(list_.get().cbegin(), Transform());
        }

        [[nodiscard]] Iterator
        end() const
        {
            return Iterator(list_.get().cend(), Transform());
        }

        [[nodiscard]] Iterator
        cend() const
        {
            return Iterator(list_.get().cend(), Transform());
        }

        [[nodiscard]] ReverseIterator
        rbegin() const
        {
            return ReverseIterator(list_.get().crbegin(), Transform());
        }

        [[nodiscard]] ReverseIterator
        crbegin() const
        {
            return ReverseIterator(list_.get().crbegin(), Transform());
        }

        [[nodiscard]] ReverseIterator
        rend() const
        {
            return ReverseIterator(list_.get().crend(), Transform());
        }

        [[nodiscard]] ReverseIterator
        crend() const
        {
            return ReverseIterator(list_.get().crend(), Transform());
        }

        // move the element to the end of the container
        void
        moveBack(ConstIterator pos)
        {
            auto& e(const_cast<Element&>(*pos.base()));
            list_.get().erase(list_.get().iterator_to(e));
            list_.get().push_back(e);
        }

    private:
        explicit Hop(beast::MaybeConst<IsConst, ListType>::Type& list) : list_(list)
        {
        }

        friend class LivecacheBase;

        std::reference_wrapper<typename beast::MaybeConst<IsConst, ListType>::Type> list_;
    };

protected:
    // Work-around to call Hop's private constructor from Livecache
    template <bool IsConst>
    static Hop<IsConst>
    makeHop(beast::MaybeConst<IsConst, ListType>::Type& list)
    {
        return Hop<IsConst>(list);
    }
};

}  // namespace detail

//------------------------------------------------------------------------------

/**
 * The Livecache holds the short-lived relayed Endpoint messages.
 *
 * Since peers only advertise themselves when they have open slots,
 * we want these messages to expire rather quickly after the peer becomes
 * full.
 *
 * Addresses added to the cache are not connection-tested to see if
 * they are connectable (with one small exception regarding neighbors).
 * Therefore, these addresses are not suitable for persisting across
 * launches or for bootstrapping, because they do not have verifiable
 * and locally observed uptime and connectability information.
 */
template <class Allocator = std::allocator<char>>
class Livecache : protected detail::LivecacheBase
{
private:
    using CacheType = beast::AgedMap<
        beast::ip::Endpoint,
        Element,
        std::chrono::steady_clock,
        std::less<beast::ip::Endpoint>,
        Allocator>;

    beast::Journal journal_;
    CacheType cache_;

public:
    using AllocatorType = Allocator;

    /**
     * Create the cache.
     */
    Livecache(ClockType& clock, beast::Journal journal, Allocator alloc = Allocator());

    //
    // Iteration by hops
    //
    // The range [begin, end) provides a sequence of ListType
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
        using Histogram = std::array<int, 1 + tuning::kMaxHops + 1>;
        using ListsType = std::array<ListType, 1 + tuning::kMaxHops + 1>;

        template <bool IsConst>
        struct Transform
        {
            using FirstArgument = ListsType::value_type;
            using ResultType = Hop<IsConst>;

            explicit Transform() = default;

            Hop<IsConst>
            operator()(beast::MaybeConst<IsConst, ListsType::value_type>::Type& list) const
            {
                return makeHop<IsConst>(list);
            }
        };

    public:
        using Iterator = boost::transform_iterator<Transform<false>, ListsType::iterator>;

        using ConstIterator = boost::transform_iterator<Transform<true>, ListsType::const_iterator>;

        using ReverseIterator =
            boost::transform_iterator<Transform<false>, ListsType::reverse_iterator>;

        using ConstReverseIterator =
            boost::transform_iterator<Transform<true>, ListsType::const_reverse_iterator>;

        Iterator
        begin()
        {
            return Iterator(lists_.begin(), Transform<false>());
        }

        [[nodiscard]] ConstIterator
        begin() const
        {
            return ConstIterator(lists_.cbegin(), Transform<true>());
        }

        [[nodiscard]] ConstIterator
        cbegin() const
        {
            return ConstIterator(lists_.cbegin(), Transform<true>());
        }

        Iterator
        end()
        {
            return Iterator(lists_.end(), Transform<false>());
        }

        [[nodiscard]] ConstIterator
        end() const
        {
            return ConstIterator(lists_.cend(), Transform<true>());
        }

        [[nodiscard]] ConstIterator
        cend() const
        {
            return ConstIterator(lists_.cend(), Transform<true>());
        }

        ReverseIterator
        rbegin()
        {
            return ReverseIterator(lists_.rbegin(), Transform<false>());
        }

        [[nodiscard]] ConstReverseIterator
        rbegin() const
        {
            return ConstReverseIterator(lists_.crbegin(), Transform<true>());
        }

        [[nodiscard]] ConstReverseIterator
        crbegin() const
        {
            return ConstReverseIterator(lists_.crbegin(), Transform<true>());
        }

        ReverseIterator
        rend()
        {
            return ReverseIterator(lists_.rend(), Transform<false>());
        }

        [[nodiscard]] ConstReverseIterator
        rend() const
        {
            return ConstReverseIterator(lists_.crend(), Transform<true>());
        }

        [[nodiscard]] ConstReverseIterator
        crend() const
        {
            return ConstReverseIterator(lists_.crend(), Transform<true>());
        }

        /**
         * Shuffle each hop list.
         */
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
        ListsType lists_;
        Histogram hist_{};
    } hops;

    /**
     * Returns `true` if the cache is empty.
     */
    [[nodiscard]] bool
    empty() const
    {
        return cache_.empty();
    }

    /**
     * Returns the number of entries in the cache.
     */
    CacheType::SizeType
    size() const
    {
        return cache_.size();
    }

    /**
     * Erase entries whose time has expired.
     */
    void
    expire();

    /**
     * Creates or updates an existing Element based on a new message.
     */
    void
    insert(Endpoint const& ep);

    /**
     * Output statistics.
     */
    void
    onWrite(beast::PropertyStream::Map& map);
};

//------------------------------------------------------------------------------

template <class Allocator>
Livecache<Allocator>::Livecache(ClockType& clock, beast::Journal journal, Allocator alloc)
    : journal_(journal), cache_(clock, alloc), hops(alloc)
{
}

template <class Allocator>
void
Livecache<Allocator>::expire()
{
    std::size_t n(0);
    typename CacheType::TimePoint const expired(
        cache_.clock().now() - tuning::kLiveCacheSecondsToLive);
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
        JLOG(journal_.debug()) << std::left << std::setw(18) << "Livecache expired " << n
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
        ep.hops <= (tuning::kMaxHops + 1),
        "xrpl::peer_finder::Livecache::insert : maximum input hops");
    auto result = cache_.emplace(ep.address, ep);
    Element& e(result.first->second);
    if (result.second)
    {
        hops.insert(e);
        JLOG(journal_.debug()) << std::left << std::setw(18) << "Livecache insert " << ep.address
                               << " at hops " << ep.hops;
        return;
    }
    if (!result.second && (ep.hops > e.endpoint.hops))
    {
        // Drop duplicates at higher hops
        std::size_t const excess(ep.hops - e.endpoint.hops);
        JLOG(journal_.trace()) << std::left << std::setw(18) << "Livecache drop " << ep.address
                               << " at hops +" << excess;
        return;
    }

    cache_.touch(result.first);

    // Address already in the cache so update metadata
    if (ep.hops < e.endpoint.hops)
    {
        hops.reinsert(e, ep.hops);
        JLOG(journal_.debug()) << std::left << std::setw(18) << "Livecache update " << ep.address
                               << " at hops " << ep.hops;
    }
    else
    {
        JLOG(journal_.trace()) << std::left << std::setw(18) << "Livecache refresh " << ep.address
                               << " at hops " << ep.hops;
    }
}

template <class Allocator>
void
Livecache<Allocator>::onWrite(beast::PropertyStream::Map& map)
{
    typename CacheType::TimePoint const expired(
        cache_.clock().now() - tuning::kLiveCacheSecondsToLive);
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
        e.endpoint.hops <= tuning::kMaxHops + 1,
        "xrpl::peer_finder::Livecache::HopsT::insert : maximum input hops");
    // This has security implications without a shuffle
    lists_[e.endpoint.hops].push_front(e);
    ++hist_[e.endpoint.hops];
}

template <class Allocator>
void
Livecache<Allocator>::HopsT::reinsert(Element& e, std::uint32_t numHops)
{
    XRPL_ASSERT(
        numHops <= tuning::kMaxHops + 1,
        "xrpl::peer_finder::Livecache::HopsT::reinsert : maximum hops input");

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

}  // namespace xrpl::peer_finder
