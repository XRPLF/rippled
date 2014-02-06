//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_PEERFINDER_BOOTCACHE_H_INCLUDED
#define RIPPLE_PEERFINDER_BOOTCACHE_H_INCLUDED

namespace ripple {
namespace PeerFinder {

/** Stores IP addresses useful for gaining initial connections.

    This is one of the caches that is consulted when additional outgoing
    connections are needed. Along with the address, each entry has this
    additional metadata:

    Uptime

        The number of seconds that the address has maintained an active
        peer connection, cumulative, without a connection attempt failure.

    Valence

        A signed integer which represents the number of successful
        consecutive connection attempts when positive, and the number of
        failed consecutive connection attempts when negative.

    When choosing addresses from the boot cache for the purpose of
    establishing outgoing connections, addresses are ranked in decreasing
    order of high uptime, with valence as the tie breaker.
*/
class Bootcache
{
public:
    /** An item used for connecting. */
    class Endpoint
    {
    public:
        Endpoint ()
            : m_uptime (0)
            , m_valence (0)
        {
        }

        Endpoint (IPAddress const& address,
            std::chrono::seconds uptime, int valence)
            : m_address (address)
            , m_uptime (uptime)
            , m_valence (valence)
        {
        }

        IPAddress const& address () const
        {
            return m_address;
        }

        std::chrono::seconds uptime () const
        {
            return m_uptime;
        }

        int valence () const
        {
            return m_valence;
        }

    private:
        IPAddress m_address;
        std::chrono::seconds m_uptime;
        int m_valence;
    };

    typedef std::vector <Endpoint> Endpoints;

    //--------------------------------------------------------------------------

    /** An entry in the bootstrap cache. */
    struct Entry
    {
        Entry ()
            : cumulativeUptime (0)
            , sessionUptime (0)
            , connectionValence (0)
            , active (false)
        {
        }

        /** Update the uptime measurement based on the time. */
        void update (clock_type::time_point const& now)
        {
            // Must be active!
            assert (active);
            // Clock must be monotonically increasing
            assert (now >= whenActive);
            // Remove the uptime we added earlier in the
            // session and add back in the new uptime measurement.
            auto const uptime (now - whenActive);
            cumulativeUptime -= sessionUptime;
            cumulativeUptime += uptime;
            sessionUptime = uptime;
        }

        /** Our cumulative uptime with this address with no failures. */
        std::chrono::seconds cumulativeUptime;

        /** Amount of uptime from the current session (if any). */
        std::chrono::seconds sessionUptime;

        /** Number of consecutive connection successes or failures.
            If the number is positive, indicates the number of
            consecutive successful connection attempts, else the
            absolute value indicates the number of consecutive
            connection failures.
        */
        int connectionValence;

        /** `true` if the peer has handshaked and is currently connected. */
        bool active;

        /** Time when the peer became active. */
        clock_type::time_point whenActive;
    };

    //--------------------------------------------------------------------------

    /* Comparison function for entries.

        1. Sort descending by cumulative uptime
        2. For all uptimes == 0,
            Sort descending by connection successes
        3. For all successes == 0
            Sort ascending by number of failures
    */
    struct Less
    {
        template <typename Iter>
        bool operator() (
            Iter const& lhs_iter, Iter const& rhs_iter)
        {
            Entry const& lhs (lhs_iter->second);
            Entry const& rhs (rhs_iter->second);
            // Higher cumulative uptime always wins
            if (lhs.cumulativeUptime > rhs.cumulativeUptime)
                return true;
            else if (lhs.cumulativeUptime <= rhs.cumulativeUptime
                     && rhs.cumulativeUptime.count() != 0)
                return false;
            // At this point both uptimes will be zero
            consistency_check (lhs.cumulativeUptime.count() == 0 &&
                               rhs.cumulativeUptime.count() == 0);
            if (lhs.connectionValence > rhs.connectionValence)
                return true;
            return false;
        }
    };

    //--------------------------------------------------------------------------

    typedef boost::unordered_map <IPAddress, Entry> Entries;

    typedef std::vector <Entries::iterator> SortedEntries;

    Store& m_store;
    clock_type& m_clock;
    Journal m_journal;
    Entries m_entries;

    // Time after which we can update the database again
    clock_type::time_point m_whenUpdate;

    // Set to true when a database update is needed
    bool m_needsUpdate;

    Bootcache (
        Store& store,
        clock_type& clock,
        Journal journal)
        : m_store (store)
        , m_clock (clock)
        , m_journal (journal)
        , m_whenUpdate (m_clock.now ())
    {
    }

    ~Bootcache ()
    {
        update ();
    }

    //--------------------------------------------------------------------------

    /** Load the persisted data from the Store into the container. */
    void load ()
    {
        typedef std::vector <Store::SavedBootstrapAddress> StoredData;
        StoredData const list (m_store.loadBootstrapCache ());

        std::size_t count (0);

        for (StoredData::const_iterator iter (list.begin());
            iter != list.end(); ++iter)
        {
            std::pair <Entries::iterator, bool> result (
                m_entries.emplace (boost::unordered::piecewise_construct,
                    boost::make_tuple (iter->address), boost::make_tuple ()));
            if (result.second)
            {
                ++count;
                Entry& entry (result.first->second);
                entry.cumulativeUptime = iter->cumulativeUptime;
                entry.connectionValence = iter->connectionValence;
            }
            else
            {
                if (m_journal.error) m_journal.error << leftw (18) <<
                    "Bootcache discard " << iter->address;
            }
        }

        if (count > 0)
        {
            if (m_journal.info) m_journal.info << leftw (18) <<
                "Bootcache loaded " << count <<
                    ((count > 1) ? " addresses" : " address");
        }

        prune ();
    }

    /** Returns the number of entries in the cache. */
    std::size_t size () const
    {
        return m_entries.size();
    }

    /** Returns up to the specified number of the best addresses. */
    IPAddresses getAddresses (int n)
    {
        SortedEntries const list (sort());
        IPAddresses result;
        int count (0);
        result.reserve (n);
        for (SortedEntries::const_iterator iter (
            list.begin()); ++count <= n && iter != list.end(); ++iter)
            result.push_back ((*iter)->first);
        consistency_check (result.size() <= n);
        return result;
    }

    /** Returns all entries in the cache. */
    Endpoints fetch () const
    {
        Endpoints result;
        result.reserve (m_entries.size ());
        for (Entries::const_iterator iter (m_entries.begin ());
            iter != m_entries.end (); ++iter)
            result.emplace_back (iter->first,
                iter->second.cumulativeUptime,
                    iter->second.connectionValence);
        return result;
    }

    /** Called periodically to perform time related tasks. */
    void periodicActivity ()
    {
        checkUpdate();
    }

    /** Called when an address is learned from a message. */
    bool insert (IPAddress const& address)
    {
        std::pair <Entries::iterator, bool> result (
            m_entries.emplace (boost::unordered::piecewise_construct,
                boost::make_tuple (address), boost::make_tuple ()));
        if (result.second)
        {
            if (m_journal.trace) m_journal.trace << leftw (18) <<
                "Bootcache insert " << address;
            prune ();
            flagForUpdate();
        }
        return result.second;
    }

    /** Called when an outbound connection attempt fails to handshake. */
    void onConnectionFailure (IPAddress const& address)
    {
        Entries::iterator iter (m_entries.find (address));
        // If the entry doesn't already exist don't bother remembering
        // it since the connection failed.
        //
        if (iter == m_entries.end())
            return;
        Entry& entry (iter->second);
        // Reset cumulative uptime to zero. We are aggressive
        // with resetting uptime to prevent the entire network
        // from settling on just a handful of addresses.
        //
        entry.cumulativeUptime = std::chrono::seconds (0);
        entry.sessionUptime = std::chrono::seconds (0);
        // Increment the number of consecutive failures.
        if (entry.connectionValence > 0)
            entry.connectionValence = 0;
        --entry.connectionValence;
        int const count (std::abs (entry.connectionValence));
        if (m_journal.debug) m_journal.debug << leftw (18) <<
            "Bootcache failed " << address <<
            " with " << count <<
            ((count > 1) ? " attempts" : " attempt");
        flagForUpdate();
    }

    /** Called when an outbound connection handshake completes. */
    void onConnectionHandshake (IPAddress const& address,
        HandshakeAction action)
    {
        std::pair <Entries::iterator, bool> result (
            m_entries.emplace (boost::unordered::piecewise_construct,
                boost::make_tuple (address), boost::make_tuple ()));
        Entry& entry (result.first->second);
        // Can't already be active!
        consistency_check (! entry.active);
        // Reset session uptime
        entry.sessionUptime = std::chrono::seconds (0);
        // Count this as a connection success
        if (entry.connectionValence < 0)
            entry.connectionValence = 0;
        ++entry.connectionValence;
        // Update active status
        if (action == doActivate)
        {
            entry.active = true;
            entry.whenActive = m_clock.now();
        }
        else
        {
            entry.active = false;
        }
        // Prune if we made the container larger
        if (result.second)
            prune ();
        flagForUpdate();
        if (m_journal.info) m_journal.info << leftw (18) <<
            "Bootcache connect " << address <<
            " with " << entry.connectionValence <<
            ((entry.connectionValence > 1) ? " successes" : " success");
    }

    /** Called periodically while the peer is active. */
    //
    // VFALCO TODO Can't we just put the active ones into an intrusive list
    //             and update their uptime in periodicActivity() now that
    //             we have the m_clock member?
    //
    void onConnectionActive (IPAddress const& address)
    {
        std::pair <Entries::iterator, bool> result (
            m_entries.emplace (boost::unordered::piecewise_construct,
                boost::make_tuple (address), boost::make_tuple ()));
        // Must exist!
        consistency_check (! result.second);
        Entry& entry (result.first->second);
        entry.update (m_clock.now());
        flagForUpdate();
    }

    template <class Rep, class Period>
    static std::string uptime_phrase (
        std::chrono::duration <Rep, Period> const& elapsed)
    {
        if (elapsed.count() > 0)
        {
            std::stringstream ss;
            ss << " with " << elapsed << " uptime";
            return ss.str();
        }
        return std::string ();
    }
    /** Called when an active outbound connection closes. */
    void onConnectionClosed (IPAddress const& address)
    {
        Entries::iterator iter (m_entries.find (address));
        // Must exist!
        consistency_check (iter != m_entries.end());
        Entry& entry (iter->second);
        // Must be active!
        consistency_check (entry.active);
        if (m_journal.trace) m_journal.trace << leftw (18) <<
            "Bootcache close " << address <<
            uptime_phrase (entry.cumulativeUptime);
        entry.update (m_clock.now());
        entry.sessionUptime = std::chrono::seconds (0);
        entry.active = false;
        flagForUpdate();
    }

    //--------------------------------------------------------------------------
    //
    // Diagnostics
    //
    //--------------------------------------------------------------------------

    void onWrite (PropertyStream::Map& map)
    {
        map ["entries"]      = uint32(m_entries.size());
    }

    static std::string valenceString (int valence)
    {
        std::stringstream ss;
        if (valence >= 0)
            ss << '+';
        ss << valence;
        return ss.str();
    }

    void dump (Journal::ScopedStream const& ss) const
    {
        std::vector <Entries::const_iterator> const list (csort ());
        ss << std::endl << std::endl <<
            "Bootcache (size " << list.size() << ")";
        for (std::vector <Entries::const_iterator>::const_iterator iter (
            list.begin()); iter != list.end(); ++iter)
        {
            ss << std::endl <<
                (*iter)->first << ", " <<
                (*iter)->second.cumulativeUptime << ", "
                << valenceString ((*iter)->second.connectionValence);
            if ((*iter)->second.active)
                ss <<
                    ", active";
        }
    }

    //--------------------------------------------------------------------------
private:
    // Returns a vector of entry iterators sorted by descending score
    std::vector <Entries::const_iterator> csort () const
    {
        std::vector <Entries::const_iterator> result;
        result.reserve (m_entries.size());
        for (Entries::const_iterator iter (m_entries.begin());
            iter != m_entries.end(); ++iter)
            result.push_back (iter);
        std::random_shuffle (result.begin(), result.end());
        // should be std::unstable_sort (c++11)
        std::sort (result.begin(), result.end(), Less());
        return result;
    }

    // Returns a vector of entry iterators sorted by descending score
    std::vector <Entries::iterator> sort ()
    {
        std::vector <Entries::iterator> result;
        result.reserve (m_entries.size());
        for (Entries::iterator iter (m_entries.begin());
            iter != m_entries.end(); ++iter)
            result.push_back (iter);
        std::random_shuffle (result.begin(), result.end());
        // should be std::unstable_sort (c++11)
        std::sort (result.begin(), result.end(), Less());
        return result;
    }

    // Checks the cache size and prunes if its over the limit.
    void prune ()
    {
        if (m_entries.size() <= Tuning::bootcacheSize)
            return;
        // Calculate the amount to remove
        int count ((m_entries.size() *
            Tuning::bootcachePrunePercent) / 100);
        int pruned (0);
        SortedEntries list (sort ());
        for (SortedEntries::const_reverse_iterator iter (
            list.rbegin()); count > 0 && iter != list.rend(); ++iter)
        {
            Entry& entry ((*iter)->second);
            // skip active entries
            if (entry.active)
                continue;
            if (m_journal.trace) m_journal.trace << leftw (18) <<
                "Bootcache pruned" << (*iter)->first <<
                uptime_phrase (entry.cumulativeUptime) <<
                " and valence " << entry.connectionValence;
            m_entries.erase (*iter);
            --count;
            ++pruned;
        }

        if (m_journal.debug) m_journal.debug << leftw (18) <<
            "Bootcache pruned " << pruned << " entries total";
    }

    // Updates the Store with the current set of entries if needed.
    void update ()
    {
        if (! m_needsUpdate)
            return;
        typedef std::vector <Store::SavedBootstrapAddress> StoredData;
        StoredData list;
        list.reserve (m_entries.size());
        for (Entries::const_iterator iter (m_entries.begin());
            iter != m_entries.end(); ++iter)
        {
            Store::SavedBootstrapAddress entry;
            entry.address = iter->first;
            entry.cumulativeUptime = iter->second.cumulativeUptime;
            entry.connectionValence = iter->second.connectionValence;
            list.push_back (entry);
        }
        m_store.updateBootstrapCache (list);
        // Reset the flag and cooldown timer
        m_needsUpdate = false;
        m_whenUpdate = m_clock.now() + Tuning::bootcacheCooldownTime;
    }

    // Checks the clock and calls update if we are off the cooldown.
    void checkUpdate ()
    {
        if (m_needsUpdate && m_whenUpdate < m_clock.now())
            update ();
    }

    // Called when changes to an entry will affect the Store.
    void flagForUpdate ()
    {
        m_needsUpdate = true;
        checkUpdate ();
    }
};

}
}

#endif
