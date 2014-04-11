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

#ifndef RIPPLE_OVERLAY_PEER_H_INCLUDED
#define RIPPLE_OVERLAY_PEER_H_INCLUDED

#include <boost/asio.hpp>

namespace ripple {

typedef boost::asio::ip::tcp::socket NativeSocketType;

namespace Resource {
class Charge;
class Manager;
}

namespace PeerFinder {
class Manager;
}

class Peers;

/** Represents a peer connection in the overlay.
*/
class Peer : private LeakChecked <Peer>
{
public:
    typedef boost::shared_ptr <Peer> Ptr;

     // DEPRECATED typedefs.
    typedef boost::shared_ptr <Peer> pointer;
    typedef pointer const& ref;

    /** Uniquely identifies a particular connection of a peer
        This works upto a restart of rippled.
    */
    typedef uint32 ShortId;

    virtual void sendPacket (const PackedMessage::pointer& packet, bool onStrand) = 0;

    /** Adjust this peer's load balance based on the type of load imposed.

        @note Formerly named punishPeer
    */
    virtual void charge (Resource::Charge const& fee) = 0;
    static  void charge (boost::weak_ptr <Peer>& peer, Resource::Charge const& fee);

    virtual Json::Value json () = 0;

    virtual bool isInCluster () const = 0;

    virtual std::string getClusterNodeName() const = 0;

    virtual uint256 const& getClosedLedgerHash () const = 0;

    virtual bool hasLedger (uint256 const& hash, uint32 seq) const = 0;

    virtual void getLedger (protocol::TMGetLedger &) = 0;

    virtual void ledgerRange (uint32& minSeq, uint32& maxSeq) const = 0;

    virtual bool hasTxSet (uint256 const& hash) const = 0;

    virtual void setShortId(Peer::ShortId shortId) = 0;

    virtual ShortId getShortId () const = 0;

    virtual const RippleAddress& getNodePublic () const = 0;

    virtual void cycleStatus () = 0;

    virtual bool supportsVersion (int version) = 0;

    virtual bool hasRange (uint32 uMin, uint32 uMax) = 0;

    virtual IP::Endpoint getRemoteAddress() const = 0;

    virtual NativeSocketType& getNativeSocket () = 0;
};

}

#endif
