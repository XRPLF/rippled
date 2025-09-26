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

#ifndef XRPL_APP_MISC_HASHROUTER_H_INCLUDED
#define XRPL_APP_MISC_HASHROUTER_H_INCLUDED

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/container/aged_unordered_map.h>

#include <optional>
#include <set>

namespace ripple {

enum class HashRouterFlags : std::uint16_t {
    // Public flags
    UNDEFINED = 0x00,
    BAD = 0x02,  // Temporarily bad
    SAVED = 0x04,
    HELD = 0x08,     // Held by LedgerMaster after potential processing failure
    TRUSTED = 0x10,  // Comes from a trusted source

    // Private flags (used internally in apply.cpp)
    // Do not attempt to read, set, or reuse.
    PRIVATE1 = 0x0100,
    PRIVATE2 = 0x0200,
    PRIVATE3 = 0x0400,
    PRIVATE4 = 0x0800,
    PRIVATE5 = 0x1000,
    PRIVATE6 = 0x2000
};

constexpr HashRouterFlags
operator|(HashRouterFlags lhs, HashRouterFlags rhs)
{
    return static_cast<HashRouterFlags>(
        static_cast<std::underlying_type_t<HashRouterFlags>>(lhs) |
        static_cast<std::underlying_type_t<HashRouterFlags>>(rhs));
}

constexpr HashRouterFlags&
operator|=(HashRouterFlags& lhs, HashRouterFlags rhs)
{
    lhs = lhs | rhs;
    return lhs;
}

constexpr HashRouterFlags
operator&(HashRouterFlags lhs, HashRouterFlags rhs)
{
    return static_cast<HashRouterFlags>(
        static_cast<std::underlying_type_t<HashRouterFlags>>(lhs) &
        static_cast<std::underlying_type_t<HashRouterFlags>>(rhs));
}

constexpr HashRouterFlags&
operator&=(HashRouterFlags& lhs, HashRouterFlags rhs)
{
    lhs = lhs & rhs;
    return lhs;
}

constexpr bool
any(HashRouterFlags flags)
{
    return static_cast<std::underlying_type_t<HashRouterFlags>>(flags) != 0;
}

class Config;

/** Routing table for objects identified by hash.

    This table keeps track of which hashes have been received by which peers.
    It is used to manage the routing and broadcasting of messages in the peer
    to peer overlay.
*/
class HashRouter
{
public:
    // The type here *MUST* match the type of Peer::id_t
    using PeerShortID = std::uint32_t;

    /** Structure used to customize @ref HashRouter behavior.
     *
     * Even though these items are configurable, they are undocumented. Don't
     * change them unless there is a good reason, and network-wide coordination
     * to do it.
     *
     * Configuration is processed in setup_HashRouter.
     */
    struct Setup
    {
        /// Default constructor
        explicit Setup() = default;

        using seconds = std::chrono::seconds;

        /** Expiration time for a hash entry
         */
        seconds holdTime{300};

        /** Amount of time required before a relayed item will be relayed again.
         */
        seconds relayTime{30};
    };

private:
    /** An entry in the routing table.
     */
    class Entry : public CountedObject<Entry>
    {
    public:
        Entry()
        {
        }

        void
        addPeer(PeerShortID peer)
        {
            if (peer != 0)
                peers_.insert(peer);
        }

        HashRouterFlags
        getFlags(void) const
        {
            return flags_;
        }

        void
        setFlags(HashRouterFlags flagsToSet)
        {
            flags_ |= flagsToSet;
        }

        /** Return set of peers we've relayed to and reset tracking */
        std::set<PeerShortID>
        releasePeerSet()
        {
            return std::move(peers_);
        }

        /** Return seated relay time point if the message has been relayed */
        std::optional<Stopwatch::time_point>
        relayed() const
        {
            return relayed_;
        }

        /** Determines if this item should be relayed.

            Checks whether the item has been recently relayed.
            If it has, return false. If it has not, update the
            last relay timestamp and return true.
        */
        bool
        shouldRelay(
            Stopwatch::time_point const& now,
            std::chrono::seconds relayTime)
        {
            if (relayed_ && *relayed_ + relayTime > now)
                return false;
            relayed_.emplace(now);
            return true;
        }

        bool
        shouldProcess(Stopwatch::time_point now, std::chrono::seconds interval)
        {
            if (processed_ && ((*processed_ + interval) > now))
                return false;
            processed_.emplace(now);
            return true;
        }

    private:
        HashRouterFlags flags_ = HashRouterFlags::UNDEFINED;
        std::set<PeerShortID> peers_;
        // This could be generalized to a map, if more
        // than one flag needs to expire independently.
        std::optional<Stopwatch::time_point> relayed_;
        std::optional<Stopwatch::time_point> processed_;
    };

public:
    HashRouter(Setup const& setup, Stopwatch& clock)
        : setup_(setup), suppressionMap_(clock)
    {
    }

    HashRouter&
    operator=(HashRouter const&) = delete;

    virtual ~HashRouter() = default;

    // VFALCO TODO Replace "Supression" terminology with something more
    // semantically meaningful.
    void
    addSuppression(uint256 const& key);

    bool
    addSuppressionPeer(uint256 const& key, PeerShortID peer);

    /** Add a suppression peer and get message's relay status.
     * Return pair:
     * element 1: true if the peer is added.
     * element 2: optional is seated to the relay time point or
     * is unseated if has not relayed yet. */
    std::pair<bool, std::optional<Stopwatch::time_point>>
    addSuppressionPeerWithStatus(uint256 const& key, PeerShortID peer);

    bool
    addSuppressionPeer(
        uint256 const& key,
        PeerShortID peer,
        HashRouterFlags& flags);

    // Add a peer suppression and return whether the entry should be processed
    bool
    shouldProcess(
        uint256 const& key,
        PeerShortID peer,
        HashRouterFlags& flags,
        std::chrono::seconds tx_interval);

    /** Set the flags on a hash.

        @return `true` if the flags were changed. `false` if unchanged.
    */
    bool
    setFlags(uint256 const& key, HashRouterFlags flags);

    HashRouterFlags
    getFlags(uint256 const& key);

    /** Determines whether the hashed item should be relayed.

        Effects:

            If the item should be relayed, this function will not
            return a seated optional again until the relay time has expired.
            The internal set of peers will also be reset.

        @return A `std::optional` set of peers which do not need to be
            relayed to. If the result is unseated, the item should
            _not_ be relayed.
    */
    std::optional<std::set<PeerShortID>>
    shouldRelay(uint256 const& key);

private:
    // pair.second indicates whether the entry was created
    std::pair<Entry&, bool>
    emplace(uint256 const&);

    std::mutex mutable mutex_;

    // Configurable parameters
    Setup const setup_;

    // Stores all suppressed hashes and their expiration time
    beast::aged_unordered_map<
        uint256,
        Entry,
        Stopwatch::clock_type,
        hardened_hash<strong_hash>>
        suppressionMap_;
};

HashRouter::Setup
setup_HashRouter(Config const&);

}  // namespace ripple

#endif
