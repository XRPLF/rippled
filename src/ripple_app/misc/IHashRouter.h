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

#ifndef RIPPLE_HASHROUTER_H_INCLUDED
#define RIPPLE_HASHROUTER_H_INCLUDED

namespace ripple {

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
class IHashRouter
{
public:
    // The type here *MUST* match the type of Peer::ShortId
    typedef std::uint32_t PeerShortID;

    // VFALCO NOTE this preferred alternative to default parameters makes
    //         behavior clear.
    //
    static inline int getDefaultHoldTime ()
    {
        return 300;
    }

    // VFALCO TODO rename the parameter to entryHoldTimeInSeconds
    static IHashRouter* New (int holdTime);

    virtual ~IHashRouter () { }

    // VFALCO TODO Replace "Supression" terminology with something more semantically meaningful.
    virtual bool addSuppression (uint256 const& index) = 0;

    virtual bool addSuppressionPeer (uint256 const& index, PeerShortID peer) = 0;

    virtual bool addSuppressionPeer (uint256 const& index, PeerShortID peer, int& flags) = 0;

    virtual bool addSuppressionFlags (uint256 const& index, int flag) = 0;

    /** Set the flags on a hash.

        @return `true` if the flags were changed.
    */
    // VFALCO TODO Rename to setFlags since it works with multiple flags.
    virtual bool setFlag (uint256 const& index, int mask) = 0;

    virtual int getFlags (uint256 const& index) = 0;

    virtual bool swapSet (uint256 const& index, std::set<PeerShortID>& peers, int flag) = 0;

    // VFALCO TODO This appears to be unused!
    //
//    virtual Entry getEntry (uint256 const&) = 0;
};

} // ripple

#endif
