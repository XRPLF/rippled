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
#include <beast/streams/debug_ostream.h>
#include <functional>

namespace ripple {

PeerImp::PeerImp (NativeSocketType&& socket, beast::IP::Endpoint remoteAddress,
    OverlayImpl& overlay, Resource::Manager& resourceManager,
        PeerFinder::Manager& peerFinder, PeerFinder::Slot::ptr const& slot,
            boost::asio::ssl::context& ssl_context, MultiSocket::Flag flags)
    : m_owned_socket (std::move (socket))
    , journal_ (deprecatedLogs().journal("Peer"))
    , remote_address_ (remoteAddress)
    , resourceManager_ (resourceManager)
    , peerFinder_ (peerFinder)
    , overlay_ (overlay)
    , m_inbound (true)
    , socket_ (MultiSocket::New (
        m_owned_socket, ssl_context, flags.asBits ()))
    , strand_ (m_owned_socket.get_io_service())
    , state_ (stateConnected)
    , minLedger_ (0)
    , maxLedger_ (0)
    , timer_ (m_owned_socket.get_io_service())
    , slot_ (slot)
    , message_stream_(*this)
{
}

PeerImp::PeerImp (beast::IP::Endpoint remoteAddress,
    boost::asio::io_service& io_service, OverlayImpl& overlay,
        Resource::Manager& resourceManager, PeerFinder::Manager& peerFinder,
            PeerFinder::Slot::ptr const& slot,
                boost::asio::ssl::context& ssl_context, MultiSocket::Flag flags)
    : m_owned_socket (io_service)
    , journal_ (deprecatedLogs().journal("Peer"))
    , remote_address_ (remoteAddress)
    , resourceManager_ (resourceManager)
    , peerFinder_ (peerFinder)
    , overlay_ (overlay)
    , m_inbound (false)
    , socket_ (MultiSocket::New (
        io_service, ssl_context, flags.asBits ()))
    , strand_ (io_service)
    , state_ (stateConnecting)
    , minLedger_ (0)
    , maxLedger_ (0)
    , timer_ (io_service)
    , slot_ (slot)
    , message_stream_(*this)
{
}

PeerImp::~PeerImp ()
{
    overlay_.remove (slot_);
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
PeerImp::close (bool graceful)
{
    was_canceled_ = true;
    detach ("stop", graceful);
}

//------------------------------------------------------------------------------

void
PeerImp::send (Message::pointer const& m)
{
    // VFALCO NOTE why call this with null?
    if (! m)
        return;

    if (! strand_.running_in_this_thread())
    {
        strand_.post (std::bind (&PeerImp::send, shared_from_this(), m));
        return;
    }

    if (send_packet_)
        send_queue_.push_back (m);
    else
        sendForce (m);
}

beast::IP::Endpoint
PeerImp::getRemoteAddress() const
{
    return remote_address_;
}

void
PeerImp::charge (Resource::Charge const& fee)
{
    if ((usage_.charge (fee) == Resource::drop) && usage_.disconnect ())
        detach ("resource");
}

//------------------------------------------------------------------------------

Peer::ShortId
PeerImp::getShortId () const
{
    return shortId_;
}

RippleAddress const&
PeerImp::getNodePublic () const
{
    return publicKey_;
}

Json::Value
PeerImp::json()
{
    Json::Value ret (Json::objectValue);

    ret["public_key"]   = publicKey_.ToString ();
    ret["address"]      = remote_address_.to_string();

    if (m_inbound)
        ret["inbound"] = true;

    if (clusterNode_)
    {
        ret["cluster"] = true;

        if (!name_.empty ())
            ret["name"] = name_;
    }

    if (hello_.has_fullversion ())
        ret["version"] = hello_.fullversion ();

    if (hello_.has_protoversion ())
    {
        auto protocol = BuildInfo::make_protocol (hello_.protoversion ());

        if (protocol != BuildInfo::getCurrentProtocol())
            ret["protocol"] = to_string (protocol);
    }

    std::uint32_t minSeq, maxSeq;
    ledgerRange(minSeq, maxSeq);

    if ((minSeq != 0) || (maxSeq != 0))
        ret["complete_ledgers"] = boost::lexical_cast<std::string>(minSeq) +
            " - " + boost::lexical_cast<std::string>(maxSeq);

    if (closedLedgerHash_ != zero)
        ret["ledger"] = to_string (closedLedgerHash_);

    if (last_status_.has_newstatus ())
    {
        switch (last_status_.newstatus ())
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
            journal_.warning <<
                "Unknown status: " << last_status_.newstatus ();
        }
    }

    return ret;
}

bool
PeerImp::isInCluster () const
{
    return clusterNode_;
}

std::string const&
PeerImp::getClusterNodeName() const
{
    return name_;
}

//------------------------------------------------------------------------------

uint256 const&
PeerImp::getClosedLedgerHash () const
{
    return closedLedgerHash_;
}

bool
PeerImp::hasLedger (uint256 const& hash, std::uint32_t seq) const
{
    std::lock_guard<std::mutex> sl(recentLock_);
    if ((seq != 0) && (seq >= minLedger_) && (seq <= maxLedger_))
        return true;
    return std::find (recentLedgers_.begin(),
        recentLedgers_.end(), hash) != recentLedgers_.end();
}

void
PeerImp::ledgerRange (std::uint32_t& minSeq,
    std::uint32_t& maxSeq) const
{
    std::lock_guard<std::mutex> sl(recentLock_);

    minSeq = minLedger_;
    maxSeq = maxLedger_;
}

bool
PeerImp::hasTxSet (uint256 const& hash) const
{
    std::lock_guard<std::mutex> sl(recentLock_);
    return std::find (recentTxSets_.begin(),
        recentTxSets_.end(), hash) != recentTxSets_.end();
}

void
PeerImp::cycleStatus ()
{
    previousLedgerHash_ = closedLedgerHash_;
    closedLedgerHash_.zero ();
}

bool
PeerImp::supportsVersion (int version)
{
    return hello_.has_protoversion () && (hello_.protoversion () >= version);
}

bool
PeerImp::hasRange (std::uint32_t uMin, std::uint32_t uMax)
{
    return (uMin >= minLedger_) && (uMax <= maxLedger_);
}

//------------------------------------------------------------------------------

/*  Completion handlers for client role.
    Logic steps:
        1. Establish outgoing connection
        2. Perform SSL handshake
        3. Send HTTP request
        4. Receive HTTP response
        5. Enter protocol loop
*/    

void PeerImp::do_connect ()
{
    journal_.info << "Connecting to " << remote_address_;

    usage_ = resourceManager_.newOutboundEndpoint (remote_address_);

    if (usage_.disconnect ())
        return detach ("do_connect");

    boost::system::error_code ec;
    timer_.expires_from_now (nodeVerifySeconds, ec);
    timer_.async_wait (strand_.wrap (std::bind (&PeerImp::handleVerifyTimer,
        shared_from_this (), beast::asio::placeholders::error)));
    if (ec)
    {
        journal_.error << "Failed to set verify timer.";
        return detach ("do_connect");
    }

    socket_->next_layer <NativeSocketType>().async_connect (
        beast::IPAddressConversion::to_asio_endpoint (remote_address_),
            strand_.wrap (std::bind (&PeerImp::on_connect,
                shared_from_this (), beast::asio::placeholders::error)));
}

void
PeerImp::on_connect (error_code ec)
{
    if (detaching_ || ec == boost::asio::error::operation_aborted)
        return;

    NativeSocketType::endpoint_type local_endpoint;
    if (! ec)
        local_endpoint = socket_->this_layer <
            NativeSocketType> ().local_endpoint (ec);

    if (ec)
    {
        journal_.error <<
            "Connect to " << remote_address_ <<
            " failed: " << ec.message();
        return detach ("hc");
    }

    assert (state_ == stateConnecting);
    state_ = stateConnected;

    if (! peerFinder_.connected (slot_,
        beast::IPAddressConversion::from_asio (local_endpoint)))
        return detach("dup");

    socket_->set_verify_mode (boost::asio::ssl::verify_none);
    socket_->async_handshake (
        boost::asio::ssl::stream_base::client,
        strand_.wrap (std::bind (&PeerImp::on_connect_ssl,
            std::static_pointer_cast <PeerImp> (shared_from_this ()),
                beast::asio::placeholders::error)));
}

beast::http::message
PeerImp::make_request()
{
    assert (! m_inbound);
    beast::http::message m;
    m.method (beast::http::method_t::http_get);
    m.url ("/");
    m.version (1, 1);
    m.headers.append ("User-Agent", BuildInfo::getFullVersionString());
    //m.headers.append ("Local-Address", socket_->
    m.headers.append ("Remote-Address", remote_address_.to_string());
    m.headers.append ("Upgrade",
        std::string("Ripple/") + to_string (BuildInfo::getCurrentProtocol()));
    m.headers.append ("Connection", "Upgrade");
    m.headers.append ("Connect-As", "Leaf, Peer");
    m.headers.append ("Accept-Encoding", "identity, snappy");
    //m.headers.append ("X-Try-IPs", "192.168.0.1:51234");
    //m.headers.append ("X-Try-IPs", "208.239.114.74:51234");
    //m.headers.append ("A", "BC");
    //m.headers.append ("Content-Length", "0");
    return m;
}

void
PeerImp::on_connect_ssl (error_code ec)
{
    if (detaching_ || ec == boost::asio::error::operation_aborted)
        return;

    if (ec)
    {
        journal_.info <<
            "on_connect_ssl: " << ec.message();
        detach("on_connect_ssl");
        return;
    }

#if RIPPLE_STRUCTURED_OVERLAY_CLIENT
    beast::http::message req (make_request());
    beast::http::write (write_buffer_, req);   
    on_write_http_request (error_code(), 0);

#else
    do_protocol_start();

#endif
}

// Called repeatedly with the http request data
void
PeerImp::on_write_http_request (error_code ec, std::size_t bytes_transferred)
{
    if (detaching_ || ec == boost::asio::error::operation_aborted)
        return;

    if (ec)
    {
        journal_.info <<
            "on_write_http_request: " << ec.message();
        detach("on_write_http_request");
        return;
    }

    write_buffer_.consume (bytes_transferred);

    if (write_buffer_.size() == 0)
    {
        // done sending request, now read the response
        http_message_ = boost::in_place ();
        http_parser_ = boost::in_place (std::ref(*http_message_), false);
        on_read_http_response (error_code(), 0);
        return;
    }

    socket_->async_write_some (write_buffer_.data(),
        strand_.wrap (std::bind (&PeerImp::on_write_http_request,
            shared_from_this(), beast::asio::placeholders::error,
                beast::asio::placeholders::bytes_transferred)));
}

// Called repeatedly with the http response data
void
PeerImp::on_read_http_response (error_code ec, std::size_t bytes_transferred)
{
    if (detaching_ || ec == boost::asio::error::operation_aborted)
        return;

    if (! ec)
    {
        read_buffer_.commit (bytes_transferred);
        bool success;
        std::size_t bytes_consumed;
        std::tie (success, bytes_consumed) = http_parser_->write (
            read_buffer_.data());
        if (! success)
            ec = http_parser_->error();

        if (! ec)
        {
            read_buffer_.consume (bytes_consumed);
            if (http_parser_->complete())
            {
                //
                // TODO Apply response to connection state, then:
                //      - Go into protocol loop, or
                //      - Submit a new request (call on_write_http_request), or
                //      - Close the connection.
                //
                if (http_message_->status() != 200)
                {
                    journal_.info <<
                        "HTTP Response: " << http_message_->reason() <<
                        "(" << http_message_->status() << ")";
                    detach("on_read_http_response");
                    return;
                }
                do_protocol_start ();
                return;
            }
        }
    }

    if (ec == boost::asio::error::eof)
    {
        // remote closed their end
        // VFALCO TODO Clean up the shutdown of the socket
    }

    if (ec)
    {
        journal_.info <<
            "on_read_response: " << ec.message();
        detach("on_read_response");
        return;
    }

    socket_->async_read_some (read_buffer_.prepare (Tuning::readBufferBytes),
        strand_.wrap (std::bind (&PeerImp::on_read_http_response,
            shared_from_this(), beast::asio::placeholders::error,
                beast::asio::placeholders::bytes_transferred)));
}

//------------------------------------------------------------------------------

/*  Completion handlers for server role.
    Logic steps:
        1. Perform SSL handshake
        2. Detect HTTP request or protocol TMHello
        3. If HTTP request received, send HTTP response
        4. Enter protocol loop
*/
void PeerImp::do_accept ()
{
    journal_.info << "Accepted " << remote_address_;

    usage_ = resourceManager_.newInboundEndpoint (remote_address_);
    if (usage_.disconnect ())
    {
        detach ("do_accept");
        return;
    }

    socket_->set_verify_mode (boost::asio::ssl::verify_none);
    socket_->async_handshake (boost::asio::ssl::stream_base::server,
        strand_.wrap (std::bind (&PeerImp::on_accept_ssl,
            std::static_pointer_cast <PeerImp> (shared_from_this ()),
                beast::asio::placeholders::error)));
}

void
PeerImp::on_accept_ssl (error_code ec)
{
    if (detaching_ || ec == boost::asio::error::operation_aborted)
        return;

    if (ec)
    {
        journal_.info <<
            "on_accept_ssl: " << ec.message();
        detach("on_accept_ssl");
        return;
    }

#if RIPPLE_STRUCTURED_OVERLAY_SERVER
    on_read_http_detect (error_code(), 0);

#else
    do_protocol_start();

#endif
}

// Called repeatedly with the initial bytes received on the connection
void
PeerImp::on_read_http_detect (error_code ec, std::size_t bytes_transferred)
{
    if (detaching_ || ec == boost::asio::error::operation_aborted)
        return;

    if (ec)
    {
        journal_.info <<
            "on_read_detect: " << ec.message();
        detach("on_read_detect");
        return;
    }

    read_buffer_.commit (bytes_transferred);
    peer_protocol_detector detector;
    boost::tribool const is_peer_protocol (detector (read_buffer_.data()));
    
    if (is_peer_protocol)
    {
        do_protocol_start();
        return;
    }
    else if (! is_peer_protocol)
    {
        http_message_ = boost::in_place ();
        http_parser_ = boost::in_place (std::ref(*http_message_), true);
        on_read_http_request (error_code(), 0);
        return;
    }

    socket_->async_read_some (read_buffer_.prepare (Tuning::readBufferBytes),
        strand_.wrap (std::bind (&PeerImp::on_read_http_detect,
            shared_from_this(), beast::asio::placeholders::error,
                beast::asio::placeholders::bytes_transferred)));
}

// Called repeatedly with the http request data
void
PeerImp::on_read_http_request (error_code ec, std::size_t bytes_transferred)
{
    if (detaching_ || ec == boost::asio::error::operation_aborted)
        return;

    if (! ec)
    {
        read_buffer_.commit (bytes_transferred);
        bool success;
        std::size_t bytes_consumed;
        std::tie (success, bytes_consumed) = http_parser_->write (
            read_buffer_.data());
        if (! success)
            ec = http_parser_->error();

        if (! ec)
        {
            read_buffer_.consume (bytes_consumed);
            if (http_parser_->complete())
            {
                //
                // TODO Apply headers to connection state.
                //
                if (http_message_->upgrade())
                {
                    std::stringstream ss;
                    ss << 
                        "HTTP/1.1 200 OK\r\n"
                        "Server: " << BuildInfo::getFullVersionString() << "\r\n"
                        "Upgrade: Ripple/1.2\r\n"
                        "Connection: Upgrade\r\n"
                        "\r\n";
                    beast::http::write (write_buffer_, ss.str());
                    on_write_http_response(error_code(), 0);
                }
                else
                {
                    std::stringstream ss;
                    ss << 
                        "HTTP/1.1 400 Bad Request\r\n"
                        "Server: " << BuildInfo::getFullVersionString() << "\r\n"
                        "\r\n"
                        "<html><head></head><body>"
                        "400 Bad Request<br>"
                        "The server requires an Upgrade request."
                        "</body></html>";
                    beast::http::write (write_buffer_, ss.str());
                    on_write_http_response(error_code(), 0);
                }
                return;
            }
        }
    }

    if (ec)
    {
        journal_.info <<
            "on_read_http_request: " << ec.message();
        detach("on_read_http_request");
        return;
    }

    socket_->async_read_some (read_buffer_.prepare (Tuning::readBufferBytes),
        strand_.wrap (std::bind (&PeerImp::on_read_http_request,
            shared_from_this(), beast::asio::placeholders::error,
                beast::asio::placeholders::bytes_transferred)));
}

beast::http::message
PeerImp::make_response (beast::http::message const& req)
{
    beast::http::message resp;
    // Unimplemented
    return resp;
}

// Called repeatedly to send the bytes in the response
void
PeerImp::on_write_http_response (error_code ec, std::size_t bytes_transferred)
{
    if (detaching_ || ec == boost::asio::error::operation_aborted)
        return;

    if (ec)
    {
        journal_.info <<
            "on_write_http_response: " << ec.message();
        detach("on_write_http_response");
        return;
    }

    write_buffer_.consume (bytes_transferred);

    if (write_buffer_.size() == 0)
    {
        do_protocol_start();
        return;
    }

    socket_->async_write_some (write_buffer_.data(),
        strand_.wrap (std::bind (&PeerImp::on_write_http_response,
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
    if (!sendHello ())
    {
        journal_.error << "Unable to send HELLO to " << remote_address_;
        detach ("hello");
        return;
    }

    on_read_protocol (error_code(), 0);
}

// Called repeatedly with protocol message data
void
PeerImp::on_read_protocol (error_code ec, std::size_t bytes_transferred)
{
    if (detaching_ || ec == boost::asio::error::operation_aborted)
        return;

    if (! ec)
    {
        read_buffer_.commit (bytes_transferred);
        ec = message_stream_.write_one (read_buffer_.data());
        read_buffer_.consume (read_buffer_.size());
    }

    if (ec)
    {
        journal_.info <<
            "on_read_protocol: " << ec.message();
        detach("on_read_protocol");
        return;
    }

    socket_->async_read_some (read_buffer_.prepare (Tuning::readBufferBytes),
        strand_.wrap (std::bind (&PeerImp::on_read_protocol,
            shared_from_this(), beast::asio::placeholders::error,
                beast::asio::placeholders::bytes_transferred)));
}

// Called repeatedly to send protcol message data
void
PeerImp::on_write_protocol (error_code ec, std::size_t bytes_transferred)
{
    // (this function isn't called yet)

    if (detaching_ || ec == boost::asio::error::operation_aborted)
        return;
}

void
PeerImp::handleShutdown (boost::system::error_code const& ec)
{
    if (detaching_)
        return;

    if (ec == boost::asio::error::operation_aborted)
        return;

    if (ec)
    {
        journal_.info << "Shutdown: " << ec.message ();
        detach ("hsd");
        return;
    }
}

void
PeerImp::handleWrite (boost::system::error_code const& ec, size_t bytes)
{
    if (detaching_)
        return;

    // Call on IO strand

    send_packet_.reset ();

    if (ec == boost::asio::error::operation_aborted)
        return;

    if (detaching_)
        return;

    if (ec)
    {
        journal_.info << "Write: " << ec.message ();
        detach ("hw");
        return;
    }

    if (!send_queue_.empty ())
    {
        Message::pointer packet = send_queue_.front ();

        if (packet)
        {
            sendForce (packet);
            send_queue_.pop_front ();
        }
    }
}

void
PeerImp::handleVerifyTimer (boost::system::error_code const& ec)
{
    if (detaching_)
        return;

    if (ec == boost::asio::error::operation_aborted)
    {
        // Timer canceled because deadline no longer needed.
    }
    else if (ec)
    {
        journal_.info << "Peer verify timer error";
    }
    else
    {
        //  journal_.info << "Verify: Peer failed to verify in time.";

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

    if (type == protocol::mtHELLO && state_ != stateConnected)
    {
        journal_.warning <<
            "Unexpected TMHello";
        ec = invalid_argument_error();
    }
    else if (type != protocol::mtHELLO && state_ == stateConnected)
    {
        journal_.warning <<
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
PeerImp::on_message (std::shared_ptr <protocol::TMHello> const& m)
{
    error_code ec;

    timer_.cancel ();

    std::uint32_t const ourTime (getApp().getOPs ().getNetworkTimeNC ());
    std::uint32_t const minTime (ourTime - clockToleranceDeltaSeconds);
    std::uint32_t const maxTime (ourTime + clockToleranceDeltaSeconds);

#ifdef BEAST_DEBUG
    if (m->has_nettime ())
    {
        std::int64_t to = ourTime;
        to -= m->nettime ();
        journal_.debug <<
            "Connect: time offset " << to;
    }
#endif

    auto protocol = BuildInfo::make_protocol(m->protoversion());

    // VFALCO TODO Report these failures in the HTTP response

    if (m->has_nettime () &&
        ((m->nettime () < minTime) || (m->nettime () > maxTime)))
    {
        if (m->nettime () > maxTime)
        {
            journal_.info <<
                "Hello: Clock for " << *this <<
                " is off by +" << m->nettime () - ourTime;
        }
        else if (m->nettime () < minTime)
        {
            journal_.info <<
                "Hello: Clock for " << *this <<
                " is off by -" << ourTime - m->nettime ();
        }
    }
    else if (m->protoversionmin () > to_packed (BuildInfo::getCurrentProtocol()))
    {
        journal_.info <<
            "Hello: Disconnect: Protocol mismatch [" <<
            "Peer expects " << to_string (protocol) <<
            " and we run " << to_string (BuildInfo::getCurrentProtocol()) << "]";
    }
    else if (! publicKey_.setNodePublic (m->nodepublic ()))
    {
        journal_.info <<
            "Hello: Disconnect: Bad node public key.";
    }
    else if (! publicKey_.verifyNodePublic (
        secureCookie_, m->nodeproof (), ECDSA::not_strict))
    {
        // Unable to verify they have private key for claimed public key.
        journal_.info <<
            "Hello: Disconnect: Failed to verify session.";
    }
    else
    {
        // Successful connection.
        journal_.info <<
            "Hello: Connect: " << publicKey_.humanNodePublic ();

        if ((protocol != BuildInfo::getCurrentProtocol()) &&
            journal_.active(beast::Journal::Severity::kInfo))
        {
            journal_.info <<
                "Peer protocol: " << to_string (protocol);
        }

        hello_ = *m;

        // Determine if this peer belongs to our cluster and get it's name
        clusterNode_ = getApp().getUNL().nodeInCluster (
            publicKey_, name_);

        if (clusterNode_)
            journal_.info <<
            "Connected to cluster node " << name_;

        assert (state_ == stateConnected);
        // VFALCO TODO Remove this needless state
        state_ = stateHandshaked;

        auto const result = peerFinder_.activate (slot_,
            RipplePublicKey (publicKey_), clusterNode_);

        if (result == PeerFinder::Result::success)
        {
            state_ = stateActive;
            assert(shortId_ == 0);
            shortId_ = overlay_.next_id();
            overlay_.activate(shared_from_this ());

            // XXX Set timer: connection is in grace period to be useful.
            // XXX Set timer: connection idle (idle may vary depending on connection type.)
            if ((hello_.has_ledgerclosed ()) && (
                hello_.ledgerclosed ().size () == (256 / 8)))
            {
                memcpy (closedLedgerHash_.begin (),
                    hello_.ledgerclosed ().data (), 256 / 8);

                if ((hello_.has_ledgerprevious ()) &&
                    (hello_.ledgerprevious ().size () == (256 / 8)))
                {
                    memcpy (previousLedgerHash_.begin (),
                        hello_.ledgerprevious ().data (), 256 / 8);
                    addLedger (previousLedgerHash_);
                }
                else
                {
                    previousLedgerHash_.zero();
                }
            }

            sendGetPeers();
            return ec;
        }

        if (result == PeerFinder::Result::full)
        {
            // TODO Provide correct HTTP response
            auto const redirects = peerFinder_.redirect (slot_);
            send_endpoints (redirects.begin(), redirects.end());
        }
        else
        {
            // TODO Duplicate connection
        }
    }

    // VFALCO Commented this out because we return an error code
    //        to the caller, who calls detach for us.
    //publicKey_.clear ();
    //detach ("recvh");
    ec = invalid_argument_error();

    return ec;
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

    journal_.info << "Received in valid proof of work object from peer";

    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMCluster> const& m)
{
    error_code ec;
    if (!clusterNode_)
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
        resourceManager_.importConsumers (name_, gossip);
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
        peerFinder_.on_legacy_endpoints (list);
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
            endpoint.address = remote_address_.at_port (
                tm.ipv4().ipv4port ());
        }

        endpoints.push_back (endpoint);
    }

    if (! endpoints.empty())
        peerFinder_.on_endpoints (slot_, endpoints);
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
            txID, shortId_, flags))
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

        journal_.debug <<
            "Got transaction from peer " << *this << ": " << txID;

        if (clusterNode_)
        {
            flags |= SF_TRUSTED;
            if (! getConfig().VALIDATION_PRIV.isSet())
            {
                // For now, be paranoid and have each validator
                // check each transaction, regardless of source
                flags |= SF_SIGGOOD;
	    }
	}

        if (getApp().getJobQueue().getJobCount(jtTRANSACTION) > 100)
            journal_.info << "Transaction queue is full";
        else if (getApp().getLedgerMaster().getValidatedLedgerAge() > 240)
            journal_.trace << "No new transactions until synchronized";
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
        journal_.warning << "Transaction invalid: " <<
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
        journal_.warning << "Ledger/TXset data with no nodes";
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
            journal_.info << "Unable to route TX/ledger data reply";
            charge (Resource::feeUnwantedData);
        }

        return ec;
    }

    uint256 hash;

    if (m->ledgerhash ().size () != 32)
    {
        journal_.warning << "TX candidate reply with invalid hash size";
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
                    hash, m, journal_));

        return ec;
    }

    if (!getApp().getInboundLedgers ().gotLedgerData (
        hash, shared_from_this(), m))
    {
        journal_.trace  << "Got data for unwanted ledger";
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
        journal_.warning << "Received proposal is malformed";
        charge (Resource::feeInvalidSignature);
        return ec;
    }

    if (set.has_previousledger () && (set.previousledger ().size () != 32))
    {
        journal_.warning << "Received proposal is malformed";
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
        suppression, shortId_))
    {
        journal_.trace <<
            "Received duplicate proposal from peer " << shortId_;
        return ec;
    }

    RippleAddress signerPublic = RippleAddress::createNodePublic (
        strCopy (set.nodepubkey ()));

    if (signerPublic == getConfig ().VALIDATION_PUB)
    {
        journal_.trace <<
            "Received our own proposal from peer " << shortId_;
        return ec;
    }

    bool isTrusted = getApp().getUNL ().nodeInUNL (signerPublic);
    if (!isTrusted && getApp().getFeeTrack ().isLoadedLocal ())
    {
        journal_.debug << "Dropping UNTRUSTED proposal due to load";
        return ec;
    }

    journal_.trace <<
        "Received " << (isTrusted ? "trusted" : "UNTRUSTED") <<
        " proposal from " << shortId_;

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
                m, proposal, consensusLCL, publicKey_,
                    std::weak_ptr<Peer> (shared_from_this ()), clusterNode_,
                        journal_));
    return ec;
}

PeerImp::error_code
PeerImp::on_message (std::shared_ptr <protocol::TMStatusChange> const& m)
{
    error_code ec;
    journal_.trace << "Received status change from peer " <<
                        to_string (this);

    if (!m->has_networktime ())
        m->set_networktime (getApp().getOPs ().getNetworkTimeNC ());

    if (!last_status_.has_newstatus () || m->has_newstatus ())
        last_status_ = *m;
    else
    {
        // preserve old status
        protocol::NodeStatus status = last_status_.newstatus ();
        last_status_ = *m;
        m->set_newstatus (status);
    }

    if (m->newevent () == protocol::neLOST_SYNC)
    {
        if (!closedLedgerHash_.isZero ())
        {
            journal_.trace << "peer has lost sync " << to_string (this);
            closedLedgerHash_.zero ();
        }

        previousLedgerHash_.zero ();
        return ec;
    }

    if (m->has_ledgerhash () && (m->ledgerhash ().size () == (256 / 8)))
    {
        // a peer has changed ledgers
        memcpy (closedLedgerHash_.begin (), m->ledgerhash ().data (), 256 / 8);
        addLedger (closedLedgerHash_);
        journal_.trace << "peer LCL is " << closedLedgerHash_ <<
                            " " << to_string (this);
    }
    else
    {
        journal_.trace << "peer has no ledger hash" << to_string (this);
        closedLedgerHash_.zero ();
    }

    if (m->has_ledgerhashprevious () &&
        m->ledgerhashprevious ().size () == (256 / 8))
    {
        memcpy (previousLedgerHash_.begin (),
            m->ledgerhashprevious ().data (), 256 / 8);
        addLedger (previousLedgerHash_);
    }
    else previousLedgerHash_.zero ();

    if (m->has_firstseq () && m->has_lastseq())
    {
        minLedger_ = m->firstseq ();
        maxLedger_ = m->lastseq ();

        // Work around some servers that report sequences incorrectly
        if (minLedger_ == 0)
            maxLedger_ = 0;
        if (maxLedger_ == 0)
            minLedger_ = 0;
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
        journal_.warning << "Too small validation from peer";
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
            journal_.trace << "Validation is more than two minutes old";
            charge (Resource::feeUnwantedData);
            return ec;
        }

        if (! getApp().getHashRouter ().addSuppressionPeer (
            s.getSHA512Half(), shortId_))
        {
            journal_.trace << "Validation is duplicate";
            return ec;
        }

        bool isTrusted = getApp().getUNL ().nodeInUNL (val->getSignerPublic ());
        if (isTrusted || !getApp().getFeeTrack ().isLoadedLocal ())
        {
            getApp().getJobQueue ().addJob (
                isTrusted ? jtVALIDATION_t : jtVALIDATION_ut,
                "recvValidation->checkValidation",
                std::bind (&PeerImp::checkValidation, std::placeholders::_1,
                    &overlay_, val, isTrusted, clusterNode_, m,
                        std::weak_ptr<Peer> (shared_from_this ()),
                            journal_));
        }
        else
        {
            journal_.debug <<
                "Dropping UNTRUSTED validation due to load";
        }
    }
    catch (...)
    {
        journal_.warning <<
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

        journal_.trace << "GetObjByHash had " << reply.objects_size () <<
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
                            journal_.active(beast::Journal::Severity::kDebug))
                            journal_.debug <<
                                "Received full fetch pack for " << pLSeq;

                        pLSeq = obj.ledgerseq ();
                        pLDo = !getApp().getOPs ().haveLedger (pLSeq);

                        if (!pLDo)
                                journal_.debug <<
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
            journal_.active(beast::Journal::Severity::kDebug))
            journal_.debug << "Received partial fetch pack for " << pLSeq;

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
        journal_.trace << "Received request for TX candidate set data "
                        << to_string (this);

        if ((!packet.has_ledgerhash () || packet.ledgerhash ().size () != 32))
        {
            charge (Resource::feeInvalidRequest);
            journal_.warning << "invalid request for TX candidate set data";
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
                journal_.debug << "Trying to route TX set request";

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
                    journal_.info << "Unable to route TX set request";
                    return;
                }

                Peer::ptr const& selectedPeer = usablePeers [
                    rand () % usablePeers.size ()];
                packet.set_requestcookie (getShortId ());
                selectedPeer->send (std::make_shared<Message> (
                    packet, protocol::mtGET_LEDGER));
                return;
            }

            journal_.error << "We do not have the map our peer wants "
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
        if (getApp().getFeeTrack().isLoadedLocal() && !clusterNode_)
        {
            journal_.debug << "Too busy to fetch ledger data";
            return;
        }

        // Figure out what ledger they want
        journal_.trace << "Received request for ledger data "
                        << to_string (this);
        Ledger::pointer ledger;

        if (packet.has_ledgerhash ())
        {
            uint256 ledgerhash;

            if (packet.ledgerhash ().size () != 32)
            {
                charge (Resource::feeInvalidRequest);
                journal_.warning << "Invalid request";
                return;
            }

            memcpy (ledgerhash.begin (), packet.ledgerhash ().data (), 32);
            logMe += "LedgerHash:";
            logMe += to_string (ledgerhash);
            ledger = getApp().getLedgerMaster ().getLedgerByHash (ledgerhash);

            if (!ledger && journal_.trace)
                journal_.trace << "Don't have ledger " << ledgerhash;

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
                    journal_.trace << "Unable to route ledger request";
                    return;
                }

                Peer::ptr const& selectedPeer = usablePeers [
                    rand () % usablePeers.size ()];
                packet.set_requestcookie (getShortId ());
                selectedPeer->send (
                    std::make_shared<Message> (packet, protocol::mtGET_LEDGER));
                journal_.debug << "Ledger request routed";
                return;
            }
        }
        else if (packet.has_ledgerseq ())
        {
            if (packet.ledgerseq() <
                    getApp().getLedgerMaster().getEarliestFetch())
            {
                journal_.debug << "Peer requests early ledger";
                return;
            }
            ledger = getApp().getLedgerMaster ().getLedgerBySeq (
                packet.ledgerseq ());
            if (!ledger && journal_.debug)
                journal_.debug << "Don't have ledger " << packet.ledgerseq ();
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
            journal_.warning << "Can't figure out what ledger they want";
            return;
        }

        if ((!ledger) || (packet.has_ledgerseq () && (
            packet.ledgerseq () != ledger->getLedgerSeq ())))
        {
            charge (Resource::feeInvalidRequest);

            if (journal_.warning && ledger)
                journal_.warning << "Ledger has wrong sequence";

            return;
        }

            if (!packet.has_ledgerseq() && (ledger->getLedgerSeq() <
                getApp().getLedgerMaster().getEarliestFetch()))
            {
                journal_.debug << "Peer requests early ledger";
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
            journal_.trace << "They want ledger base data";
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
        journal_.warning << "Can't find map or empty request";
        charge (Resource::feeInvalidRequest);
        return;
    }

    journal_.trace << "Request: " << logMe;

    for (int i = 0; i < packet.nodeids ().size (); ++i)
    {
        SHAMapNodeID mn (packet.nodeids (i).data (), packet.nodeids (i).size ());

        if (!mn.isValid ())
        {
            journal_.warning << "Request for invalid node: " << logMe;
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
                journal_.trace <<
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
                journal_.warning << "getNodeFat returns false";
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

            journal_.warning <<
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
    if (! strand_.running_in_this_thread ())
    {
        strand_.post (std::bind (&PeerImp::detach,
            shared_from_this (), rsn, graceful));
        return;
    }

    if (!detaching_)
    {
        // NIKB TODO No - a race is NOT ok. This needs to be fixed
        //           to have PeerFinder work reliably.
        detaching_  = true; // Race is ok.

        if (was_canceled_)
            peerFinder_.on_cancel (slot_);
        else
            peerFinder_.on_closed (slot_);

        if (state_ == stateActive)
            overlay_.onPeerDisconnect (shared_from_this ());

        state_ = stateGracefulClose;

        if (clusterNode_ && journal_.active(beast::Journal::Severity::kWarning))
            journal_.warning << "Cluster peer " << name_ <<
                                    " detached: " << rsn;

        send_queue_.clear ();

        (void) timer_.cancel ();

        if (graceful)
        {
            socket_->async_shutdown (
                strand_.wrap ( std::bind(
                    &PeerImp::handleShutdown,
                    std::static_pointer_cast <PeerImp> (shared_from_this ()),
                    beast::asio::placeholders::error)));
        }
        else
        {
            socket_->cancel ();
        }

        // VFALCO TODO Stop doing this.
        if (publicKey_.isValid ())
            publicKey_.clear ();       // Be idempotent.
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
    if (!detaching_)
    {
        send_packet_ = packet;

        boost::asio::async_write (*socket_,
            boost::asio::buffer (packet->getBuffer ()),
            strand_.wrap (std::bind (
                &PeerImp::handleWrite,
                std::static_pointer_cast <PeerImp> (shared_from_this ()),
                beast::asio::placeholders::error,
                beast::asio::placeholders::bytes_transferred)));
    }
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
    SSL* ssl = socket_->ssl_handle ();

    if (!ssl)
    {
        journal_.error << "Cookie generation: No underlying connection";
        return false;
    }

    unsigned char sha1[64];
    unsigned char sha2[64];

    if (!hashLatestFinishedMessage(ssl, sha1, SSL_get_finished))
    {
        journal_.error << "Cookie generation: local setup not complete";
        return false;
    }

    if (!hashLatestFinishedMessage(ssl, sha2, SSL_get_peer_finished))
    {
        journal_.error << "Cookie generation: peer setup not complete";
        return false;
    }

    // If both messages hash to the same value (i.e. match) something is
    // wrong. This would cause the resulting cookie to be 0.
    if (memcmp (sha1, sha2, sizeof (sha1)) == 0)
    {
        journal_.error << "Cookie generation: identical finished messages";
        return false;
    }

    for (size_t i = 0; i < sizeof (sha1); ++i)
        sha1[i] ^= sha2[i];

    // Finally, derive the actual cookie for the values that we have
    // calculated.
    secureCookie_ = Serializer::getSHA512Half (sha1, sizeof(sha1));

    return true;
}

bool
PeerImp::sendHello ()
{
    if (!calculateSessionCookie())
        return false;

    Blob vchSig;
    getApp().getLocalCredentials ().getNodePrivate ().signNodePrivate (
        secureCookie_, vchSig);

    protocol::TMHello h;

    h.set_protoversion (to_packed (BuildInfo::getCurrentProtocol()));
    h.set_protoversionmin (to_packed (BuildInfo::getMinimumProtocol()));
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

    Message::pointer packet = std::make_shared<Message> (
        h, protocol::mtHELLO);
    send (packet);

    return true;
}

void
PeerImp::addLedger (uint256 const& hash)
{
    std::lock_guard<std::mutex> sl(recentLock_);

    if (std::find (recentLedgers_.begin(),
        recentLedgers_.end(), hash) != recentLedgers_.end())
        return;

    // VFALCO TODO See if a sorted vector would be better.

    if (recentLedgers_.size () == 128)
        recentLedgers_.pop_front ();

    recentLedgers_.push_back (hash);
}

void
PeerImp::addTxSet (uint256 const& hash)
{
    std::lock_guard<std::mutex> sl(recentLock_);

    if (std::find (recentTxSets_.begin (),
        recentTxSets_.end (), hash) != recentTxSets_.end ())
        return;

    if (recentTxSets_.size () == 128)
        recentTxSets_.pop_front ();

    recentTxSets_.push_back (hash);
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
        journal_.info << "Too busy to make fetch pack";
        return;
    }

    if (packet->ledgerhash ().size () != 32)
    {
        journal_.warning << "FetchPack hash size malformed";
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
            journal_.warning << "Failed to solve proof of work";
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
        // Expired?
        if (stx->isFieldPresent(sfLastLedgerSequence) &&
            (stx->getFieldU32 (sfLastLedgerSequence) <
            getApp().getLedgerMaster().getValidLedgerIndex()))
        {
            getApp().getHashRouter().setFlag(stx->getTransactionID(), SF_BAD);
            charge (peer, Resource::feeUnwantedData);
            return;
        }

        auto validate = (flags & SF_SIGGOOD) ? Validate::NO : Validate::YES;
        auto tx = std::make_shared<Transaction> (stx, validate);

        if (tx->getStatus () == INVALID)
        {
            getApp().getHashRouter ().setFlag (stx->getTransactionID (), SF_BAD);
            charge (peer, Resource::feeInvalidSignature);
            return;
        }
        else
        {
            getApp().getHashRouter ().setFlag (
                stx->getTransactionID (), SF_SIGGOOD);
        }

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
