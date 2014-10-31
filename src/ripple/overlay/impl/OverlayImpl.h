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

#include <ripple/app/main/ServerHandler.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/common/Resolver.h>
#include <ripple/common/seconds_clock.h>
#include <ripple/common/UnorderedContainers.h>
#include <ripple/peerfinder/Manager.h>
#include <ripple/resource/api/Manager.h>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/container/flat_map.hpp>
#include <beast/cxx14/memory.h> // <memory>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <unordered_map>

namespace ripple {

class PeerImp;

class OverlayImpl : public Overlay
{
public:
    class Child
    {
    protected:
        OverlayImpl& overlay_;

        explicit
        Child (OverlayImpl& overlay);

        virtual ~Child();

    public:
        virtual void close() = 0;
    };

private:
    using clock_type = std::chrono::steady_clock;
    using socket_type = boost::asio::ip::tcp::socket;
    using error_code = boost::system::error_code;
    using yield_context = boost::asio::yield_context;

    using PeersBySlot = hash_map <PeerFinder::Slot::ptr,
        std::weak_ptr <PeerImp>>;

    using PeerByPublicKey = hash_map<RippleAddress, Peer::ptr>;

    using PeerByShortId = hash_map<Peer::ShortId, Peer::ptr>;

    struct Timer
        : Child
        , std::enable_shared_from_this<Timer>
    {
        boost::asio::basic_waitable_timer <clock_type> timer_;

        explicit
        Timer (OverlayImpl& overlay);

        void
        close() override;

        void
        run();

        void
        on_timer (error_code ec);
    };

    boost::asio::io_service& io_service_;
    boost::optional<boost::asio::io_service::work> work_;
    boost::asio::io_service::strand strand_;

    std::recursive_mutex mutex_; // VFALCO use std::mutex
    std::condition_variable_any cond_;
    std::weak_ptr<Timer> timer_;
    boost::container::flat_map<
        Child*, std::weak_ptr<Child>> list_;

    Setup setup_;
    beast::Journal journal_;
    ServerHandler& serverHandler_;

    Resource::Manager& m_resourceManager;

    std::unique_ptr <PeerFinder::Manager> m_peerFinder;

    /** Associates slots to peers. */
    PeersBySlot m_peers;

    /** Tracks peers by their public key */
    PeerByPublicKey m_publicKeyMap;

    /** Tracks peers by their session ID */
    PeerByShortId m_shortIdMap;

    /** The resolver we use for peer hostnames */
    Resolver& m_resolver;

    /** Monotically increasing identifiers for peers */
    std::atomic <Peer::ShortId> m_nextShortId;

    //--------------------------------------------------------------------------

public:
    OverlayImpl (Setup const& setup, Stoppable& parent,
        ServerHandler& serverHandler, Resource::Manager& resourceManager,
            SiteFiles::Manager& siteFiles,
                beast::File const& pathToDbFileOrDirectory,
                    Resolver& resolver, boost::asio::io_service& io_service);

    ~OverlayImpl ();

    Setup const&
    setup() const
    {
        return setup_;
    }

    ServerHandler&
    serverHandler()
    {
        return serverHandler_;
    }

    void
    onLegacyPeerHello (std::unique_ptr<beast::asio::ssl_bundle>&& ssl_bundle,
        boost::asio::const_buffer buffer,
            boost::asio::ip::tcp::endpoint remote_address) override;

    HTTP::Handler::What
    onMaybeMove (std::unique_ptr <beast::asio::ssl_bundle>&& bundle,
        beast::http::message&& request,
            boost::asio::ip::tcp::endpoint remote_address) override;

    PeerSequence
    getActivePeers() override;

    Peer::ptr
    findPeerByShortID (Peer::ShortId const& id) override;

    Peer::ShortId
    next_id();

    void
    remove (PeerFinder::Slot::ptr const& slot);

    /** Called when a peer has connected successfully
        This is called after the peer handshake has been completed and during
        peer activation. At this point, the peer address and the public key
        are known.
    */
    void
    activate (Peer::ptr const& peer);

    /** A peer is being disconnected
        This is called during the disconnection of a known, activated peer. It
        will not be called for outbound peer connections that don't succeed or
        for connections of peers that are dropped prior to being activated.
    */
    void
    onPeerDisconnect (Peer::ptr const& peer);

private:
    OverlayImpl (OverlayImpl const&) = delete;
    OverlayImpl& operator= (OverlayImpl const&) = delete;

    bool
    isPeerUpgrade (beast::http::message const& request);

    std::shared_ptr<HTTP::Writer>
    makeRedirectResponse (PeerFinder::Slot::ptr const& slot,
        beast::http::message const& request);

    void
    connect (beast::IP::Endpoint const& remote_endpoint) override;

    std::size_t
    size() override;

    Json::Value
    json() override;

    //--------------------------------------------------------------------------

    //
    // Stoppable
    //

    void
    onPrepare() override;

    void
    onStart() override;

    void
    onStop() override;

    void
    onChildrenStopped() override;

    //
    // PropertyStream
    //

    void
    onWrite (beast::PropertyStream::Map& stream);

    //--------------------------------------------------------------------------

    void
    remove (Child& child);

    void
    checkStopped ();

    void
    close();

    void
    addpeer (std::shared_ptr<PeerImp> const& peer);

    void
    autoConnect();

    void
    sendEndpoints();
};

} // ripple

#endif
