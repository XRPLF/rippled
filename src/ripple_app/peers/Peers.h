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

#ifndef RIPPLE_PEERS_H_INCLUDED
#define RIPPLE_PEERS_H_INCLUDED

namespace ripple {

namespace PeerFinder {
struct Endpoint;
class Manager;
}

namespace Resource {
class Manager;
}

namespace SiteFiles {
class Manager;
}

//------------------------------------------------------------------------------

/** Manages the set of connected peers. */
class Peers
    : public Stoppable
    , public PropertyStream::Source
{
protected:
    // VFALCO NOTE The requirement of this constructor is an
    //             unfortunate problem with the API for
    //             Stoppable and PropertyStream
    //
    Peers (Stoppable& parent)
        : Stoppable ("Peers", parent)
        , PropertyStream::Source ("peers")
    {

    }

public:
    typedef std::vector <Peer::pointer> PeerSequence;

    static Peers* New (Stoppable& parent,
        Resource::Manager& resourceManager,
            SiteFiles::Manager& siteFiles,
                Resolver& resolver,
                    boost::asio::io_service& io_service,
                        boost::asio::ssl::context& context);

    virtual ~Peers () = 0;

    //==========================================================================
    // NIKB TODO The following group of functions are a private interface
    //           between PeerImp and PeersImp that is, unfortunately, exposed
    //           here as an implementation detail. 
    //           We can fix this having PeerImp have visibility to PeersImp,
    //           which are tightly coupled anyways, without polluting the
    //           public interface exported by either Peer or Peers.
    virtual void peerCreated (Peer* peer) = 0;
    virtual void peerDestroyed (Peer *peer) = 0;
    virtual void track (Peer::ref peer);
    //==========================================================================    

    virtual void accept (bool proxyHandshake,
        boost::shared_ptr <NativeSocketType> const& socket) = 0;

    virtual void connect (IP::Endpoint const& address) = 0;

    // Notification that a peer has connected.
    virtual void onPeerActivated (Peer::ref peer) = 0;

    // Notification that a peer has disconnected.
    virtual void onPeerDisconnect (Peer::ref peer) = 0;

    virtual std::size_t size () = 0;
    virtual Json::Value json () = 0;
    virtual PeerSequence getActivePeers () = 0;

    // Peer 64-bit ID function
    virtual Peer::pointer findPeerByShortID (Peer::ShortId const& id) = 0;

    virtual void addPeer (Peer::Ptr const& peer) = 0;
    virtual void removePeer (Peer::Ptr const& peer) = 0;

    /** Visit every active peer and return a value
        The functor must:
        - Be callable as:
            void operator()(Peer::ref peer);
         - Must have the following typedef:
            typedef void return_type;
         - Be callable as:
            Function::return_type operator()() const;

        @param f the functor to call with every peer
        @returns `f()`

        @note The functor is passed by value!
    */
    template<typename Function>
    typename boost::disable_if <
        boost::is_void <typename Function::return_type>, 
        typename Function::return_type>::type
    foreach(Function f)
    {
        PeerSequence peers (getActivePeers());

        for(PeerSequence::const_iterator i = peers.begin(); i != peers.end(); ++i)
            f (*i);

        return f();
    }

    /** Visit every active peer
        The visitor functor must:
         - Be callable as:
            void operator()(Peer::ref peer);
         - Must have the following typedef:
            typedef void return_type;

        @param f the functor to call with every peer
    */
    template <class Function>
    typename boost::enable_if <
        boost::is_void <typename Function::return_type>, 
        typename Function::return_type>::type
    foreach(Function f)
    {
        PeerSequence peers (getActivePeers());

        for(PeerSequence::const_iterator i = peers.begin(); i != peers.end(); ++i)
            f (*i);
    }
};

/** Sends a message to all peers */
struct send_always
{
    typedef void return_type;

    PackedMessage::pointer const& msg;

    send_always(PackedMessage::pointer const& m)
        : msg(m)
    { }

    void operator()(Peer::ref peer) const
    {
        peer->sendPacket (msg, false);
    }
};

//------------------------------------------------------------------------------

/** Sends a message to match peers */
template <typename Predicate>
struct send_if_pred
{
    typedef void return_type;

	PackedMessage::pointer const& msg; 
	Predicate const& predicate;

	send_if_pred(PackedMessage::pointer const& m, Predicate const& p)
		: msg(m), predicate(p)
	{ }

    void operator()(Peer::ref peer) const
	{
        if (predicate (peer))
            peer->sendPacket (msg, false);
	}
};

/** Helper function to aid in type deduction */
template <typename Predicate>
send_if_pred<Predicate> send_if (
    PackedMessage::pointer const& m,
        Predicate const &f)
{
    return send_if_pred<Predicate>(m, f);
}

//------------------------------------------------------------------------------

/** Sends a message to non-matching peers */
template <typename Predicate>
struct send_if_not_pred
{
    typedef void return_type;

    PackedMessage::pointer const& msg; 
    Predicate const& predicate;

    send_if_not_pred(PackedMessage::pointer const& m, Predicate const& p)
        : msg(m), predicate(p)
    { }

    void operator()(Peer::ref peer) const
    {
        if (!predicate (peer))
            peer->sendPacket (msg, false);
    }
};

/** Helper function to aid in type deduction */
template <typename Predicate>
send_if_not_pred<Predicate> send_if_not (
    PackedMessage::pointer const& m,
        Predicate const &f)
{
    return send_if_not_pred<Predicate>(m, f);
}

//------------------------------------------------------------------------------

/** Select the specific peer */
struct match_peer
{
    Peer const* matchPeer;

    match_peer (Peer const* match = nullptr)
        : matchPeer (match)
    { }

    bool operator() (Peer::ref peer) const
    {
        bassert(peer->isConnected());

        if(matchPeer && (peer.get () == matchPeer))
            return true;

        return false;
    }
};

//------------------------------------------------------------------------------

/** Select all peers (except optional excluded) that are in our cluster */
struct peer_in_cluster
{
    match_peer skipPeer;

    peer_in_cluster (Peer const* skip = nullptr)
        : skipPeer (skip)
    { }

    bool operator() (Peer::ref peer) const
    {
        if (skipPeer (peer))
            return false;

        if (!peer->isInCluster ())
            return false;

        return true;
    }
};

//------------------------------------------------------------------------------

/** Select all peers that are in the specified set */
struct peer_in_set
{
    std::set<Peer::ShortId> const& peerSet;

    peer_in_set (std::set<Peer::ShortId> const& peers)
        : peerSet (peers)
    { }

    bool operator() (Peer::ref peer) const
    {
        bassert(peer->isConnected());

        if (peerSet.count (peer->getShortId ()) == 0)
            return false;

        return true;
    }
};

}

#endif
