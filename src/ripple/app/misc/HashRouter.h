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

#include <ripple/basics/base_uint.h>
#include <ripple/basics/chrono.h>
#include <ripple/basics/CountedObject.h>
#include <ripple/basics/UnorderedContainers.h>
#include <beast/container/aged_unordered_map.h>

namespace ripple {

// VFALCO NOTE Are these the flags?? Why aren't we using a packed struct?
// VFALCO TODO convert these macros to int constants
#define SF_RELAYED      0x01    // Has already been relayed to other nodes
// VFALCO NOTE How can both bad and good be set on a hash?
#define SF_BAD          0x02    // Temporarily bad
#define SF_SAVED        0x04
#define SF_RETRY        0x08    // Transaction can be retried
#define SF_TRUSTED      0x10    // comes from trusted source
// Private flags, used internally in apply.cpp.
// Do not attempt to read, set, or reuse.
#define SF_PRIVATE1     0x100
#define SF_PRIVATE2     0x200
#define SF_PRIVATE3     0x400
#define SF_PRIVATE4     0x800

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
    class Entry : public CountedObject <Entry>
    {
    public:
        static char const* getCountedObjectName () { return "HashRouterEntry"; }

        Entry ()
            : flags_ (0)
        {
        }

        std::set <PeerShortID> const& peekPeers () const
        {
            return peers_;
        }

        void addPeer (PeerShortID peer)
        {
            if (peer != 0)
                peers_.insert (peer);
        }

        bool hasPeer (PeerShortID peer) const
        {
            return peers_.count (peer) > 0;
        }

        int getFlags (void) const
        {
            return flags_;
        }

        bool hasFlag (int mask) const
        {
            return (flags_ & mask) != 0;
        }

        void setFlags (int flagsToSet)
        {
            flags_ |= flagsToSet;
        }

        void clearFlag (int flagsToClear)
        {
            flags_ &= ~flagsToClear;
        }

        void swapSet (std::set <PeerShortID>& other)
        {
            peers_.swap (other);
        }

    private:
        int flags_;
        std::set <PeerShortID> peers_;
    };

public:
    static inline std::chrono::seconds getDefaultHoldTime ()
    {
        using namespace std::chrono;

        return 300s;
    }

    HashRouter (Stopwatch& clock, std::chrono::seconds entryHoldTimeInSeconds)
        : mSuppressionMap(clock)
        , mHoldTime (entryHoldTimeInSeconds)
    {
    }

    HashRouter& operator= (HashRouter const&) = delete;

    virtual ~HashRouter() = default;

    // VFALCO TODO Replace "Supression" terminology with something more
    // semantically meaningful.
    void addSuppression(uint256 const& key);

    bool addSuppressionPeer (uint256 const& key, PeerShortID peer);

    bool addSuppressionPeer (uint256 const& key, PeerShortID peer,
                             int& flags);

    /** Set the flags on a hash.

        @return `true` if the flags were changed. `false` if unchanged.
    */
    bool setFlags (uint256 const& key, int flags);

    int getFlags (uint256 const& key);

    bool swapSet (uint256 const& key, std::set<PeerShortID>& peers, int flag);

private:
    // pair.second indicates whether the entry was created
    std::pair<Entry&, bool> emplace (uint256 const&);

    std::mutex mutable mMutex;

    // Stores all suppressed hashes and their expiration time
    beast::aged_unordered_map<uint256, Entry, Stopwatch::clock_type,
        hardened_hash<strong_hash>> mSuppressionMap;

    std::chrono::seconds const mHoldTime;
};

} // ripple

#endif
