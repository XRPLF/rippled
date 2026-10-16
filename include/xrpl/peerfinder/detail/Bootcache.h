#pragma once

#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/PropertyStream.h>
#include <xrpl/peerfinder/Types.h>
#include <xrpl/peerfinder/detail/Store.h>
#include <xrpl/peerfinder/detail/Tuning.h>

#include <boost/bimap.hpp>
#include <boost/bimap/multiset_of.hpp>
#include <boost/bimap/unordered_set_of.hpp>
#include <boost/iterator/transform_iterator.hpp>

#include <functional>

namespace xrpl::peer_finder {

/**
 * Stores IP addresses useful for gaining initial connections.
 *
 * This is one of the caches that is consulted when additional outgoing
 * connections are needed. Along with the address, each entry has this
 * additional metadata:
 *
 * Valence
 *     A signed integer which represents the number of successful
 *     consecutive connection attempts when positive, and the number of
 *     failed consecutive connection attempts when negative.
 *
 * When choosing addresses from the boot cache for the purpose of
 * establishing outgoing connections, addresses are ranked in decreasing
 * order of high uptime, with valence as the tie breaker.
 */
class Bootcache
{
private:
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

    using LeftT = boost::bimaps::
        unordered_set_of<beast::ip::Endpoint, boost::hash<beast::ip::Endpoint>, std::equal_to<>>;
    using RightT = boost::bimaps::multiset_of<Entry, std::less<>>;
    using MapType = boost::bimap<LeftT, RightT>;
    using value_type = MapType::value_type;

    struct Transform
    {
        using first_argument_type = MapType::right_map::const_iterator::value_type const&;
        using result_type = beast::ip::Endpoint const&;

        explicit Transform() = default;

        beast::ip::Endpoint const&
        operator()(MapType::right_map::const_iterator::value_type const& v) const
        {
            return v.get_left();
        }
    };

private:
    MapType map_;

    Store& store_;
    ClockType& clock_;
    beast::Journal journal_;

    // Time after which we can update the database again
    ClockType::time_point whenUpdate_;

    // Set to true when a database update is needed
    bool needsUpdate_{false};

public:
    static constexpr int kStaticValence = 32;

    using iterator = boost::transform_iterator<Transform, MapType::right_map::const_iterator>;

    using const_iterator = iterator;

    Bootcache(Store& store, ClockType& clock, beast::Journal journal);

    ~Bootcache();

    /**
     * Returns `true` if the cache is empty.
     */
    [[nodiscard]] bool
    empty() const;

    /**
     * Returns the number of entries in the cache.
     */
    [[nodiscard]] MapType::size_type
    size() const;

    /**
     * ip::Endpoint iterators that traverse in decreasing valence.
     */
    /** @{ */
    [[nodiscard]] const_iterator
    begin() const;
    [[nodiscard]] const_iterator
    cbegin() const;
    [[nodiscard]] const_iterator
    end() const;
    [[nodiscard]] const_iterator
    cend() const;
    void
    clear();
    /** @} */

    /**
     * Load the persisted data from the Store into the container.
     */
    void
    load();

    /**
     * Add a newly-learned address to the cache.
     */
    bool
    insert(beast::ip::Endpoint const& endpoint);

    /**
     * Add a staticallyconfigured address to the cache.
     */
    bool
    insertStatic(beast::ip::Endpoint const& endpoint);

    /**
     * Called when an outbound connection handshake completes.
     */
    void
    onSuccess(beast::ip::Endpoint const& endpoint);

    /**
     * Called when an outbound connection attempt fails to handshake.
     */
    void
    onFailure(beast::ip::Endpoint const& endpoint);

    /**
     * Stores the cache in the persistent database on a timer.
     */
    void
    periodicActivity();

    /**
     * Write the cache state to the property stream.
     */
    void
    onWrite(beast::PropertyStream::Map& map);

private:
    void
    prune();
    void
    update();
    void
    checkUpdate();
    void
    flagForUpdate();
};

}  // namespace xrpl::peer_finder
