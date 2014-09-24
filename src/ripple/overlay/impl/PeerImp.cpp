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

#include <ripple/basics/StringUtilities.h>
#include <ripple/overlay/impl/PeerImp.h>
#include <ripple/overlay/impl/Tuning.h>
#include <beast/crypto/base64.h>
#include <beast/http/rfc2616.h>
#include <beast/streams/debug_ostream.h>

namespace ripple {

PeerImp::PeerImp (NativeSocketType&& socket, beast::IP::Endpoint remoteAddress,
    OverlayImpl& overlay, Resource::Manager& resourceManager,
        PeerFinder::Manager& peerFinder, PeerFinder::Slot::ptr const& slot,
            boost::asio::ssl::context& ssl_context, MultiSocket::Flag flags)
    : m_owned_socket (std::move (socket))
    , m_journal (deprecatedLogs().journal("Peer"))
    , m_remoteAddress (remoteAddress)
    , m_resourceManager (resourceManager)
    , m_peerFinder (peerFinder)
    , overlay_ (overlay)
    , m_inbound (true)
    , m_socket (MultiSocket::New (
        m_owned_socket, ssl_context, flags.asBits ()))
    , m_strand (m_owned_socket.get_io_service())
    , m_state (stateConnected)
    , m_minLedger (0)
    , m_maxLedger (0)
    , timer_ (m_owned_socket.get_io_service())
    , m_slot (slot)
    , message_stream_(*this)
{
}

PeerImp::PeerImp (beast::IP::Endpoint remoteAddress,
    boost::asio::io_service& io_service, OverlayImpl& overlay,
        Resource::Manager& resourceManager, PeerFinder::Manager& peerFinder,
            PeerFinder::Slot::ptr const& slot,
                boost::asio::ssl::context& ssl_context, MultiSocket::Flag flags)
    : m_owned_socket (io_service)
    , m_journal (deprecatedLogs().journal("Peer"))
    , m_remoteAddress (remoteAddress)
    , m_resourceManager (resourceManager)
    , m_peerFinder (peerFinder)
    , overlay_ (overlay)
    , m_inbound (false)
    , m_socket (MultiSocket::New (
        io_service, ssl_context, flags.asBits ()))
    , m_strand (io_service)
    , m_state (stateConnecting)
    , m_minLedger (0)
    , m_maxLedger (0)
    , timer_ (io_service)
    , m_slot (slot)
    , message_stream_(*this)
{
}

PeerImp::~PeerImp ()
{
    overlay_.remove (m_slot);
}

void
PeerImp::start ()
{
    if (m_inbound)
        do_accept ();
    else
        do_connect ();
}

void
PeerImp::activate ()
{
    assert (m_state == stateHandshaked);
    m_state = stateActive;
    assert(m_shortId == 0);
    m_shortId = overlay_.next_id();
    overlay_.onPeerActivated(shared_from_this ());
}

void
PeerImp::close (bool graceful)
{
    m_was_canceled = true;
    detach ("stop", graceful);
}

//------------------------------------------------------------------------------

void
PeerImp::send (Message::pointer const& m)
{
    // VFALCO NOTE why call this with null?
    if (! m)
        return;

    if (! m_strand.running_in_this_thread())
    {
        m_strand.post (std::bind (&PeerImp::send, shared_from_this(), m));
        return;
    }

    if (mSendingPacket)
        mSendQ.push_back (m);
    else
        sendForce (m);
}

beast::IP::Endpoint
PeerImp::getRemoteAddress() const
{
    return m_remoteAddress;
}

void
PeerImp::charge (Resource::Charge const& fee)
{
    if ((m_usage.charge (fee) == Resource::drop) && m_usage.disconnect ())
        detach ("resource");
}

//------------------------------------------------------------------------------

Peer::ShortId
PeerImp::getShortId () const
{
    return m_shortId;
}

RippleAddress const&
PeerImp::getNodePublic () const
{
    return m_nodePublicKey;
}

Json::Value
PeerImp::json()
{
    Json::Value ret (Json::objectValue);

    ret["public_key"]   = m_nodePublicKey.ToString ();
    ret["address"]      = m_remoteAddress.to_string();

    if (m_inbound)
        ret["inbound"] = true;

    if (m_clusterNode)
    {
        ret["cluster"] = true;

        if (!m_nodeName.empty ())
            ret["name"] = m_nodeName;
    }

    if (mHello.has_fullversion ())
        ret["version"] = mHello.fullversion ();

    if (mHello.has_protoversion () &&
            (mHello.protoversion () !=
                 BuildInfo::getCurrentProtocol().toPacked ()))
    {
        ret["protocol"] = BuildInfo::Protocol (
            mHello.protoversion ()).toStdString ();
    }

    std::uint32_t minSeq, maxSeq;
    ledgerRange(minSeq, maxSeq);

    if ((minSeq != 0) || (maxSeq != 0))
        ret["complete_ledgers"] = boost::lexical_cast<std::string>(minSeq) +
            " - " + boost::lexical_cast<std::string>(maxSeq);

    if (m_closedLedgerHash != zero)
        ret["ledger"] = to_string (m_closedLedgerHash);

    if (mLastStatus.has_newstatus ())
    {
        switch (mLastStatus.newstatus ())
        {
        case protocol::nsCONNECTING:
            ret["status"] = "connecting";
            break;

        case protocol::nsCONNECTED:
            ret["status"] = "connected";
            break;

        case protocol::nsMONITORING:
            ret["status"] = "monitoring";
            break;

        case protocol::nsVALIDATING:
            ret["status"] = "validating";
            break;

        case protocol::nsSHUTTING:
            ret["status"] = "shutting";
            break;

        default:
            // FIXME: do we really want this?
            m_journal.warning <<
                "Unknown status: " << mLastStatus.newstatus ();
        }
    }

    return ret;
}

bool
PeerImp::isInCluster () const
{
    return m_clusterNode;
}

std::string const&
PeerImp::getClusterNodeName() const
{
    return m_nodeName;
}

//------------------------------------------------------------------------------

uint256 const&
PeerImp::getClosedLedgerHash () const
{
    return m_closedLedgerHash;
}

bool
PeerImp::hasLedger (uint256 const& hash, std::uint32_t seq) const
{
    std::lock_guard<std::mutex> sl(m_recentLock);
    if ((seq != 0) && (seq >= m_minLedger) && (seq <= m_maxLedger))
        return true;
    return std::find (m_recentLedgers.begin(),
        m_recentLedgers.end(), hash) != m_recentLedgers.end();
}

void
PeerImp::ledgerRange (std::uint32_t& minSeq,
    std::uint32_t& maxSeq) const
{
    std::lock_guard<std::mutex> sl(m_recentLock);

    minSeq = m_minLedger;
    maxSeq = m_maxLedger;
}

bool
PeerImp::hasTxSet (uint256 const& hash) const
{
    std::lock_guard<std::mutex> sl(m_recentLock);
    return std::find (m_recentTxSets.begin(),
        m_recentTxSets.end(), hash) != m_recentTxSets.end();
}

void
PeerImp::cycleStatus ()
{
    m_previousLedgerHash = m_closedLedgerHash;
    m_closedLedgerHash.zero ();
}

bool
PeerImp::supportsVersion (int version)
{
    return mHello.has_protoversion () && (mHello.protoversion () >= version);
}

bool
PeerImp::hasRange (std::uint32_t uMin, std::uint32_t uMax)
{
    return (uMin >= m_minLedger) && (uMax <= m_maxLedger);
}

//------------------------------------------------------------------------------

void
PeerImp::on_shutdown (error_code ec)
{
    // Report ec?

    // VFALCO TODO This might not be right
    detach ("on_shutdown");
}

// client role

void
PeerImp::do_connect ()
{
    m_journal.info <<
        "Connecting to " << m_remoteAddress;

    m_usage = m_resourceManager.newOutboundEndpoint (m_remoteAddress);

    if (m_usage.disconnect ())
    {
        // VFALCO Why are we charging an outbound connection?
        detach ("do_connect");
        return;
    }

    error_code ec;
    timer_.expires_from_now (nodeVerifySeconds, ec);
    timer_.async_wait (m_strand.wrap (std::bind (&PeerImp::handleVerifyTimer,
        shared_from_this (), beast::asio::placeholders::error)));
    if (ec)
    {
        m_journal.error << "Failed to set verify timer.";
        detach ("do_connect");
        return;
    }

    m_socket->next_layer <NativeSocketType>().async_connect (
        beast::IPAddressConversion::to_asio_endpoint (m_remoteAddress),
            m_strand.wrap (std::bind (&PeerImp::on_connect,
                shared_from_this (), beast::asio::placeholders::error)));
}

void
PeerImp::on_connect (error_code ec)
{
    if (m_detaching || ec == boost::asio::error::operation_aborted)
        return;

    NativeSocketType::endpoint_type local_endpoint;
    if (! ec)
        local_endpoint = m_socket->this_layer <
            NativeSocketType> ().local_endpoint (ec);

    if (ec)
    {
        m_journal.error <<
            "Connect to " << m_remoteAddress <<
            " failed: " << ec.message();
        detach ("hc");
        return;
    }

    assert (m_state == stateConnecting);
    m_state = stateConnected;

    m_peerFinder.on_connected (m_slot,
        beast::IPAddressConversion::from_asio (local_endpoint));

    m_socket->set_verify_mode (boost::asio::ssl::verify_none);

    m_socket->async_handshake (boost::asio::ssl::stream_base::client,
        m_strand.wrap (std::bind (&PeerImp::on_connect_ssl,
            shared_from_this(), beast::asio::placeholders::error)));
}

beast::http::message
PeerImp::make_request (protocol::TMHello const& hello)
{
    assert (! m_inbound);
    beast::http::message m;
    m.method (beast::http::method_t::http_get);
    m.url ("/");
    m.version (1, 1);
    m.headers.append ("Remote-Address", m_remoteAddress.to_string());
    m.headers.append ("Upgrade",
        std::string("Ripple/")+BuildInfo::getCurrentProtocol().toStdString());
    m.headers.append ("Connection", "Upgrade");
    m.headers.append ("Connect-As", "Peer");
    m.headers.append ("Accept-Encoding", "identity, snappy");
    //m.headers.append ("X-Try-IPs", "192.168.0.1:51234");
    //m.headers.append ("X-Try-IPs", "208.239.114.74:51234");
    //m.headers.append ("A", "BC");
    //m.headers.append ("Content-Length", "0");
    append_hello (m, hello);
    return m;
}

// Called when ssl handshake complets on an outbound connection
void
PeerImp::on_connect_ssl (error_code ec)
{
    if (m_detaching || ec == boost::asio::error::operation_aborted)
        return;

    if (ec)
    {
        m_journal.info <<
            "on_connect_ssl: " << ec.message();
        detach("on_connect_ssl");
        return;
    }

    if (! overlay_.setup().use_handshake)
        return do_protocol_start();

    http_handshake_ = true;

    auto const result = build_hello();
    if (! result.second)
    {
        m_journal.error <<
            "on_connect_ssl: build_hello failed";
        detach("on_connect_ssl");
        return;
    }
    beast::http::message req = make_request (result.first);
    beast::http::write (write_buffer_, req);

    boost::asio::async_write (*m_socket, write_buffer_.data(),
        boost::asio::transfer_at_least(1), m_strand.wrap (std::bind (
            &PeerImp::on_write_request, shared_from_this(),
                beast::asio::placeholders::error,
                    beast::asio::placeholders::bytes_transferred)));
}

// Called repeatedly with the http request data
void
PeerImp::on_write_request (error_code ec, std::size_t bytes_transferred)
{
    if (m_detaching || ec == boost::asio::error::operation_aborted)
        return;

    if (ec)
    {
        m_journal.info <<
            "on_write_request: " << ec.message();
        detach("on_write_request");
        return;
    }

    write_buffer_.consume (bytes_transferred);

    if (write_buffer_.size() == 0)
    {
        // done sending request, now read the response
        http_message_ = boost::in_place ();
        http_parser_ = boost::in_place (std::ref(*http_message_), false);
        on_read_response (error_code(), 0);
        return;
    }

    boost::asio::async_write (*m_socket, write_buffer_.data(),
        boost::asio::transfer_at_least(1), m_strand.wrap (std::bind (
            &PeerImp::on_write_request, shared_from_this(),
                beast::asio::placeholders::error,
                    beast::asio::placeholders::bytes_transferred)));
}

// Called repeatedly with the http response data
void
PeerImp::on_read_response (error_code ec, std::size_t bytes_transferred)
{
    if (m_detaching || ec == boost::asio::error::operation_aborted)
        return;

    if (ec == boost::asio::error::eof)
    {
        // remote closed their end
        // VFALCO TODO Clean up the shutdown of the socket
    }

    if (! ec)
    {
        read_buffer_.commit (bytes_transferred);
        bool success;
        std::size_t bytes_consumed;
        std::tie (success, bytes_consumed) = http_parser_->write (
            read_buffer_.data());
        if (success)
            read_buffer_.consume (bytes_consumed);
        else
            ec = http_parser_->error();
    }

    if (ec)
    {
        m_journal.info <<
            "on_read_response: " << ec.message();
        detach("on_read_response");
        return;
    }

    if (! http_parser_->complete())
        return boost::asio::async_read (*m_socket, read_buffer_.prepare (
            Tuning::readBufferBytes), boost::asio::transfer_at_least(1),
                m_strand.wrap (std::bind (&PeerImp::on_read_response,
                    shared_from_this(), beast::asio::placeholders::error,
                        beast::asio::placeholders::bytes_transferred)));

    //
    // TODO Apply response to connection state, then:
    //      - Go into protocol loop, or
    //      - Submit a new request (call on_write_request), or
    //      - Close the connection.
    //
    if (http_message_->status() != 200 || ! http_message_->upgrade())
    {
        m_journal.info <<
            "HTTP Response: " << http_message_->reason() <<
            "(" << http_message_->status() << ")";
        detach("on_read_response");
        return;
    }

    auto const result = parse_hello (*http_message_);

    if (! result.second)
    {
        m_journal.error <<
            "HTTP Response: bad hello credentials";
        // TODO We might want to log the user-agent or other info 
        detach("on_read_response");
        return;
    }

    do_protocol_start();
}

//------------------------------------------------------------------------------

// server role

void PeerImp::do_accept ()
{
    m_journal.info << "Accepted " << m_remoteAddress;

    m_usage = m_resourceManager.newInboundEndpoint (m_remoteAddress);
    if (m_usage.disconnect ())
    {
        detach ("do_accept");
        return;
    }

    m_socket->set_verify_mode (boost::asio::ssl::verify_none);
    m_socket->async_handshake (boost::asio::ssl::stream_base::server,
        m_strand.wrap (std::bind (&PeerImp::on_accept_ssl,
            std::static_pointer_cast <PeerImp> (shared_from_this ()),
                beast::asio::placeholders::error)));
}

void
PeerImp::on_accept_ssl (error_code ec)
{
    if (m_detaching || ec == boost::asio::error::operation_aborted)
        return;

    if (ec)
    {
        m_journal.info <<
            "on_accept_ssl: " << ec.message();
        detach("on_accept_ssl");
        return;
    }

    boost::asio::async_read (*m_socket, read_buffer_.prepare (
        Tuning::readBufferBytes), boost::asio::transfer_at_least(1),
            m_strand.wrap (std::bind (&PeerImp::on_read_detect,
                shared_from_this(), beast::asio::placeholders::error,
                    beast::asio::placeholders::bytes_transferred)));
}

// Called repeatedly with the initial bytes received on the connection
void
PeerImp::on_read_detect (error_code ec, std::size_t bytes_transferred)
{
    if (m_detaching || ec == boost::asio::error::operation_aborted)
        return;

    if (ec)
    {
        m_journal.info <<
            "on_read_detect: " << ec.message();
        detach("on_read_detect");
        return;
    }

    read_buffer_.commit (bytes_transferred);
    peer_protocol_detector detector;
    boost::tribool const is_peer_protocol = detector (read_buffer_.data());

    if (is_peer_protocol)
    {
        do_protocol_start();
        return;
    }

    if (! is_peer_protocol)
    {
        http_handshake_ = true;
        http_message_ = boost::in_place ();
        http_parser_ = boost::in_place (std::ref(*http_message_), true);

        return boost::asio::async_read (*m_socket,  read_buffer_.prepare (
            Tuning::readBufferBytes), boost::asio::transfer_at_least(1),
                m_strand.wrap (std::bind (&PeerImp::on_read_request,
                    shared_from_this(), beast::asio::placeholders::error,
                        beast::asio::placeholders::bytes_transferred)));
    }

    // Need more bytes to figure out the handshake
    boost::asio::async_read (*m_socket, read_buffer_.prepare (
        Tuning::readBufferBytes), boost::asio::transfer_at_least(1),
            m_strand.wrap (std::bind (&PeerImp::on_read_detect,
                shared_from_this(), beast::asio::placeholders::error,
                    beast::asio::placeholders::bytes_transferred)));
}

// Builds the HTTP response given the request.
std::pair <beast::http::message, bool>
PeerImp::make_response (beast::http::message const& req,
    protocol::TMHello const& hello)
{
    std::pair <beast::http::message, bool> result;
    beast::http::message& m = result.first;
    result.second = false;

    m.headers.append ("Server",
        BuildInfo::getFullVersionString());
    m.headers.append ("Remote-Address", m_remoteAddress.to_string());

    if (! req.upgrade())
    {
        m.headers.append ("Content-Length", "0");
        m.status (404);
        m.reason ("Not Found");
        //m.body = "?";
        return result;
    }

    m.status (200);
    m.reason ("OK");
    m.headers.append ("Upgrade", "Ripple/1.2");
    m.headers.append ("Connection", "Upgrade");
    append_hello (m, hello);

    // Trigger the protocol loop after the response is sent
    result.second = true;

    return result;
}

// Called repeatedly with the http request data
void
PeerImp::on_read_request (error_code ec, std::size_t bytes_transferred)
{
    if (m_detaching || ec == boost::asio::error::operation_aborted)
        return;

    if (! ec)
    {
        read_buffer_.commit (bytes_transferred);
        bool success;
        std::size_t bytes_consumed;
        std::tie (success, bytes_consumed) = http_parser_->write (
            read_buffer_.data());
        if (success)
            read_buffer_.consume (bytes_consumed);
        else
            ec = http_parser_->error();
    }

    if (ec)
    {
        if (m_journal.info) m_journal.info <<
            "on_read_request: " << ec.message();
        detach("on_read_request");
        return;
    }

    if (! http_parser_->complete())
        return boost::asio::async_read (*m_socket,  read_buffer_.prepare (
            Tuning::readBufferBytes), boost::asio::transfer_at_least(1),
                m_strand.wrap (std::bind (&PeerImp::on_read_request,
                    shared_from_this(), beast::asio::placeholders::error,
                        beast::asio::placeholders::bytes_transferred)));

    //
    // TODO Apply headers to connection state.
    //
    auto const hello = build_hello();
    if (! hello.second)
    {
        // VFALCO TODO Should we charge the resource endpoint?
        m_journal.error <<
            "on_read_request: build_hello failed";
        detach("on_read_request");
        return;
    }

    bool protocol_start;
    beast::http::message resp;
    std::tie (resp, protocol_start) =
        make_response (*http_message_, hello.first);

    if (! protocol_start)
    {
        // VFALCO TODO Review this
        m_usage.charge (Resource::feeReferenceRPC);
        if (m_usage.disconnect ())
        {
            detach ("on_read_request");
            return;
        }
    }

    beast::http::write (write_buffer_, resp);

    boost::asio::async_write (*m_socket, write_buffer_.data(),
        boost::asio::transfer_at_least(1), m_strand.wrap (std::bind (
            &PeerImp::on_write_response, shared_from_this(),
                beast::asio::placeholders::error,
                    beast::asio::placeholders::bytes_transferred,
                        protocol_start)));
}

// Called repeatedly to send the bytes in the response
void
PeerImp::on_write_response (error_code ec,
    std::size_t bytes_transferred, bool protocol_start)
{
    // cancel_timer();

    if (m_detaching || ec == boost::asio::error::operation_aborted)
        return;

    if (ec)
    {
        m_journal.info <<
            "on_write_response: " << ec.message();
        detach("on_write_response");
        return;
    }

    write_buffer_.consume (bytes_transferred);

    if (write_buffer_.size() > 0)
    {
        boost::asio::async_write (*m_socket, write_buffer_.data(),
            boost::asio::transfer_at_least(1), m_strand.wrap (std::bind (
                &PeerImp::on_write_response, shared_from_this(),
                    beast::asio::placeholders::error,
                        beast::asio::placeholders::bytes_transferred,
                            protocol_start)));
        return;
    }

    if (close_ != Close::none)
    {
        if (m_socket->needs_handshake())
            m_socket->async_shutdown (m_strand.wrap (std::bind (
                &PeerImp::on_shutdown, shared_from_this(),
                    beast::asio::placeholders::error)));
        else
            on_shutdown (error_code{});
        return;
    }

    if (protocol_start)
        return do_protocol_start();

    // Accept another HTTP request
    boost::asio::async_read (*m_socket, read_buffer_.prepare (
        Tuning::readBufferBytes), boost::asio::transfer_at_least(1),
            m_strand.wrap (std::bind (&PeerImp::on_read_detect,
                shared_from_this(), beast::asio::placeholders::error,
                    beast::asio::placeholders::bytes_transferred)));
}

//------------------------------------------------------------------------------

// Protocol logic

// We have an encrypted connection to the peer.
// Have it say who it is so we know to avoid redundant connections.
// Establish that it really who we are talking to by having it sign a
// connection detail. Also need to establish no man in the middle attack
// is in progress.
void
PeerImp::do_protocol_start ()
{
    if (! http_handshake_)
    {
        if (!sendHello ())
        {
            m_journal.error << "Unable to send HELLO to " << m_remoteAddress;
            detach ("hello");
            return;
        }
    }

    on_read_protocol (error_code(), 0);
}

// Called repeatedly with protocol message data
void
PeerImp::on_read_protocol (error_code ec, std::size_t bytes_transferred)
{
    if (m_detaching || ec == boost::asio::error::operation_aborted)
        return;

    if (! ec)
    {
        read_buffer_.commit (bytes_transferred);
        ec = message_stream_.write_one (read_buffer_.data());
        read_buffer_.consume (read_buffer_.size());
    }

    if (ec)
    {
        m_journal.info <<
            "on_read_protocol: " << ec.message();
        detach("on_read_protocol");
        return;
    }

    m_socket->async_read_some (read_buffer_.prepare (Tuning::readBufferBytes),
        m_strand.wrap (std::bind (&PeerImp::on_read_protocol,
            shared_from_this(), beast::asio::placeholders::error,
                beast::asio::placeholders::bytes_transferred)));
}

// Called repeatedly to send protcol message data
void
PeerImp::on_write_protocol (error_code ec, std::size_t bytes_transferred)
{
    // (this function isn't called yet)

    if (m_detaching || ec == boost::asio::error::operation_aborted)
        return;
}

void
PeerImp::handleShutdown (error_code const& ec)
{
    if (m_detaching)
        return;

    if (ec == boost::asio::error::operation_aborted)
        return;

    if (ec)
    {
        m_journal.info << "Shutdown: " << ec.message ();
        detach ("hsd");
        return;
    }
}

void
PeerImp::handleWrite (error_code const& ec, size_t bytes)
{
    if (m_detaching)
        return;

    // Call on IO strand

    mSendingPacket.reset ();

    if (ec == boost::asio::error::operation_aborted)
        return;

    if (m_detaching)
        return;

    if (ec)
    {
        m_journal.info << "Write: " << ec.message ();
        detach ("hw");
        return;
    }

    if (!mSendQ.empty ())
    {
        Message::pointer packet = mSendQ.front ();

        if (packet)
        {
            sendForce (packet);
            mSendQ.pop_front ();
        }
    }
}

void
PeerImp::handleVerifyTimer (error_code const& ec)
{
    if (m_detaching)
        return;

    if (ec == boost::asio::error::operation_aborted)
    {
        // Timer canceled because deadline no longer needed.
    }
    else if (ec)
    {
        m_journal.info << "Peer verify timer error";
    }
    else
    {
        //  m_journal.info << "Verify: Peer failed to verify in time.";

        detach ("hvt");
    }
}

//------------------------------------------------------------------------------
//
// abstract_protocol_handler
//
//------------------------------------------------------------------------------

PeerImp::error_code
PeerImp::on_message_unknown (std::uint16_t type)
{
    error_code ec;
    // TODO
    return ec;
}

PeerImp::error_code
PeerImp::on_message_begin (std::uint16_t type,
    std::shared_ptr <::google::protobuf::Message> const& m)
{
    error_code ec;

#if 0
    beast::debug_ostream log;
    log << m->DebugString();
#endif

    if (type == protocol::mtHELLO &&
        (m_state != stateConnected || http_handshake_))
    {
        m_journal.warning <<
            "Unexpected TMHello";
        ec = invalid_argument_error();
    }
    else if (type != protocol::mtHELLO &&
        (m_state == stateConnected && ! http_handshake_))
    {
        m_journal.warning <<
            "Expected TMHello";
        ec = invalid_argument_error();
    }

    if (! ec)
    {
        load_event_ = getApp().getJobQueue ().getLoadEventAP (
            jtPEER, protocol_message_name(type));
    }

    return ec;
}

void
PeerImp::on_message_end (std::uint16_t,
    std::shared_ptr <::google::protobuf::Message> const&)
{
    load_event_.reset();
}

PeerImp::error_code
PeerImp::on_message (protocol::TMHello const& m)
{
    error_code ec;

    bool bDetach (true);

    timer_.cancel ();

    std::uint32_t const ourTime (getApp().getOPs ().getNetworkTimeNC ());
    std::uint32_t const minTime (ourTime - clockToleranceDeltaSeconds);
    std::uint32_t const maxTime (ourTime + clockToleranceDeltaSeconds);

#ifdef BEAST_DEBUG
    if (m.has_nettime ())
    {
        std::int64_t to = ourTime;
        to -= m.nettime ();
        m_journal.debug <<
            "Connect: time offset " << to;
    }
#endif

    BuildInfo::Protocol protocol (m.protoversion());

    if (m.has_nettime () &&
        ((m.nettime () < minTime) || (m.nettime () > maxTime)))
    {
        if (m.nettime () > maxTime)
        {
            m_journal.info <<
                "Hello: Clock for " << *this <<
                " is off by +" << m.nettime () - ourTime;
        }
        else if (m.nettime () < minTime)
        {
            m_journal.info <<
                "Hello: Clock for " << *this <<
                " is off by -" << ourTime - m.nettime ();
        }
    }
    else if (m.protoversionmin () >
        BuildInfo::getCurrentProtocol().toPacked ())
    {
        std::string reqVersion (
            protocol.toStdString ());

        std::string curVersion (
            BuildInfo::getCurrentProtocol().toStdString ());

        m_journal.info <<
            "Hello: Disconnect: Protocol mismatch [" <<
            "Peer expects " << reqVersion <<
            " and we run " << curVersion << "]";
    }
    else if (! m_nodePublicKey.setNodePublic (m.nodepublic ()))
    {
        m_journal.info <<
            "Hello: Disconnect: Bad node public key.";
    }
    else if (! m_nodePublicKey.verifyNodePublic (
        m_secureCookie, m.nodeproof (), ECDSA::not_strict))
    {
        // Unable to verify they have private key for claimed public key.
        m_journal.info <<
            "Hello: Disconnect: Failed to verify session.";
    }
    else
    {
        // Successful connection.
        m_journal.info <<
            "Hello: Connect: " << m_nodePublicKey.humanNodePublic ();

        if ((protocol != BuildInfo::getCurrentProtocol()) &&
            m_journal.active(beast::Journal::Severity::kInfo))
        {
            m_journal.info <<
                "Peer protocol: " << protocol.toStdString ();
        }

        mHello = m;

        // Determine if this peer belongs to our cluster and get it's name
        m_clusterNode = getApp().getUNL().nodeInCluster (
            m_nodePublicKey, m_nodeName);

        if (m_clusterNode)
            m_journal.info <<
            "Connected to cluster node " << m_nodeName;

        assert (m_state == stateConnected);
        m_state = stateHandshaked;

        m_peerFinder.on_handshake (m_slot, RipplePublicKey (m_nodePublicKey),
            m_clusterNode);

        // XXX Set timer: connection is in grace period to be useful.
        // XXX Set timer: connection idle (idle may vary depending on connection type.)
        if ((mHello.has_ledgerclosed ()) && (
            mHello.ledgerclosed ().size () == (256 / 8)))
        {
            memcpy (m_closedLedgerHash.begin (),
                mHello.ledgerclosed ().data (), 256 / 8);

            if ((mHello.has_ledgerprevious ()) &&
                (mHello.ledgerprevious ().size () == (256 / 8)))
            {
                memcpy (m_previousLedgerHash.begin (),
                    mHello.ledgerprevious ().data (), 256 / 8);
                addLedger (m_previousLedgerHash);
            }
            else
            {
                m_previousLedgerHash.zero ();
            }
        }

        bDetach = false;
    }

    if (bDetach)
    {
        //m_nodePublicKey.clear ();
        //detach ("recvh");

        ec = invalid_argument_error();
    }
    else
    {
        sendGetPeers();
    }

    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMHello> const& m)
{
    return on_message(*m);
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMPing> const& m)
{
    error_code ec;
    if (m->type () == protocol::TMPing::ptPING)
    {
        m->set_type (protocol::TMPing::ptPONG);
        send (std::make_shared<Message> (*m, protocol::mtPING));
    }
    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMProofWork> const& m)
{
    error_code ec;
    if (m->has_response ())
    {
        // this is an answer to a proof of work we requested
        if (m->response ().size () != (256 / 8))
        {
            charge (Resource::feeInvalidRequest);
            return ec;
        }

        uint256 response;
        memcpy (response.begin (), m->response ().data (), 256 / 8);

        // VFALCO TODO Use a dependency injection here
        PowResult r = getApp().getProofOfWorkFactory ().checkProof (
            m->token (), response);

        if (r == powOK)
        {
            // credit peer
            // WRITEME
            return ec;
        }

        // return error message
        // WRITEME
        if (r != powTOOEASY)
        {
            charge (Resource::feeBadProofOfWork);
        }

        return ec;
    }

    if (m->has_result ())
    {
        // this is a reply to a proof of work we sent
        // WRITEME
    }

    if (m->has_target () && m->has_challenge () && m->has_iterations ())
    {
        // this is a challenge
        // WRITEME: Reject from inbound connections

        uint256 challenge, target;

        if ((m->challenge ().size () != (256 / 8)) || (
            m->target ().size () != (256 / 8)))
        {
            charge (Resource::feeInvalidRequest);
            return ec;
        }

        memcpy (challenge.begin (), m->challenge ().data (), 256 / 8);
        memcpy (target.begin (), m->target ().data (), 256 / 8);
        ProofOfWork::pointer pow = std::make_shared<ProofOfWork> (
            m->token (), m->iterations (), challenge, target);

        if (!pow->isValid ())
        {
            charge (Resource::feeInvalidRequest);
            return ec;
        }

#if 0   // Until proof of work is completed, don't do it
        getApp().getJobQueue ().addJob (
            jtPROOFWORK,
            "recvProof->doProof",
            std::bind (&PeerImp::doProofOfWork, std::placeholders::_1,
                        std::weak_ptr <Peer> (shared_from_this ()), pow));
#endif

        return ec;
    }

    m_journal.info << "Received in valid proof of work object from peer";

    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMCluster> const& m)
{
    error_code ec;
    if (!m_clusterNode)
    {
        charge (Resource::feeUnwantedData);
        return ec;
    }

    for (int i = 0; i < m->clusternodes().size(); ++i)
    {
        protocol::TMClusterNode const& node = m->clusternodes(i);

        std::string name;
        if (node.has_nodename())
            name = node.nodename();
        ClusterNodeStatus s(name, node.nodeload(), node.reporttime());

        RippleAddress nodePub;
        nodePub.setNodePublic(node.publickey());

        getApp().getUNL().nodeUpdate(nodePub, s);
    }

    int loadSources = m->loadsources().size();
    if (loadSources != 0)
    {
        Resource::Gossip gossip;
        gossip.items.reserve (loadSources);
        for (int i = 0; i < m->loadsources().size(); ++i)
        {
            protocol::TMLoadSource const& node = m->loadsources (i);
            Resource::Gossip::Item item;
            item.address = beast::IP::Endpoint::from_string (node.name());
            item.balance = node.cost();
            if (item.address != beast::IP::Endpoint())
                gossip.items.push_back(item);
        }
        m_resourceManager.importConsumers (m_nodeName, gossip);
    }

    getApp().getFeeTrack().setClusterFee(getApp().getUNL().getClusterFee());
    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMGetPeers> const& m)
{
    error_code ec;
    // VFALCO TODO This message is now obsolete due to PeerFinder
    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMPeers> const& m)
{
    error_code ec;
    // VFALCO TODO This message is now obsolete due to PeerFinder
    std::vector <beast::IP::Endpoint> list;
    list.reserve (m->nodes().size());
    for (int i = 0; i < m->nodes ().size (); ++i)
    {
        in_addr addr;

        addr.s_addr = m->nodes (i).ipv4 ();

        {
            beast::IP::AddressV4 v4 (ntohl (addr.s_addr));
            beast::IP::Endpoint address (v4, m->nodes (i).ipv4port ());
            list.push_back (address);
        }
    }

    if (! list.empty())
        m_peerFinder.on_legacy_endpoints (list);
    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMEndpoints> const& m)
{
    error_code ec;
    std::vector <PeerFinder::Endpoint> endpoints;

    endpoints.reserve (m->endpoints().size());

    for (int i = 0; i < m->endpoints ().size (); ++i)
    {
        PeerFinder::Endpoint endpoint;
        protocol::TMEndpoint const& tm (m->endpoints(i));

        // hops
        endpoint.hops = tm.hops();

        // ipv4
        if (endpoint.hops > 0)
        {
            in_addr addr;
            addr.s_addr = tm.ipv4().ipv4();
            beast::IP::AddressV4 v4 (ntohl (addr.s_addr));
            endpoint.address = beast::IP::Endpoint (v4, tm.ipv4().ipv4port ());
        }
        else
        {
            // This Endpoint describes the peer we are connected to.
            // We will take the remote address seen on the socket and
            // store that in the IP::Endpoint. If this is the first time,
            // then we'll verify that their listener can receive incoming
            // by performing a connectivity test.
            //
            endpoint.address = m_remoteAddress.at_port (
                tm.ipv4().ipv4port ());
        }

        endpoints.push_back (endpoint);
    }

    if (! endpoints.empty())
        m_peerFinder.on_endpoints (m_slot, endpoints);
    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMTransaction> const& m)
{
    error_code ec;
    Serializer s (m->rawtransaction ());

    try
    {
        SerializerIterator sit (s);
        SerializedTransaction::pointer stx = std::make_shared <
            SerializedTransaction> (std::ref (sit));
        uint256 txID = stx->getTransactionID();

        int flags;

        if (! getApp().getHashRouter ().addSuppressionPeer (
            txID, m_shortId, flags))
        {
            // we have seen this transaction recently
            if (flags & SF_BAD)
            {
                charge (Resource::feeInvalidSignature);
                return ec;
            }

            if (!(flags & SF_RETRY))
                return ec;
        }

        m_journal.debug <<
            "Got transaction from peer " << *this << ": " << txID;

        if (m_clusterNode)
            flags |= SF_TRUSTED | SF_SIGGOOD;

        if (getApp().getJobQueue().getJobCount(jtTRANSACTION) > 100)
            m_journal.info << "Transaction queue is full";
        else if (getApp().getLedgerMaster().getValidatedLedgerAge() > 240)
            m_journal.trace << "No new transactions until synchronized";
        else
            getApp().getJobQueue ().addJob (jtTRANSACTION,
                "recvTransaction->checkTransaction",
                std::bind (
                    &PeerImp::checkTransaction, std::placeholders::_1,
                    flags, stx,
                    std::weak_ptr<Peer> (shared_from_this ())));

    }
    catch (...)
    {
        m_journal.warning << "Transaction invalid: " <<
            s.getHex();
    }
    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMGetLedger> const& m)
{
    error_code ec;
    getApp().getJobQueue().addJob (jtPACK, "recvGetLedger",
        std::bind (&PeerImp::sGetLedger, std::weak_ptr<PeerImp> (
            shared_from_this ()), m));
    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMLedgerData> const& m)
{
    error_code ec;
    protocol::TMLedgerData& packet = *m;

    if (m->nodes ().size () <= 0)
    {
        m_journal.warning << "Ledger/TXset data with no nodes";
        return ec;
    }

    if (m->has_requestcookie ())
    {
        Peer::ptr target = overlay_.findPeerByShortID (m->requestcookie ());

        if (target)
        {
            m->clear_requestcookie ();
            target->send (std::make_shared<Message> (
                packet, protocol::mtLEDGER_DATA));
        }
        else
        {
            m_journal.info << "Unable to route TX/ledger data reply";
            charge (Resource::feeUnwantedData);
        }

        return ec;
    }

    uint256 hash;

    if (m->ledgerhash ().size () != 32)
    {
        m_journal.warning << "TX candidate reply with invalid hash size";
        charge (Resource::feeInvalidRequest);
        return ec;
    }

    memcpy (hash.begin (), m->ledgerhash ().data (), 32);

    if (m->type () == protocol::liTS_CANDIDATE)
    {
        // got data for a candidate transaction set

        getApp().getJobQueue().addJob (jtTXN_DATA, "recvPeerData",
            std::bind (&PeerImp::peerTXData, std::placeholders::_1,
                std::weak_ptr<Peer> (shared_from_this ()),
                    hash, m, m_journal));

        return ec;
    }

    if (!getApp().getInboundLedgers ().gotLedgerData (
        hash, shared_from_this(), m))
    {
        m_journal.trace  << "Got data for unwanted ledger";
        charge (Resource::feeUnwantedData);
    }
    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMProposeSet> const& m)
{
    error_code ec;
    protocol::TMProposeSet& set = *m;

    // VFALCO Magic numbers are bad
    if ((set.closetime() + 180) < getApp().getOPs().getCloseTimeNC())
        return ec;

    // VFALCO Magic numbers are bad
    // Roll this into a validation function
    if (
        (set.currenttxhash ().size () != 32) ||
        (set.nodepubkey ().size () < 28) ||
        (set.signature ().size () < 56) ||
        (set.nodepubkey ().size () > 128) ||
        (set.signature ().size () > 128)
    )
    {
        m_journal.warning << "Received proposal is malformed";
        charge (Resource::feeInvalidSignature);
        return ec;
    }

    if (set.has_previousledger () && (set.previousledger ().size () != 32))
    {
        m_journal.warning << "Received proposal is malformed";
        charge (Resource::feeInvalidRequest);
        return ec;
    }

    uint256 proposeHash, prevLedger;
    memcpy (proposeHash.begin (), set.currenttxhash ().data (), 32);

    if (set.has_previousledger ())
        memcpy (prevLedger.begin (), set.previousledger ().data (), 32);

    uint256 suppression = LedgerProposal::computeSuppressionID (
        proposeHash, prevLedger, set.proposeseq(), set.closetime (),
            Blob(set.nodepubkey ().begin (), set.nodepubkey ().end ()),
                Blob(set.signature ().begin (), set.signature ().end ()));

    if (! getApp().getHashRouter ().addSuppressionPeer (
        suppression, m_shortId))
    {
        m_journal.trace <<
            "Received duplicate proposal from peer " << m_shortId;
        return ec;
    }

    RippleAddress signerPublic = RippleAddress::createNodePublic (
        strCopy (set.nodepubkey ()));

    if (signerPublic == getConfig ().VALIDATION_PUB)
    {
        m_journal.trace <<
            "Received our own proposal from peer " << m_shortId;
        return ec;
    }

    bool isTrusted = getApp().getUNL ().nodeInUNL (signerPublic);
    if (!isTrusted && getApp().getFeeTrack ().isLoadedLocal ())
    {
        m_journal.debug << "Dropping UNTRUSTED proposal due to load";
        return ec;
    }

    m_journal.trace <<
        "Received " << (isTrusted ? "trusted" : "UNTRUSTED") <<
        " proposal from " << m_shortId;

    uint256 consensusLCL;

    {
        Application::ScopedLockType lock (getApp ().getMasterLock ());
        consensusLCL = getApp().getOPs ().getConsensusLCL ();
    }

    LedgerProposal::pointer proposal = std::make_shared<LedgerProposal> (
        prevLedger.isNonZero () ? prevLedger : consensusLCL,
            set.proposeseq (), proposeHash, set.closetime (),
                signerPublic, suppression);

    getApp().getJobQueue ().addJob (isTrusted ? jtPROPOSAL_t : jtPROPOSAL_ut,
        "recvPropose->checkPropose", std::bind (
            &PeerImp::checkPropose, std::placeholders::_1, &overlay_,
                m, proposal, consensusLCL, m_nodePublicKey,
                    std::weak_ptr<Peer> (shared_from_this ()), m_clusterNode,
                        m_journal));
    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMStatusChange> const& m)
{
    error_code ec;
    m_journal.trace << "Received status change from peer " <<
                        to_string (this);

    if (!m->has_networktime ())
        m->set_networktime (getApp().getOPs ().getNetworkTimeNC ());

    if (!mLastStatus.has_newstatus () || m->has_newstatus ())
        mLastStatus = *m;
    else
    {
        // preserve old status
        protocol::NodeStatus status = mLastStatus.newstatus ();
        mLastStatus = *m;
        m->set_newstatus (status);
    }

    if (m->newevent () == protocol::neLOST_SYNC)
    {
        if (!m_closedLedgerHash.isZero ())
        {
            m_journal.trace << "peer has lost sync " << to_string (this);
            m_closedLedgerHash.zero ();
        }

        m_previousLedgerHash.zero ();
        return ec;
    }

    if (m->has_ledgerhash () && (m->ledgerhash ().size () == (256 / 8)))
    {
        // a peer has changed ledgers
        memcpy (m_closedLedgerHash.begin (), m->ledgerhash ().data (), 256 / 8);
        addLedger (m_closedLedgerHash);
        m_journal.trace << "peer LCL is " << m_closedLedgerHash <<
                            " " << to_string (this);
    }
    else
    {
        m_journal.trace << "peer has no ledger hash" << to_string (this);
        m_closedLedgerHash.zero ();
    }

    if (m->has_ledgerhashprevious () &&
        m->ledgerhashprevious ().size () == (256 / 8))
    {
        memcpy (m_previousLedgerHash.begin (),
            m->ledgerhashprevious ().data (), 256 / 8);
        addLedger (m_previousLedgerHash);
    }
    else m_previousLedgerHash.zero ();

    if (m->has_firstseq () && m->has_lastseq())
    {
        m_minLedger = m->firstseq ();
        m_maxLedger = m->lastseq ();

        // Work around some servers that report sequences incorrectly
        if (m_minLedger == 0)
            m_maxLedger = 0;
        if (m_maxLedger == 0)
            m_minLedger = 0;
    }
    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMHaveTransactionSet> const& m)
{
    error_code ec;
    uint256 hashes;

    if (m->hash ().size () != (256 / 8))
    {
        charge (Resource::feeInvalidRequest);
        return ec;
    }

    uint256 hash;

    // VFALCO TODO There should be no use of memcpy() throughout the program.
    //        TODO Clean up this magic number
    //
    memcpy (hash.begin (), m->hash ().data (), 32);

    if (m->status () == protocol::tsHAVE)
        addTxSet (hash);

    {
        Application::ScopedLockType lock (getApp ().getMasterLock ());

        if (!getApp().getOPs ().hasTXSet (
                shared_from_this (), hash, m->status ()))
            charge (Resource::feeUnwantedData);
    }
    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMValidation> const& m)
{
    error_code ec;
    std::uint32_t closeTime = getApp().getOPs().getCloseTimeNC();

    if (m->validation ().size () < 50)
    {
        m_journal.warning << "Too small validation from peer";
        charge (Resource::feeInvalidRequest);
        return ec;
    }

    try
    {
        Serializer s (m->validation ());
        SerializerIterator sit (s);
        SerializedValidation::pointer val = std::make_shared <
            SerializedValidation> (std::ref (sit), false);

        if (closeTime > (120 + val->getFieldU32(sfSigningTime)))
        {
            m_journal.trace << "Validation is more than two minutes old";
            charge (Resource::feeUnwantedData);
            return ec;
        }

        if (! getApp().getHashRouter ().addSuppressionPeer (
            s.getSHA512Half(), m_shortId))
        {
            m_journal.trace << "Validation is duplicate";
            return ec;
        }

        bool isTrusted = getApp().getUNL ().nodeInUNL (val->getSignerPublic ());
        if (isTrusted || !getApp().getFeeTrack ().isLoadedLocal ())
        {
            getApp().getJobQueue ().addJob (
                isTrusted ? jtVALIDATION_t : jtVALIDATION_ut,
                "recvValidation->checkValidation",
                std::bind (&PeerImp::checkValidation, std::placeholders::_1,
                    &overlay_, val, isTrusted, m_clusterNode, m,
                        std::weak_ptr<Peer> (shared_from_this ()),
                            m_journal));
        }
        else
        {
            m_journal.debug <<
                "Dropping UNTRUSTED validation due to load";
        }
    }
    catch (...)
    {
        m_journal.warning <<
            "Exception processing validation";
        charge (Resource::feeInvalidRequest);
    }

    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMGetObjectByHash> const& m)
{
    error_code ec;
    protocol::TMGetObjectByHash& packet = *m;

    if (packet.query ())
    {
        // this is a query
        if (packet.type () == protocol::TMGetObjectByHash::otFETCH_PACK)
        {
            doFetchPack (m);
            return ec;
        }

        protocol::TMGetObjectByHash reply;

        reply.set_query (false);

        if (packet.has_seq ())
            reply.set_seq (packet.seq ());

        reply.set_type (packet.type ());

        if (packet.has_ledgerhash ())
            reply.set_ledgerhash (packet.ledgerhash ());

        // This is a very minimal implementation
        for (int i = 0; i < packet.objects_size (); ++i)
        {
            uint256 hash;
            const protocol::TMIndexedObject& obj = packet.objects (i);

            if (obj.has_hash () && (obj.hash ().size () == (256 / 8)))
            {
                memcpy (hash.begin (), obj.hash ().data (), 256 / 8);
                // VFALCO TODO Move this someplace more sensible so we dont
                //             need to inject the NodeStore interfaces.
                NodeObject::pointer hObj =
                    getApp().getNodeStore ().fetch (hash);

                if (hObj)
                {
                    protocol::TMIndexedObject& newObj = *reply.add_objects ();
                    newObj.set_hash (hash.begin (), hash.size ());
                    newObj.set_data (&hObj->getData ().front (),
                        hObj->getData ().size ());

                    if (obj.has_nodeid ())
                        newObj.set_index (obj.nodeid ());

                    if (!reply.has_seq () && (hObj->getLedgerIndex () != 0))
                        reply.set_seq (hObj->getLedgerIndex ());
                }
            }
        }

        m_journal.trace << "GetObjByHash had " << reply.objects_size () <<
                            " of " << packet.objects_size () <<
                            " for " << to_string (this);
        send (std::make_shared<Message> (reply, protocol::mtGET_OBJECTS));
    }
    else
    {
        // this is a reply
        std::uint32_t pLSeq = 0;
        bool pLDo = true;
        bool progress = false;

        for (int i = 0; i < packet.objects_size (); ++i)
        {
            const protocol::TMIndexedObject& obj = packet.objects (i);

            if (obj.has_hash () && (obj.hash ().size () == (256 / 8)))
            {
                if (obj.has_ledgerseq ())
                {
                    if (obj.ledgerseq () != pLSeq)
                    {
                        if ((pLDo && (pLSeq != 0)) &&
                            m_journal.active(beast::Journal::Severity::kDebug))
                            m_journal.debug <<
                                "Received full fetch pack for " << pLSeq;

                        pLSeq = obj.ledgerseq ();
                        pLDo = !getApp().getOPs ().haveLedger (pLSeq);

                        if (!pLDo)
                                m_journal.debug <<
                                    "Got pack for " << pLSeq << " too late";
                        else
                            progress = true;
                    }
                }

                if (pLDo)
                {
                    uint256 hash;
                    memcpy (hash.begin (), obj.hash ().data (), 256 / 8);

                    std::shared_ptr< Blob > data (
                        std::make_shared< Blob > (
                            obj.data ().begin (), obj.data ().end ()));

                    getApp().getOPs ().addFetchPack (hash, data);
                }
            }
        }

        if ((pLDo && (pLSeq != 0)) &&
            m_journal.active(beast::Journal::Severity::kDebug))
            m_journal.debug << "Received partial fetch pack for " << pLSeq;

        if (packet.type () == protocol::TMGetObjectByHash::otFETCH_PACK)
            getApp().getOPs ().gotFetchPack (progress, pLSeq);
    }
    return ec;
}

//------------------------------------------------------------------------------

// VFALCO NOTE This function is way too big and cumbersome.
void
PeerImp::getLedger (protocol::TMGetLedger& packet)
{
    SHAMap::pointer map;
    protocol::TMLedgerData reply;
    bool fatLeaves = true, fatRoot = false;

    if (packet.has_requestcookie ())
        reply.set_requestcookie (packet.requestcookie ());

    std::string logMe;

    if (packet.itype () == protocol::liTS_CANDIDATE)
    {
        // Request is for a transaction candidate set
        m_journal.trace << "Received request for TX candidate set data "
                        << to_string (this);

        if ((!packet.has_ledgerhash () || packet.ledgerhash ().size () != 32))
        {
            charge (Resource::feeInvalidRequest);
            m_journal.warning << "invalid request for TX candidate set data";
            return;
        }

        uint256 txHash;
        memcpy (txHash.begin (), packet.ledgerhash ().data (), 32);

        {
            Application::ScopedLockType lock (getApp ().getMasterLock ());
            map = getApp().getOPs ().getTXMap (txHash);
        }

        if (!map)
        {
            if (packet.has_querytype () && !packet.has_requestcookie ())
            {
                m_journal.debug << "Trying to route TX set request";

                struct get_usable_peers
                {
                    typedef Overlay::PeerSequence return_type;

                    Overlay::PeerSequence usablePeers;
                    uint256 const& txHash;
                    Peer const* skip;

                    get_usable_peers(uint256 const& hash, Peer const* s)
                        : txHash(hash), skip(s)
                    { }

                    void operator() (Peer::ptr const& peer)
                    {
                        if (peer->hasTxSet (txHash) && (peer.get () != skip))
                            usablePeers.push_back (peer);
                    }

                    return_type operator() ()
                    {
                        return usablePeers;
                    }
                };

                Overlay::PeerSequence usablePeers (overlay_.foreach (
                    get_usable_peers (txHash, this)));

                if (usablePeers.empty ())
                {
                    m_journal.info << "Unable to route TX set request";
                    return;
                }

                Peer::ptr const& selectedPeer = usablePeers [
                    rand () % usablePeers.size ()];
                packet.set_requestcookie (getShortId ());
                selectedPeer->send (std::make_shared<Message> (
                    packet, protocol::mtGET_LEDGER));
                return;
            }

            m_journal.error << "We do not have the map our peer wants "
                            << to_string (this);

            charge (Resource::feeInvalidRequest);
            return;
        }

        reply.set_ledgerseq (0);
        reply.set_ledgerhash (txHash.begin (), txHash.size ());
        reply.set_type (protocol::liTS_CANDIDATE);
        fatLeaves = false; // We'll already have most transactions
        fatRoot = true; // Save a pass
    }
    else
    {
        if (getApp().getFeeTrack().isLoadedLocal() && !m_clusterNode)
        {
            m_journal.debug << "Too busy to fetch ledger data";
            return;
        }

        // Figure out what ledger they want
        m_journal.trace << "Received request for ledger data "
                        << to_string (this);
        Ledger::pointer ledger;

        if (packet.has_ledgerhash ())
        {
            uint256 ledgerhash;

            if (packet.ledgerhash ().size () != 32)
            {
                charge (Resource::feeInvalidRequest);
                m_journal.warning << "Invalid request";
                return;
            }

            memcpy (ledgerhash.begin (), packet.ledgerhash ().data (), 32);
            logMe += "LedgerHash:";
            logMe += to_string (ledgerhash);
            ledger = getApp().getLedgerMaster ().getLedgerByHash (ledgerhash);

            if (!ledger && m_journal.trace)
                m_journal.trace << "Don't have ledger " << ledgerhash;

            if (!ledger && (packet.has_querytype () &&
                !packet.has_requestcookie ()))
            {
                std::uint32_t seq = 0;

                if (packet.has_ledgerseq ())
                    seq = packet.ledgerseq ();

                Overlay::PeerSequence peerList = overlay_.getActivePeers ();
                Overlay::PeerSequence usablePeers;
                for (auto const& peer : peerList)
                {
                    if (peer->hasLedger (ledgerhash, seq) && (peer.get () != this))
                        usablePeers.push_back (peer);
                }

                if (usablePeers.empty ())
                {
                    m_journal.trace << "Unable to route ledger request";
                    return;
                }

                Peer::ptr const& selectedPeer = usablePeers [
                    rand () % usablePeers.size ()];
                packet.set_requestcookie (getShortId ());
                selectedPeer->send (
                    std::make_shared<Message> (packet, protocol::mtGET_LEDGER));
                m_journal.debug << "Ledger request routed";
                return;
            }
        }
        else if (packet.has_ledgerseq ())
        {
            if (packet.ledgerseq() <
                    getApp().getLedgerMaster().getEarliestFetch())
            {
                m_journal.debug << "Peer requests early ledger";
                return;
            }
            ledger = getApp().getLedgerMaster ().getLedgerBySeq (
                packet.ledgerseq ());
            if (!ledger && m_journal.debug)
                m_journal.debug << "Don't have ledger " << packet.ledgerseq ();
        }
        else if (packet.has_ltype () && (packet.ltype () == protocol::ltCURRENT))
        {
            ledger = getApp().getLedgerMaster ().getCurrentLedger ();
        }
        else if (packet.has_ltype () && (packet.ltype () == protocol::ltCLOSED) )
        {
            ledger = getApp().getLedgerMaster ().getClosedLedger ();

            if (ledger && !ledger->isClosed ())
                ledger = getApp().getLedgerMaster ().getLedgerBySeq (
                    ledger->getLedgerSeq () - 1);
        }
        else
        {
            charge (Resource::feeInvalidRequest);
            m_journal.warning << "Can't figure out what ledger they want";
            return;
        }

        if ((!ledger) || (packet.has_ledgerseq () && (
            packet.ledgerseq () != ledger->getLedgerSeq ())))
        {
            charge (Resource::feeInvalidRequest);

            if (m_journal.warning && ledger)
                m_journal.warning << "Ledger has wrong sequence";

            return;
        }

            if (!packet.has_ledgerseq() && (ledger->getLedgerSeq() <
                getApp().getLedgerMaster().getEarliestFetch()))
            {
                m_journal.debug << "Peer requests early ledger";
                return;
            }

        // Fill out the reply
        uint256 lHash = ledger->getHash ();
        reply.set_ledgerhash (lHash.begin (), lHash.size ());
        reply.set_ledgerseq (ledger->getLedgerSeq ());
        reply.set_type (packet.itype ());

        if (packet.itype () == protocol::liBASE)
        {
            // they want the ledger base data
            m_journal.trace << "They want ledger base data";
            Serializer nData (128);
            ledger->addRaw (nData);
            reply.add_nodes ()->set_nodedata (
                nData.getDataPtr (), nData.getLength ());

            SHAMap::pointer map = ledger->peekAccountStateMap ();

            if (map && map->getHash ().isNonZero ())
            {
                // return account state root node if possible
                Serializer rootNode (768);

                if (map->getRootNode (rootNode, snfWIRE))
                {
                    reply.add_nodes ()->set_nodedata (
                        rootNode.getDataPtr (), rootNode.getLength ());

                    if (ledger->getTransHash ().isNonZero ())
                    {
                        map = ledger->peekTransactionMap ();

                        if (map && map->getHash ().isNonZero ())
                        {
                            rootNode.erase ();

                            if (map->getRootNode (rootNode, snfWIRE))
                                reply.add_nodes ()->set_nodedata (
                                    rootNode.getDataPtr (),
                                        rootNode.getLength ());
                        }
                    }
                }
            }

            Message::pointer oPacket = std::make_shared<Message> (
                reply, protocol::mtLEDGER_DATA);
            send (oPacket);
            return;
        }

        if (packet.itype () == protocol::liTX_NODE)
        {
            map = ledger->peekTransactionMap ();
            logMe += " TX:";
            logMe += to_string (map->getHash ());
        }
        else if (packet.itype () == protocol::liAS_NODE)
        {
            map = ledger->peekAccountStateMap ();
            logMe += " AS:";
            logMe += to_string (map->getHash ());
        }
    }

    if (!map || (packet.nodeids_size () == 0))
    {
        m_journal.warning << "Can't find map or empty request";
        charge (Resource::feeInvalidRequest);
        return;
    }

    m_journal.trace << "Request: " << logMe;

    for (int i = 0; i < packet.nodeids ().size (); ++i)
    {
        SHAMapNodeID mn (packet.nodeids (i).data (), packet.nodeids (i).size ());

        if (!mn.isValid ())
        {
            m_journal.warning << "Request for invalid node: " << logMe;
            charge (Resource::feeInvalidRequest);
            return;
        }

        std::vector<SHAMapNodeID> nodeIDs;
        std::list< Blob > rawNodes;

        try
        {
            if (map->getNodeFat (mn, nodeIDs, rawNodes, fatRoot, fatLeaves))
            {
                assert (nodeIDs.size () == rawNodes.size ());
                m_journal.trace <<
                    "getNodeFat got " << rawNodes.size () << " nodes";
                std::vector<SHAMapNodeID>::iterator nodeIDIterator;
                std::list< Blob >::iterator rawNodeIterator;

                for (nodeIDIterator = nodeIDs.begin (),
                        rawNodeIterator = rawNodes.begin ();
                            nodeIDIterator != nodeIDs.end ();
                                ++nodeIDIterator, ++rawNodeIterator)
                {
                    Serializer nID (33);
                    nodeIDIterator->addIDRaw (nID);
                    protocol::TMLedgerNode* node = reply.add_nodes ();
                    node->set_nodeid (nID.getDataPtr (), nID.getLength ());
                    node->set_nodedata (&rawNodeIterator->front (),
                        rawNodeIterator->size ());
                }
            }
            else
                m_journal.warning << "getNodeFat returns false";
        }
        catch (std::exception&)
        {
            std::string info;

            if (packet.itype () == protocol::liTS_CANDIDATE)
                info = "TS candidate";
            else if (packet.itype () == protocol::liBASE)
                info = "Ledger base";
            else if (packet.itype () == protocol::liTX_NODE)
                info = "TX node";
            else if (packet.itype () == protocol::liAS_NODE)
                info = "AS node";

            if (!packet.has_ledgerhash ())
                info += ", no hash specified";

            m_journal.warning <<
                "getNodeFat( " << mn << ") throws exception: " << info;
        }
    }

    Message::pointer oPacket = std::make_shared<Message> (
        reply, protocol::mtLEDGER_DATA);
    send (oPacket);
}

//------------------------------------------------------------------------------

void
PeerImp::detach (const char* rsn, bool graceful)
{
    if (! m_strand.running_in_this_thread ())
    {
        m_strand.post (std::bind (&PeerImp::detach,
            shared_from_this (), rsn, graceful));
        return;
    }

    if (!m_detaching)
    {
        // NIKB TODO No - a race is NOT ok. This needs to be fixed
        //           to have PeerFinder work reliably.
        m_detaching  = true; // Race is ok.

        if (m_was_canceled)
            m_peerFinder.on_cancel (m_slot);
        else
            m_peerFinder.on_closed (m_slot);

        if (m_state == stateActive)
            overlay_.onPeerDisconnect (shared_from_this ());

        m_state = stateGracefulClose;

        if (m_clusterNode && m_journal.active(beast::Journal::Severity::kWarning))
            m_journal.warning << "Cluster peer " << m_nodeName <<
                                    " detached: " << rsn;

        mSendQ.clear ();

        (void) timer_.cancel ();

        if (graceful)
        {
            m_socket->async_shutdown (
                m_strand.wrap ( std::bind(
                    &PeerImp::handleShutdown,
                    std::static_pointer_cast <PeerImp> (shared_from_this ()),
                    beast::asio::placeholders::error)));
        }
        else
        {
            m_socket->cancel ();
        }

        // VFALCO TODO Stop doing this.
        if (m_nodePublicKey.isValid ())
            m_nodePublicKey.clear ();       // Be idempotent.
    }
}

//--------------------------------------------------------------------------

void
PeerImp::sendGetPeers ()
{
    // Ask peer for known other peers.
    protocol::TMGetPeers msg;

    msg.set_doweneedthis (1);

    Message::pointer packet = std::make_shared<Message> (
        msg, protocol::mtGET_PEERS);

    send (packet);
}

void
PeerImp::charge (std::weak_ptr <Peer>& peer, Resource::Charge const& fee)
{
    Peer::ptr p (peer.lock());

    if (p != nullptr)
        p->charge (fee);
}

void
PeerImp::sendForce (const Message::pointer& packet)
{
    // must be on IO strand
    if (!m_detaching)
    {
        mSendingPacket = packet;

        boost::asio::async_write (*m_socket,
            boost::asio::buffer (packet->getBuffer ()),
            m_strand.wrap (std::bind (
                &PeerImp::handleWrite,
                std::static_pointer_cast <PeerImp> (shared_from_this ()),
                beast::asio::placeholders::error,
                beast::asio::placeholders::bytes_transferred)));
    }
}

std::pair <protocol::TMHello, bool>
PeerImp::parse_hello (beast::http::message const& m)
{
    auto const& h = m.headers;
    std::pair <protocol::TMHello, bool> result;
    result.second = false;
    protocol::TMHello& hello (result.first);

    // VFALCO TODO
    //hello->set_protoversion (BuildInfo::Protocol 

    {
        // Required
        auto const iter = h.find ("Public-Key");
        if (iter != h.end())
        {
            RippleAddress addr;
            addr.setNodePublic (iter->value);
            if (! addr.isValid())
                return result;
            hello.set_nodepublic (iter->value);
        }
    }

    {
        // Required
        auto const iter = h.find ("Session-Signature");
        if (iter == h.end())
            return result;
        // TODO Security Review
        hello.set_nodeproof (beast::base64_decode (iter->value));
    }

    {
        auto const iter = h.find (m.request() ?
            "User-Agent" : "Server");
        if (iter != h.end())
            hello.set_fullversion (iter->value);
    }

    {
        auto const iter = h.find ("Network-Time");
        if (iter != h.end())
        {
            auto const ret = beast::http::rfc2616::parse_uint <
                std::uint64_t> (iter->value);
            if (! ret.first)
                return result;
            hello.set_nettime (ret.second);
        }
    }

    {
        auto const iter = h.find ("Ledger");
        if (iter != h.end())
        {
            auto const ret = beast::http::rfc2616::parse_uint <
                LedgerIndex> (iter->value);
            if (! ret.first)
                return result;
            hello.set_ledgerindex (ret.second);
        }
    }

    {
        auto const iter = h.find ("Closed-Ledger");
        if (iter != h.end())
            hello.set_ledgerclosed (beast::base64_decode (iter->value));
    }

    {
        auto const iter = h.find ("Previous-Ledger");
        if (iter != h.end())
            hello.set_ledgerprevious (beast::base64_decode (iter->value));
    }

    result.second = true;
    return result;
}

void
PeerImp::append_hello (beast::http::message& m,
    protocol::TMHello const& hello)
{
    auto& h = m.headers;

    //h.append ("Protocol-Versions",...

    h.append ("Public-Key", hello.nodepublic());

    h.append ("Session-Signature", beast::base64_encode (
        hello.nodeproof()));

    if (m.request())
        h.append ("User-Agent", BuildInfo::getFullVersionString());
    else
        h.append ("Server", BuildInfo::getFullVersionString());

    if (hello.has_nettime())
        h.append ("Network-Time", std::to_string (hello.nettime()));

    if (hello.has_ledgerindex())
        h.append ("Ledger", std::to_string (hello.ledgerindex()));

    if (hello.has_ledgerclosed())
        h.append ("Closed-Ledger", beast::base64_encode (
            hello.ledgerclosed()));

    if (hello.has_ledgerprevious())
        h.append ("Previous-Ledger", beast::base64_encode (
            hello.ledgerprevious()));
}

bool
PeerImp::hashLatestFinishedMessage (const SSL *sslSession, unsigned char *hash,
    size_t (*getFinishedMessage)(const SSL *, void *buf, size_t))
{
    unsigned char buf[1024];

    // Get our finished message and hash it.
    std::memset(hash, 0, 64);

    size_t len = getFinishedMessage (sslSession, buf, sizeof (buf));

    if(len < sslMinimumFinishedLength)
        return false;

    SHA512 (buf, len, hash);

    return true;
}

bool
PeerImp::calculateSessionCookie ()
{
    SSL* ssl = m_socket->ssl_handle ();

    if (!ssl)
    {
        m_journal.error << "Cookie generation: No underlying connection";
        return false;
    }

    unsigned char sha1[64];
    unsigned char sha2[64];

    if (!hashLatestFinishedMessage(ssl, sha1, SSL_get_finished))
    {
        m_journal.error << "Cookie generation: local setup not complete";
        return false;
    }

    if (!hashLatestFinishedMessage(ssl, sha2, SSL_get_peer_finished))
    {
        m_journal.error << "Cookie generation: peer setup not complete";
        return false;
    }

    // If both messages hash to the same value (i.e. match) something is
    // wrong. This would cause the resulting cookie to be 0.
    if (memcmp (sha1, sha2, sizeof (sha1)) == 0)
    {
        m_journal.error << "Cookie generation: identical finished messages";
        return false;
    }

    for (size_t i = 0; i < sizeof (sha1); ++i)
        sha1[i] ^= sha2[i];

    // Finally, derive the actual cookie for the values that we have
    // calculated.
    m_secureCookie = Serializer::getSHA512Half (sha1, sizeof(sha1));

    return true;
}

std::pair <protocol::TMHello, bool>
PeerImp::build_hello()
{
    std::pair <protocol::TMHello, bool> result { {}, false };
    protocol::TMHello& h = result.first;

    if (!calculateSessionCookie())
        return result;

    Blob vchSig;
    getApp().getLocalCredentials ().getNodePrivate ().signNodePrivate (
        m_secureCookie, vchSig);

    h.set_protoversion (BuildInfo::getCurrentProtocol().toPacked ());
    h.set_protoversionmin (BuildInfo::getMinimumProtocol().toPacked ());
    h.set_fullversion (BuildInfo::getFullVersionString ());
    h.set_nettime (getApp().getOPs ().getNetworkTimeNC ());
    h.set_nodepublic (getApp().getLocalCredentials ().getNodePublic (
        ).humanNodePublic ());
    h.set_nodeproof (&vchSig[0], vchSig.size ());
    h.set_ipv4port (getConfig ().peerListeningPort);
    h.set_testnet (false);

    // We always advertise ourselves as private in the HELLO message. This
    // suppresses the old peer advertising code and allows PeerFinder to
    // take over the functionality.
    h.set_nodeprivate (true);

    Ledger::pointer closedLedger = getApp().getLedgerMaster ().getClosedLedger ();

    if (closedLedger && closedLedger->isClosed ())
    {
        uint256 hash = closedLedger->getHash ();
        h.set_ledgerclosed (hash.begin (), hash.size ());
        hash = closedLedger->getParentHash ();
        h.set_ledgerprevious (hash.begin (), hash.size ());
    }

    result.second = true;
    return result;
}

bool
PeerImp::sendHello ()
{
    auto const result = build_hello();
    if (! result.second)
        return false;
    auto const m = std::make_shared <Message> (
        result.first, protocol::mtHELLO);
    send (m);
    return true;
}

void
PeerImp::addLedger (uint256 const& hash)
{
    std::lock_guard<std::mutex> sl(m_recentLock);

    if (std::find (m_recentLedgers.begin(),
        m_recentLedgers.end(), hash) != m_recentLedgers.end())
        return;

    // VFALCO TODO See if a sorted vector would be better.

    if (m_recentLedgers.size () == 128)
        m_recentLedgers.pop_front ();

    m_recentLedgers.push_back (hash);
}

void
PeerImp::addTxSet (uint256 const& hash)
{
    std::lock_guard<std::mutex> sl(m_recentLock);

    if (std::find (m_recentTxSets.begin (),
        m_recentTxSets.end (), hash) != m_recentTxSets.end ())
        return;

    if (m_recentTxSets.size () == 128)
        m_recentTxSets.pop_front ();

    m_recentTxSets.push_back (hash);
}

void
PeerImp::doFetchPack (const std::shared_ptr<protocol::TMGetObjectByHash>& packet)
{
    // VFALCO TODO Invert this dependency using an observer and shared state object.
    // Don't queue fetch pack jobs if we're under load or we already have
    // some queued.
    if (getApp().getFeeTrack ().isLoadedLocal () ||
        (getApp().getLedgerMaster().getValidatedLedgerAge() > 40) ||
        (getApp().getJobQueue().getJobCount(jtPACK) > 10))
    {
        m_journal.info << "Too busy to make fetch pack";
        return;
    }

    if (packet->ledgerhash ().size () != 32)
    {
        m_journal.warning << "FetchPack hash size malformed";
        charge (Resource::feeInvalidRequest);
        return;
    }

    uint256 hash;
    memcpy (hash.begin (), packet->ledgerhash ().data (), 32);

    getApp().getJobQueue ().addJob (jtPACK, "MakeFetchPack",
        std::bind (&NetworkOPs::makeFetchPack, &getApp().getOPs (),
            std::placeholders::_1, std::weak_ptr<Peer> (shared_from_this ()),
                packet, hash, UptimeTimer::getInstance ().getElapsedSeconds ()));
}

void
PeerImp::doProofOfWork (Job&, std::weak_ptr <Peer> peer,
    ProofOfWork::pointer pow)
{
    if (peer.expired ())
        return;

    uint256 solution = pow->solve ();

    if (solution.isZero ())
    {
            m_journal.warning << "Failed to solve proof of work";
    }
    else
    {
        Peer::ptr pptr (peer.lock ());

        if (pptr)
        {
            protocol::TMProofWork reply;
            reply.set_token (pow->getToken ());
            reply.set_response (solution.begin (), solution.size ());
            pptr->send (std::make_shared<Message> (
                reply, protocol::mtPROOFOFWORK));
        }
        else
        {
            // WRITEME: Save solved proof of work for new connection
        }
    }
}

void
PeerImp::checkTransaction (Job&, int flags,
    SerializedTransaction::pointer stx, std::weak_ptr<Peer> peer)
{
    // VFALCO TODO Rewrite to not use exceptions
    try
    {
        if (stx->isFieldPresent(sfLastLedgerSequence) &&
            (stx->getFieldU32 (sfLastLedgerSequence) <
            getApp().getLedgerMaster().getValidLedgerIndex()))
        {
            // Transaction has expired
            getApp().getHashRouter().setFlag(stx->getTransactionID(), SF_BAD);
            charge (peer, Resource::feeUnwantedData);
            return;
        }

        bool const needCheck = !(flags & SF_SIGGOOD);
        Transaction::pointer tx =
            std::make_shared<Transaction> (stx, needCheck);

        if (tx->getStatus () == INVALID)
        {
            getApp().getHashRouter ().setFlag (stx->getTransactionID (), SF_BAD);
            charge (peer, Resource::feeInvalidSignature);
            return;
        }
        else
            getApp().getHashRouter ().setFlag (
                stx->getTransactionID (), SF_SIGGOOD);

        bool const trusted (flags & SF_TRUSTED);
        getApp().getOPs ().processTransaction (tx, trusted, false, false);
    }
    catch (...)
    {
        getApp().getHashRouter ().setFlag (stx->getTransactionID (), SF_BAD);
        charge (peer, Resource::feeInvalidRequest);
    }
}

// Called from our JobQueue
void
PeerImp::checkPropose (Job& job, Overlay* pPeers,
    std::shared_ptr <protocol::TMProposeSet> packet,
        LedgerProposal::pointer proposal, uint256 consensusLCL,
            RippleAddress nodePublic, std::weak_ptr<Peer> peer,
                bool fromCluster, beast::Journal journal)
{
    bool sigGood = false;
    bool isTrusted = (job.getType () == jtPROPOSAL_t);

    journal.trace <<
        "Checking " << (isTrusted ? "trusted" : "UNTRUSTED") << " proposal";

    assert (packet);
    protocol::TMProposeSet& set = *packet;

    uint256 prevLedger;

    if (set.has_previousledger ())
    {
        // proposal includes a previous ledger
        journal.trace <<
            "proposal with previous ledger";
        memcpy (prevLedger.begin (), set.previousledger ().data (), 256 / 8);

        if (!fromCluster && !proposal->checkSign (set.signature ()))
        {
            Peer::ptr p = peer.lock ();
            journal.warning <<
                "proposal with previous ledger fails sig check: " << *p;
            charge (peer, Resource::feeInvalidSignature);
            return;
        }
        else
            sigGood = true;
    }
    else
    {
        if (consensusLCL.isNonZero () && proposal->checkSign (set.signature ()))
        {
            prevLedger = consensusLCL;
            sigGood = true;
        }
        else
        {
            // Could be mismatched prev ledger
            journal.warning <<
                "Ledger proposal fails signature check";
            proposal->setSignature (set.signature ());
        }
    }

    if (isTrusted)
    {
        getApp().getOPs ().processTrustedProposal (
            proposal, packet, nodePublic, prevLedger, sigGood);
    }
    else if (sigGood && (prevLedger == consensusLCL))
    {
        // relay untrusted proposal
        journal.trace <<
            "relaying UNTRUSTED proposal";
        std::set<Peer::ShortId> peers;

        if (getApp().getHashRouter ().swapSet (
            proposal->getSuppressionID (), peers, SF_RELAYED))
        {
            pPeers->foreach (send_if_not (
                std::make_shared<Message> (set, protocol::mtPROPOSE_LEDGER),
                peer_in_set(peers)));
        }
    }
    else
    {
        journal.debug <<
            "Not relaying UNTRUSTED proposal";
    }
}

void
PeerImp::checkValidation (Job&, Overlay* pPeers,
    SerializedValidation::pointer val, bool isTrusted, bool isCluster,
        std::shared_ptr<protocol::TMValidation> packet,
            std::weak_ptr<Peer> peer, beast::Journal journal)
{
    try
    {
        uint256 signingHash = val->getSigningHash();
        if (!isCluster && !val->isValid (signingHash))
        {
            journal.warning <<
                "Validation is invalid";
            charge (peer, Resource::feeInvalidRequest);
            return;
        }

        std::string source;
        Peer::ptr lp = peer.lock ();

        if (lp)
            source = to_string(*lp);
        else
            source = "unknown";

        std::set<Peer::ShortId> peers;

        //----------------------------------------------------------------------
        //
        {
            SerializedValidation const& sv (*val);
            Validators::ReceivedValidation rv;
            rv.ledgerHash = sv.getLedgerHash ();
            rv.publicKey = sv.getSignerPublic();
            getApp ().getValidators ().on_receive_validation (rv);
        }
        //
        //----------------------------------------------------------------------

        if (getApp().getOPs ().recvValidation (val, source) &&
                getApp().getHashRouter ().swapSet (
                    signingHash, peers, SF_RELAYED))
        {
            pPeers->foreach (send_if_not (
                std::make_shared<Message> (*packet, protocol::mtVALIDATION),
                peer_in_set(peers)));
        }
    }
    catch (...)
    {
        journal.trace <<
            "Exception processing validation";
        charge (peer, Resource::feeInvalidRequest);
    }
}

// This is dispatched by the job queue
void
PeerImp::sGetLedger (std::weak_ptr<PeerImp> wPeer,
    std::shared_ptr <protocol::TMGetLedger> packet)
{
    std::shared_ptr<PeerImp> peer = wPeer.lock ();

    if (peer)
        peer->getLedger (*packet);
}

// VFALCO TODO Make this non-static
void
PeerImp::peerTXData (Job&, std::weak_ptr <Peer> wPeer, uint256 const& hash,
    std::shared_ptr <protocol::TMLedgerData> pPacket, beast::Journal journal)
{
    std::shared_ptr <Peer> peer = wPeer.lock ();
    if (!peer)
        return;

    protocol::TMLedgerData& packet = *pPacket;

    std::list<SHAMapNodeID> nodeIDs;
    std::list< Blob > nodeData;
    for (int i = 0; i < packet.nodes ().size (); ++i)
    {
        const protocol::TMLedgerNode& node = packet.nodes (i);

        if (!node.has_nodeid () || !node.has_nodedata () || (
            node.nodeid ().size () != 33))
        {
            journal.warning << "LedgerData request with invalid node ID";
            peer->charge (Resource::feeInvalidRequest);
            return;
        }

        nodeIDs.push_back (SHAMapNodeID {node.nodeid ().data (),
                           static_cast<int>(node.nodeid ().size ())});
        nodeData.push_back (Blob (node.nodedata ().begin (),
            node.nodedata ().end ()));
    }

    SHAMapAddNode san;
    {
        Application::ScopedLockType lock (getApp ().getMasterLock ());

        san =  getApp().getOPs().gotTXData (peer, hash, nodeIDs, nodeData);
    }

    if (san.isInvalid ())
    {
        peer->charge (Resource::feeUnwantedData);
    }
}

} // ripple
