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

#ifndef RIPPLE_APP_MISC_HASHROUTER_H_INCLUDED
#define RIPPLE_APP_MISC_HASHROUTER_H_INCLUDED

#include <ripple/basics/CountedObject.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/basics/base_uint.h>
#include <ripple/basics/chrono.h>
#include <ripple/beast/container/aged_unordered_map.h>

#include <optional>

namespace ripple {

// TODO convert these macros to int constants or an enum
#define SF_BAD 0x02  // Temporarily bad
#define SF_SAVED 0x04
#define SF_TRUSTED 0x10  // comes from trusted source

// Private flags, used internally in apply.cpp.
// Do not attempt to read, set, or reuse.
#define SF_PRIVATE1 0x0100
#define SF_PRIVATE2 0x0200
#define SF_PRIVATE3 0x0400
#define SF_PRIVATE4 0x0800
#define SF_PRIVATE5 0x1000
#define SF_PRIVATE6 0x2000

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

        int
        getFlags(void) const
        {
            return flags_;
        }

        void
        setFlags(int flagsToSet)
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
            std::chrono::seconds holdTime)
        {
            if (relayed_ && *relayed_ + holdTime > now)
                return false;
            relayed_.emplace(now);
            return true;
        }

        /** Determines if this item should be recovered from the open ledger.

            Counts the number of times the item has been recovered.
            Every `limit` times the function is called, return false.
            Else return true.

            @note The limit must be > 0
        */
        bool
        shouldRecover(std::uint32_t limit)
        {
            return ++recoveries_ % limit != 0;
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
        int flags_ = 0;
        std::set<PeerShortID> peers_;
        // This could be generalized to a map, if more
        // than one flag needs to expire independently.
        std::optional<Stopwatch::time_point> relayed_;
        std::optional<Stopwatch::time_point> processed_;
        std::uint32_t recoveries_ = 0;
    };

public:
    static inline std::chrono::seconds
    getDefaultHoldTime()
    {
        using namespace std::chrono;

        return 300s;
    }

    static inline std::uint32_t
    getDefaultRecoverLimit()
    {
        return 1;
    }

    HashRouter(
        Stopwatch& clock,
        std::chrono::seconds entryHoldTimeInSeconds,
        std::uint32_t recoverLimit)
        : suppressionMap_(clock)
        , holdTime_(entryHoldTimeInSeconds)
        , recoverLimit_(recoverLimit + 1u)
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
    addSuppressionPeer(uint256 const& key, PeerShortID peer, int& flags);

    // Add a peer suppression and return whether the entry should be processed
    bool
    shouldProcess(
        uint256 const& key,
        PeerShortID peer,
        int& flags,
        std::chrono::seconds tx_interval);

    /** Set the flags on a hash.

        @return `true` if the flags were changed. `false` if unchanged.
    */
    bool
    setFlags(uint256 const& key, int flags);

    int
    getFlags(uint256 const& key);

    /** Determines whether the hashed item should be relayed.

        Effects:

            If the item should be relayed, this function will not
            return `true` again until the hold time has expired.
            The internal set of peers will also be reset.

        @return A `std::optional` set of peers which do not need to be
            relayed to. If the result is uninitialized, the item should
            _not_ be relayed.
    */
    std::optional<std::set<PeerShortID>>
    shouldRelay(uint256 const& key);

    /** Determines whether the hashed item should be recovered
        from the open ledger into the next open ledger or the transaction
        queue.

        @return `bool` indicates whether the item should be recovered
    */
    bool
    shouldRecover(uint256 const& key);

private:
    // pair.second indicates whether the entry was created
    std::pair<Entry&, bool>
    emplace(uint256 const&);

    std::mutex mutable mutex_;

    // Stores all suppressed hashes and their expiration time
    beast::aged_unordered_map<
        uint256,
        Entry,
        Stopwatch::clock_type,
        hardened_hash<strong_hash>>
        suppressionMap_;

    std::chrono::seconds const holdTime_;

    std::uint32_t const recoverLimit_;
};

}  // namespace ripple

#endif
