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
#include <ripple/basics/CountedObject.h>
#include <ripple/basics/UnorderedContainers.h>
#include <cstdint>
#include <functional>
#include <set>

namespace ripple {

class STTx;

// VFALCO NOTE Are these the flags?? Why aren't we using a packed struct?
// VFALCO TODO convert these macros to int constants
#define SF_RELAYED      0x01    // Has already been relayed to other nodes
// VFALCO NOTE How can both bad and good be set on a hash?
#define SF_BAD          0x02    // Signature/format is bad
#define SF_SIGGOOD      0x04    // Signature is good
#define SF_SAVED        0x08
#define SF_RETRY        0x10    // Transaction can be retried
#define SF_TRUSTED      0x20    // comes from trusted source

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
            : mFlags (0)
        {
        }

        std::set <PeerShortID> const& peekPeers () const
        {
            return mPeers;
        }

        void addPeer (PeerShortID peer)
        {
            if (peer != 0)
                mPeers.insert (peer);
        }

        bool hasPeer (PeerShortID peer) const
        {
            return mPeers.count (peer) > 0;
        }

        int getFlags (void) const
        {
            return mFlags;
        }

        bool hasFlag (int mask) const
        {
            return (mFlags & mask) != 0;
        }

        void setFlags (int flagsToSet)
        {
            mFlags |= flagsToSet;
        }

        void clearFlag (int flagsToClear)
        {
            mFlags &= ~flagsToClear;
        }

        void swapSet (std::set <PeerShortID>& other)
        {
            mPeers.swap (other);
        }

    private:
        int mFlags;
        std::set <PeerShortID> mPeers;
    };

public:
    // VFALCO NOTE this preferred alternative to default parameters makes
    //         behavior clear.
    //
    static inline int getDefaultHoldTime ()
    {
        return 300;
    }

    explicit HashRouter (int entryHoldTimeInSeconds)
        : mHoldTime (entryHoldTimeInSeconds)
    {
    }

    virtual ~HashRouter() = default;

    // VFALCO TODO Replace "Supression" terminology with something more
    // semantically meaningful.
    bool addSuppression (uint256 const& index);

    bool addSuppressionPeer (uint256 const& index, PeerShortID peer);

    bool addSuppressionPeer (uint256 const& index, PeerShortID peer,
                             int& flags);

    bool addSuppressionFlags (uint256 const& index, int flag);

    /** Set the flags on a hash.

        @return `true` if the flags were changed.
    */
    bool setFlags (uint256 const& index, int flags);

    int getFlags (uint256 const& index);

    bool swapSet (uint256 const& index, std::set<PeerShortID>& peers, int flag);

    /**
        Function wrapper that will check the signature status
        of a STTx before calling an expensive signature
        checking function.
    */
    std::function<bool(STTx const&, std::function<bool(STTx const&)>)>
    sigVerify();

private:
    Entry getEntry (uint256 const& );

    Entry& findCreateEntry (uint256 const& , bool& created);

    using MutexType = std::mutex;
    using ScopedLockType = std::lock_guard <MutexType>;
    MutexType mMutex;

    // Stores all suppressed hashes and their expiration time
    hash_map <uint256, Entry> mSuppressionMap;

    // Stores all expiration times and the hashes indexed for them
    std::map< int, std::list<uint256> > mSuppressionTimes;

    int mHoldTime;
};

} // ripple

#endif
