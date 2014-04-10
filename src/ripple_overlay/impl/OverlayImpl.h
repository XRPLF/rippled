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

#ifndef RIPPLE_OVERLAY_OVERLAYIMPL_H_INCLUDED
#define RIPPLE_OVERLAY_OVERLAYIMPL_H_INCLUDED

#include "../api/Overlay.h"

#include "../../ripple/common/Resolver.h"
#include "../../ripple/common/seconds_clock.h"
#include "../../ripple/peerfinder/api/Callback.h"
#include "../../ripple/peerfinder/api/Manager.h"
#include "../../ripple/resource/api/Manager.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/unordered_map.hpp>

#include "../../beast/beast/cxx14/memory.h" // <memory>
#include <cassert>
#include <condition_variable>
#include <mutex>
#include <unordered_map>

namespace ripple {

class PeerDoor;
class PeerImp;

class OverlayImpl
    : public Overlay
    , public PeerFinder::Callback
{    
public:
    typedef boost::asio::ip::tcp::socket socket_type;

    typedef std::unordered_map <PeerFinder::Slot::ptr,
        boost::weak_ptr <PeerImp>> PeersBySlot;

    typedef boost::unordered_map <
        RippleAddress, Peer::ptr> PeerByPublicKey;

    typedef std::unordered_map <
        Peer::ShortId, Peer::ptr> PeerByShortId;

    std::recursive_mutex m_mutex;

    // Blocks us until dependent objects have been destroyed
    std::condition_variable_any m_cond;

    // Number of dependencies that must be destroyed before we can stop
    std::size_t m_child_count;

    beast::Journal m_journal;
    Resource::Manager& m_resourceManager;

    std::unique_ptr <PeerFinder::Manager> m_peerFinder;

    boost::asio::io_service& m_io_service;
    boost::asio::ssl::context& m_ssl_context;

    /** Associates slots to peers. */
    PeersBySlot m_peers;

    /** Tracks peers by their public key */
    PeerByPublicKey m_publicKeyMap;

    /** Tracks peers by their session ID */
    PeerByShortId m_shortIdMap;

    /** The peer door for regular SSL connections */
    std::unique_ptr <PeerDoor> m_doorDirect;

    /** The peer door for proxy connections */
    std::unique_ptr <PeerDoor> m_doorProxy;

    /** The resolver we use for peer hostnames */
    Resolver& m_resolver;

    /** Monotically increasing identifiers for peers */
    beast::Atomic <Peer::ShortId> m_nextShortId;

    //--------------------------------------------------------------------------

    OverlayImpl (Stoppable& parent,
        Resource::Manager& resourceManager,
        SiteFiles::Manager& siteFiles,
        beast::File const& pathToDbFileOrDirectory,
        Resolver& resolver,
        boost::asio::io_service& io_service,
        boost::asio::ssl::context& ssl_context);

    ~OverlayImpl ();

    OverlayImpl (OverlayImpl const&) = delete;
    OverlayImpl& operator= (OverlayImpl const&) = delete;

    /** Process an incoming connection using the Peer protocol.
        The caller transfers ownership of the socket via rvalue move.
        @param proxyHandshake `true` If a PROXY handshake is required.
        @param socket A socket in the accepted state.
    */
    void
    accept (bool proxyHandshake,
        socket_type&& socket);

    void
    connect (beast::IP::Endpoint const& remote_endpoint);

    Peer::ShortId
    next_id();

    //--------------------------------------------------------------------------

    void
    check_stopped ();
    
    void
    release ();
    
    void
    remove (PeerFinder::Slot::ptr const& slot);

    //
    // PeerFinder::Callback
    //

    void
    connect (std::vector <beast::IP::Endpoint> const& list);

    void
    activate (PeerFinder::Slot::ptr const& slot);

    void
    send (PeerFinder::Slot::ptr const& slot,
        std::vector <PeerFinder::Endpoint> const& endpoints);

    void
    disconnect (PeerFinder::Slot::ptr const& slot, bool graceful);

    //
    // Stoppable
    //

    void
    onPrepare () override;

    void
    onStart () override;

    /** Close all peer connections.
        If `graceful` is true then active 
        Requirements:
            Caller must hold the mutex.
    */
    void
    close_all (bool graceful);

    void
    onStop () override;

    void
    onChildrenStopped () override;

    //
    // PropertyStream
    //

    void
    onWrite (beast::PropertyStream::Map& stream);

    //--------------------------------------------------------------------------

    /** A peer has connected successfully
        This is called after the peer handshake has been completed and during
        peer activation. At this point, the peer address and the public key
        are known.
    */
    void
    onPeerActivated (Peer::ptr const& peer);

    /** A peer is being disconnected
        This is called during the disconnection of a known, activated peer. It
        will not be called for outbound peer connections that don't succeed or
        for connections of peers that are dropped prior to being activated.
    */
    void
    onPeerDisconnect (Peer::ptr const& peer);

    /** The number of active peers on the network
        Active peers are only those peers that have completed the handshake
        and are running the Ripple protocol.
    */
    std::size_t
    size ();

    // Returns information on verified peers.
    Json::Value
    json ();

    Overlay::PeerSequence
    getActivePeers ();

    Peer::ptr
    findPeerByShortID (Peer::ShortId const& id);
};

} // ripple

#endif
