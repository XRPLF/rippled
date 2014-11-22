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
#include <ripple/basics/UptimeTimer.h>
#include <ripple/core/JobQueue.h>
#include <ripple/json/json_reader.h>
#include <ripple/overlay/impl/TMHello.h>
#include <ripple/overlay/impl/PeerImp.h>
#include <ripple/overlay/impl/Tuning.h>
#include <ripple/resource/Fees.h>
#include <ripple/server/ServerHandler.h>
#include <ripple/protocol/BuildInfo.h>
#include <beast/streams/debug_ostream.h>
#include <beast/weak_fn.h>
#include <functional>
#include <beast/cxx14/memory.h> // <memory>
#include <sstream>

namespace ripple {

PeerImp::PeerImp (id_t id, endpoint_type remote_endpoint,
    PeerFinder::Slot::ptr const& slot, beast::http::message&& request,
        protocol::TMHello const& hello, RippleAddress const& publicKey,
            Resource::Consumer consumer,
                std::unique_ptr<beast::asio::ssl_bundle>&& ssl_bundle,
                    OverlayImpl& overlay)
    : Child (overlay)
    , id_(id)
    , sink_(deprecatedLogs().journal("Peer"), makePrefix(id))
    , p_sink_(deprecatedLogs().journal("Protocol"), makePrefix(id))
    , journal_ (sink_)
    , p_journal_(p_sink_)
    , ssl_bundle_(std::move(ssl_bundle))
    , socket_ (ssl_bundle_->socket)
    , stream_ (ssl_bundle_->stream)
    , strand_ (socket_.get_io_service())
    , timer_ (socket_.get_io_service())
    , remote_address_ (
        beast::IPAddressConversion::from_asio(remote_endpoint))
    , overlay_ (overlay)
    , m_inbound (true)
    , state_ (State::active)
    , publicKey_(publicKey)
    , hello_(hello)
    , usage_(consumer)
    , slot_ (slot)
    , http_message_(std::move(request))
{
}

PeerImp::~PeerImp ()
{
    if (cluster())
        if (journal_.warning) journal_.warning <<
            name_ << " left cluster";
    if (state_ == State::active)
    {
        assert(publicKey_.isSet());
        assert(publicKey_.isValid());
        overlay_.onPeerDeactivate(id_, publicKey_);
    }
    overlay_.peerFinder().on_closed (slot_);
    overlay_.remove (slot_);
}

void
PeerImp::run()
{
    if (m_inbound)
    {
        if (read_buffer_.size() > 0)
            doLegacyAccept();
        else
            doAccept();
    }
    else
    {
        assert (state_ == State::active);
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
        doProtocolStart(false);
    }
}

void
PeerImp::stop()
{
    if(! strand_.running_in_this_thread())
        return strand_.post(std::bind (
            &PeerImp::stop, shared_from_this()));
    if (socket_.is_open())
    {
        // The rationale for using different severity levels is that
        // outbound connections are under our control and may be logged
        // at a higher level, but inbound connections are more numerous and
        // uncontrolled so to prevent log flooding the severity is reduced.
        //
        if(m_inbound)
        {
            if(journal_.debug) journal_.debug <<
                "Stop";
        }
        else
        {
            if(journal_.info) journal_.info <<
                "Stop";
        }
    }
    close();
}

//------------------------------------------------------------------------------

void
PeerImp::send (Message::pointer const& m)
{
    if (! strand_.running_in_this_thread())
        return strand_.post(std::bind (
            &PeerImp::send, shared_from_this(), m));
    if(gracefulClose_)
        return;
    if(detaching_)
        return;
    send_queue_.push(m);
    if(send_queue_.size() > 1)
        return;
    setTimer();
    boost::asio::async_write (stream_, boost::asio::buffer(
        send_queue_.front()->getBuffer()), strand_.wrap(std::bind(
            &PeerImp::onWriteMessage, shared_from_this(),
                beast::asio::placeholders::error,
                    beast::asio::placeholders::bytes_transferred)));
}

void
PeerImp::charge (Resource::Charge const& fee)
{
    if ((usage_.charge(fee) == Resource::drop) && usage_.disconnect())
        fail("charge: Resources");
}

//------------------------------------------------------------------------------

Json::Value
PeerImp::json()
{
    Json::Value ret (Json::objectValue);

    ret["public_key"]   = publicKey_.ToString ();
    ret["address"]      = remote_address_.to_string();

    if (m_inbound)
        ret["inbound"] = true;

    if (cluster())
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
            p_journal_.warning <<
                "Unknown status: " << last_status_.newstatus ();
        }
    }

    return ret;
}

//------------------------------------------------------------------------------

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

void
PeerImp::close()
{
    assert(strand_.running_in_this_thread());
    if (socket_.is_open())
    {
        detaching_ = true; // DEPRECATED
        error_code ec;
        timer_.cancel(ec);
        socket_.close(ec);
        if(m_inbound)
        {
            if(journal_.debug) journal_.debug <<
                "Closed";
        }
        else
        {
            if(journal_.info) journal_.info <<
                "Closed";
        }
    }
}

void
PeerImp::fail(std::string const& reason)
{
    assert(strand_.running_in_this_thread());
    if (socket_.is_open())
        if (journal_.debug) journal_.debug <<
            reason;
    close();
}

void
PeerImp::fail(std::string const& name, error_code ec)
{
    assert(strand_.running_in_this_thread());
    if (socket_.is_open())
        if (journal_.debug) journal_.debug <<
            name << ": " << ec.message();
    close();
}

void
PeerImp::gracefulClose()
{
    assert(strand_.running_in_this_thread());
    assert(socket_.is_open());
    assert(! gracefulClose_);
    gracefulClose_ = true;
#if 0
    // Flush messages
    while(send_queue_.size() > 1)
        send_queue_.pop_back();
#endif
    if (send_queue_.size() > 0)
        return;
    setTimer();
    stream_.async_shutdown(strand_.wrap(std::bind(&PeerImp::onShutdown,
        shared_from_this(), beast::asio::placeholders::error)));
}

void
PeerImp::setTimer()
{
    error_code ec;
    timer_.expires_from_now(std::chrono::seconds(15), ec);
    if (ec)
    {
        if (journal_.error) journal_.error <<
            "setTimer: " << ec.message();
        return;
    }
    timer_.async_wait(strand_.wrap(std::bind(&PeerImp::onTimer,
        shared_from_this(), beast::asio::placeholders::error)));
}

// convenience for ignoring the error code
void
PeerImp::cancelTimer()
{
    error_code ec;
    timer_.cancel(ec);
}

//------------------------------------------------------------------------------

std::string
PeerImp::makePrefix(id_t id)
{
    std::stringstream ss;
    ss << "[" << std::setfill('0') << std::setw(3) << id << "] ";
    return ss.str();
}

beast::http::message
PeerImp::makeRequest(boost::asio::ip::address const& remote_address)
{
    beast::http::message m;
    m.method (beast::http::method_t::http_get);
    m.url ("/");
    m.version (1, 1);
    m.headers.append ("User-Agent", BuildInfo::getFullVersionString());
    m.headers.append ("Upgrade", "RTXP/1.3");
        //std::string("RTXP/") + to_string (BuildInfo::getCurrentProtocol()));
    m.headers.append ("Connection", "Upgrade");
    m.headers.append ("Connect-As", "Peer");
    //m.headers.append ("Connect-As", "Leaf, Peer");
    //m.headers.append ("Accept-Encoding", "identity");
    //m.headers.append ("Local-Address", stream_.
    //m.headers.append ("X-Try-IPs", "192.168.0.1:51234");
    //m.headers.append ("X-Try-IPs", "208.239.114.74:51234");
    //m.headers.append ("A", "BC");
    //m.headers.append ("Content-Length", "0");
    return m;
}

void
PeerImp::onTimer (error_code const& ec)
{
    if (! socket_.is_open())
        return;

    if (ec == boost::asio::error::operation_aborted)
        return;

    if (ec)
    {
        // This should never happen
        if (journal_.error) journal_.error <<
            "onTimer: " << ec.message();
        return close();
    }

    fail("Timeout");
}

void
PeerImp::onShutdown(error_code ec)
{
    cancelTimer();
    // If we don't get eof then something went wrong
    if (! ec)
    {
        if (journal_.error) journal_.error <<
            "onShutdown: expected error condition";
        return close();
    }
    if (ec != boost::asio::error::eof)
        return fail("onShutdown", ec);
    close();
}

//------------------------------------------------------------------------------

void PeerImp::doLegacyAccept()
{
    assert(read_buffer_.size() > 0);
    if(journal_.debug) journal_.debug <<
        "doLegacyAccept: " << remote_address_;
    usage_ = overlay_.resourceManager().newInboundEndpoint (remote_address_);
    if (usage_.disconnect ())
        return fail("doLegacyAccept: Resources");
    doProtocolStart(true);
}

void PeerImp::doAccept()
{
    assert(read_buffer_.size() == 0);
    assert(http_message_.upgrade());

    if(journal_.debug) journal_.debug <<
        "doAccept: " << remote_address_;

    bool success;
    uint256 sharedValue;
    std::tie(sharedValue, success) = makeSharedValue(
        ssl_bundle_->stream.native_handle(), journal_);
    // This shouldn't fail since we already computed
    // the shared value successfully in OverlayImpl
    if(! success)
        return fail("makeSharedValue: Unexpected failure");

    // TODO Apply headers to connection state.

    auto resp = makeResponse(http_message_, sharedValue);
    beast::http::write (write_buffer_, resp);

    auto const protocol = BuildInfo::make_protocol(hello_.protoversion());
    if(journal_.info) journal_.info <<
        "Protocol: " << to_string(protocol);
    if(journal_.info) journal_.info <<
        "Public Key: " << publicKey_.humanNodePublic();
    bool const cluster = getApp().getUNL().nodeInCluster(publicKey_, name_);
    if (cluster)
        if (journal_.info) journal_.info <<
            "Cluster name: " << name_;

    overlay_.activate(shared_from_this());

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

    onWriteResponse(error_code(), 0);
}

beast::http::message
PeerImp::makeResponse (beast::http::message const& req,
    uint256 const& sharedValue)
{
    beast::http::message resp;
    resp.request(false);
    resp.status(101);
    resp.reason("Switching Protocols");
    resp.version(req.version());
    resp.headers.append("Connection", "Upgrade");
    resp.headers.append("Upgrade", "RTXP/1.2");
    resp.headers.append("Connect-AS", "Peer");
    resp.headers.append("Server", BuildInfo::getFullVersionString());
    protocol::TMHello hello = buildHello(sharedValue, getApp());
    appendHello(resp, hello);
    return resp;
}

// Called repeatedly to send the bytes in the response
void
PeerImp::onWriteResponse (error_code ec, std::size_t bytes_transferred)
{
    cancelTimer();
    if(! socket_.is_open())
        return;
    if(ec == boost::asio::error::operation_aborted)
        return;
    if(ec)
        return fail("onWriteResponse", ec);
    if(journal_.trace)
    {
        if (bytes_transferred > 0) journal_.trace <<
            "onWriteResponse: " << bytes_transferred << " bytes";
        else journal_.trace <<
            "onWriteResponse";
    }

    write_buffer_.consume (bytes_transferred);
    if (write_buffer_.size() == 0)
        return doProtocolStart(false);

    setTimer();
    stream_.async_write_some (write_buffer_.data(),
        strand_.wrap (std::bind (&PeerImp::onWriteResponse,
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
PeerImp::doProtocolStart(bool legacy)
{
    if (legacy && !sendHello ())
    {
        journal_.error << "Unable to send HELLO to " << remote_address_;
        return fail ("hello");
    }

    onReadMessage(error_code(), 0);
}

// Called repeatedly with protocol message data
void
PeerImp::onReadMessage (error_code ec, std::size_t bytes_transferred)
{
    if(! socket_.is_open())
        return;
    if(ec == boost::asio::error::operation_aborted)
        return;
    if(ec == boost::asio::error::eof)
    {
        if(journal_.info) journal_.info <<
            "EOF";
        return gracefulClose();
    }
    if(ec)
        return fail("onReadMessage", ec);
    if(journal_.trace)
    {
        if (bytes_transferred > 0) journal_.trace <<
            "onReadMessage: " << bytes_transferred << " bytes";
        else journal_.trace <<
            "onReadMessage";
    }

    read_buffer_.commit (bytes_transferred);

    while (read_buffer_.size() > 0)
    {
        std::size_t bytes_consumed;
        std::tie(bytes_consumed, ec) = invokeProtocolMessage(
            read_buffer_.data(), *this);
        if (ec)
            return fail("onReadMessage", ec);
        if (! stream_.next_layer().is_open())
            return;
        if (bytes_consumed == 0)
            break;
        read_buffer_.consume (bytes_consumed);
    }
    if(gracefulClose_)
        return;
    // Timeout on writes only
    stream_.async_read_some (read_buffer_.prepare (Tuning::readBufferBytes),
        strand_.wrap (std::bind (&PeerImp::onReadMessage,
            shared_from_this(), beast::asio::placeholders::error,
                beast::asio::placeholders::bytes_transferred)));
}

void
PeerImp::onWriteMessage (error_code ec, std::size_t bytes_transferred)
{
    cancelTimer();
    if(! socket_.is_open())
        return;
    if(ec == boost::asio::error::operation_aborted)
        return;
    if(ec)
        return fail("onWriteMessage", ec);
    if(journal_.trace)
    {
        if (bytes_transferred > 0) journal_.trace <<
            "onWriteMessage: " << bytes_transferred << " bytes";
        else journal_.trace <<
            "onWriteMessage";
    }

    assert(! send_queue_.empty());
    send_queue_.pop();
    if (! send_queue_.empty())
    {
        // Timeout on writes only
        setTimer();
        return boost::asio::async_write (stream_, boost::asio::buffer(
            send_queue_.front()->getBuffer()), strand_.wrap(std::bind(
                &PeerImp::onWriteMessage, shared_from_this(),
                    beast::asio::placeholders::error,
                        beast::asio::placeholders::bytes_transferred)));
    }

    if (gracefulClose_)
    {
        setTimer();
        return stream_.async_shutdown(strand_.wrap(std::bind(
            &PeerImp::onShutdown, shared_from_this(),
                beast::asio::placeholders::error)));
    }
}

//------------------------------------------------------------------------------
//
// ProtocolHandler
//
//------------------------------------------------------------------------------

PeerImp::error_code
PeerImp::onMessageUnknown (std::uint16_t type)
{
    error_code ec;
    // TODO
    return ec;
}

PeerImp::error_code
PeerImp::onMessageBegin (std::uint16_t type,
    std::shared_ptr <::google::protobuf::Message> const& m)
{
    error_code ec;

    if (type == protocol::mtHELLO && state_ != State::connected)
    {
        journal_.warning <<
            "Unexpected TMHello";
        ec = invalid_argument_error();
    }
    else if (type != protocol::mtHELLO && state_ == State::connected)
    {
        journal_.warning <<
            "Expected TMHello";
        ec = invalid_argument_error();
    }

    if (! ec)
    {
        load_event_ = getApp().getJobQueue ().getLoadEventAP (
            jtPEER, protocolMessageName(type));
    }

    return ec;
}

void
PeerImp::onMessageEnd (std::uint16_t,
    std::shared_ptr <::google::protobuf::Message> const&)
{
    load_event_.reset();
}

void
PeerImp::onMessage (std::shared_ptr <protocol::TMHello> const& m)
{
    std::uint32_t const ourTime (getApp().getOPs ().getNetworkTimeNC ());
    std::uint32_t const minTime (ourTime - clockToleranceDeltaSeconds);
    std::uint32_t const maxTime (ourTime + clockToleranceDeltaSeconds);

#ifdef BEAST_DEBUG
    if (m->has_nettime ())
    {
        std::int64_t to = ourTime;
        to -= m->nettime ();
        journal_.debug <<
            "Time offset: " << to;
    }
#endif

    // VFALCO TODO Report these failures in the HTTP response

    auto const protocol = BuildInfo::make_protocol(m->protoversion());
    if (m->has_nettime () &&
        ((m->nettime () < minTime) || (m->nettime () > maxTime)))
    {
        if (m->nettime () > maxTime)
        {
            journal_.info <<
                "Hello: Clock off by +" << m->nettime () - ourTime;
        }
        else if (m->nettime () < minTime)
        {
            journal_.info <<
                "Hello: Clock off by -" << ourTime - m->nettime ();
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
        sharedValue_, m->nodeproof (), ECDSA::not_strict))
    {
        // Unable to verify they have private key for claimed public key.
        journal_.info <<
            "Hello: Disconnect: Failed to verify session.";
    }
    else
    {
        if(journal_.info) journal_.info <<
            "Protocol: " << to_string(protocol);
        if(journal_.info) journal_.info <<
            "Public Key: " << publicKey_.humanNodePublic();
        bool const cluster = getApp().getUNL().nodeInCluster(publicKey_, name_);
        if (cluster)
            if (journal_.info) journal_.info <<
                "Cluster name: " << name_;

        assert (state_ == State::connected);
        // VFALCO TODO Remove this needless state
        state_ = State::handshaked;
        hello_ = *m;

        auto const result = overlay_.peerFinder().activate (slot_,
            RipplePublicKey (publicKey_), cluster);

        if (result == PeerFinder::Result::success)
        {
            state_ = State::active;
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

            return sendGetPeers();
        }

        if (result == PeerFinder::Result::full)
        {
            // TODO Provide correct HTTP response
            auto const redirects = overlay_.peerFinder().redirect (slot_);
            sendEndpoints (redirects.begin(), redirects.end());
            return gracefulClose();
        }
        else if (result == PeerFinder::Result::duplicate)
        {
            return fail("Duplicate public key");
        }
    }

    fail("TMHello invalid");
}

void
PeerImp::onMessage (std::shared_ptr <protocol::TMPing> const& m)
{
    if (m->type () == protocol::TMPing::ptPING)
    {
        m->set_type (protocol::TMPing::ptPONG);
        send (std::make_shared<Message> (*m, protocol::mtPING));
    }
}

void
PeerImp::onMessage (std::shared_ptr <protocol::TMProofWork> const& m)
{
    if (m->has_response ())
    {
        // this is an answer to a proof of work we requested
        if (m->response ().size () != (256 / 8))
            return charge (Resource::feeInvalidRequest);

        uint256 response;
        memcpy (response.begin (), m->response ().data (), 256 / 8);

        // VFALCO TODO Use a dependency injection here
        PowResult r = getApp().getProofOfWorkFactory ().checkProof (
            m->token (), response);

        if (r == powOK)
            // credit peer
            // WRITEME
            return;

        // return error message
        // WRITEME
        if (r != powTOOEASY)
            charge (Resource::feeBadProofOfWork);

        return;
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
            return charge (Resource::feeInvalidRequest);

        memcpy (challenge.begin (), m->challenge ().data (), 256 / 8);
        memcpy (target.begin (), m->target ().data (), 256 / 8);
        ProofOfWork::pointer pow = std::make_shared<ProofOfWork> (
            m->token (), m->iterations (), challenge, target);

        if (!pow->isValid ())
            return charge (Resource::feeInvalidRequest);

#if 0   // Until proof of work is completed, don't do it
        getApp().getJobQueue ().addJob (
            jtPROOFWORK,
            "recvProof->doProof",
            std::bind (&PeerImp::doProofOfWork, std::placeholders::_1,
                        std::weak_ptr <PeerImp> (shared_from_this ()), pow));
#endif

        return;
    }

    p_journal_.info << "Bad proof of work";
}

void
PeerImp::onMessage (std::shared_ptr <protocol::TMCluster> const& m)
{
    // VFALCO NOTE I think we should drop the peer immediately
    if (! cluster())
        return charge (Resource::feeUnwantedData);

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
        overlay_.resourceManager().importConsumers (name_, gossip);
    }

    getApp().getFeeTrack().setClusterFee(getApp().getUNL().getClusterFee());
}

void
PeerImp::onMessage (std::shared_ptr <protocol::TMGetPeers> const& m)
{
    // VFALCO TODO This message is now obsolete due to PeerFinder
}

void
PeerImp::onMessage (std::shared_ptr <protocol::TMPeers> const& m)
{
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
        overlay_.peerFinder().on_legacy_endpoints (list);
}

void
PeerImp::onMessage (std::shared_ptr <protocol::TMEndpoints> const& m)
{
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
        overlay_.peerFinder().on_endpoints (slot_, endpoints);
}

void
PeerImp::onMessage (std::shared_ptr <protocol::TMTransaction> const& m)
{
    Serializer s (m->rawtransaction ());

    try
    {
        SerializerIterator sit (s);
        STTx::pointer stx = std::make_shared <
            STTx> (std::ref (sit));
        uint256 txID = stx->getTransactionID ();

        int flags;

        if (! getApp().getHashRouter ().addSuppressionPeer (
            txID, id_, flags))
        {
            // we have seen this transaction recently
            if (flags & SF_BAD)
            {
                charge (Resource::feeInvalidSignature);
                return;
            }

            if (!(flags & SF_RETRY))
                return;
        }

        p_journal_.debug <<
            "Got tx " << txID;

        if (cluster())
        {
            if (! m->has_deferred () || ! m->deferred ())
            {
                // Skip local checks if a server we trust
                // put the transaction in its open ledger
                flags |= SF_TRUSTED;
            }

            if (! getConfig().VALIDATION_PRIV.isSet())
            {
                // For now, be paranoid and have each validator
                // check each transaction, regardless of source
                flags |= SF_SIGGOOD;
            }
        }

        if (getApp().getJobQueue().getJobCount(jtTRANSACTION) > 100)
            p_journal_.info << "Transaction queue is full";
        else if (getApp().getLedgerMaster().getValidatedLedgerAge() > 240)
            p_journal_.trace << "No new transactions until synchronized";
        else
            getApp().getJobQueue ().addJob (jtTRANSACTION,
                "recvTransaction->checkTransaction",
                std::bind(beast::weak_fn(&PeerImp::checkTransaction,
                shared_from_this()), std::placeholders::_1, flags, stx));
    }
    catch (...)
    {
        p_journal_.warning << "Transaction invalid: " <<
            s.getHex();
    }
}

void
PeerImp::onMessage (std::shared_ptr <protocol::TMGetLedger> const& m)
{
    getApp().getJobQueue().addJob (jtPACK, "recvGetLedger", std::bind(
        beast::weak_fn(&PeerImp::getLedger, shared_from_this()), m));
}

void
PeerImp::onMessage (std::shared_ptr <protocol::TMLedgerData> const& m)
{
    protocol::TMLedgerData& packet = *m;

    if (m->nodes ().size () <= 0)
    {
        p_journal_.warning << "Ledger/TXset data with no nodes";
        return;
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
            p_journal_.info << "Unable to route TX/ledger data reply";
            charge (Resource::feeUnwantedData);
        }
        return;
    }

    uint256 hash;

    if (m->ledgerhash ().size () != 32)
    {
        p_journal_.warning << "TX candidate reply with invalid hash size";
        charge (Resource::feeInvalidRequest);
        return;
    }

    memcpy (hash.begin (), m->ledgerhash ().data (), 32);

    if (m->type () == protocol::liTS_CANDIDATE)
    {
        // got data for a candidate transaction set
        getApp().getJobQueue().addJob(jtTXN_DATA, "recvPeerData", std::bind(
            beast::weak_fn(&PeerImp::peerTXData, shared_from_this()),
            std::placeholders::_1, hash, m, p_journal_));
        return;
    }

    if (!getApp().getInboundLedgers ().gotLedgerData (
        hash, shared_from_this(), m))
    {
        p_journal_.trace  << "Got data for unwanted ledger";
        charge (Resource::feeUnwantedData);
    }
}

void
PeerImp::onMessage (std::shared_ptr <protocol::TMProposeSet> const& m)
{
    protocol::TMProposeSet& set = *m;

    // VFALCO Magic numbers are bad
    if ((set.closetime() + 180) < getApp().getOPs().getCloseTimeNC())
        return;

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
        p_journal_.warning << "Proposal: malformed";
        charge (Resource::feeInvalidSignature);
        return;
    }

    if (set.has_previousledger () && (set.previousledger ().size () != 32))
    {
        p_journal_.warning << "Proposal: malformed";
        charge (Resource::feeInvalidRequest);
        return;
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
        suppression, id_))
    {
        p_journal_.trace << "Proposal: duplicate";
        return;
    }

    RippleAddress signerPublic = RippleAddress::createNodePublic (
        strCopy (set.nodepubkey ()));

    if (signerPublic == getConfig ().VALIDATION_PUB)
    {
        p_journal_.trace << "Proposal: self";
        return;
    }

    bool isTrusted = getApp().getUNL ().nodeInUNL (signerPublic);
    if (!isTrusted && getApp().getFeeTrack ().isLoadedLocal ())
    {
        p_journal_.debug << "Proposal: Dropping UNTRUSTED (load)";
        return;
    }

    p_journal_.trace <<
        "Proposal: " << (isTrusted ? "trusted" : "UNTRUSTED");

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
        "recvPropose->checkPropose", std::bind(beast::weak_fn(
            &PeerImp::checkPropose, shared_from_this()), std::placeholders::_1,
            m, proposal, consensusLCL));
}

void
PeerImp::onMessage (std::shared_ptr <protocol::TMStatusChange> const& m)
{
    p_journal_.trace << "Status: Change";

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
            p_journal_.trace << "Status: Out of sync";
            closedLedgerHash_.zero ();
        }

        previousLedgerHash_.zero ();
        return;
    }

    if (m->has_ledgerhash () && (m->ledgerhash ().size () == (256 / 8)))
    {
        // a peer has changed ledgers
        memcpy (closedLedgerHash_.begin (), m->ledgerhash ().data (), 256 / 8);
        addLedger (closedLedgerHash_);
        p_journal_.trace << "LCL is " << closedLedgerHash_;
    }
    else
    {
        p_journal_.trace << "Status: No ledger";
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

        // VFALCO Is this workaround still needed?
        // Work around some servers that report sequences incorrectly
        if (minLedger_ == 0)
            maxLedger_ = 0;
        if (maxLedger_ == 0)
            minLedger_ = 0;
    }
}

void
PeerImp::onMessage (std::shared_ptr <protocol::TMHaveTransactionSet> const& m)
{
    uint256 hashes;

    if (m->hash ().size () != (256 / 8))
    {
        charge (Resource::feeInvalidRequest);
        return;
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
}

void
PeerImp::onMessage (std::shared_ptr <protocol::TMValidation> const& m)
{
    error_code ec;
    std::uint32_t closeTime = getApp().getOPs().getCloseTimeNC();

    if (m->validation ().size () < 50)
    {
        p_journal_.warning << "Validation: Too small";
        charge (Resource::feeInvalidRequest);
        return;
    }

    try
    {
        Serializer s (m->validation ());
        SerializerIterator sit (s);
        STValidation::pointer val = std::make_shared <
            STValidation> (std::ref (sit), false);

        if (closeTime > (120 + val->getFieldU32(sfSigningTime)))
        {
            p_journal_.trace << "Validation: Too old";
            charge (Resource::feeUnwantedData);
            return;
        }

        if (! getApp().getHashRouter ().addSuppressionPeer (
            s.getSHA512Half(), id_))
        {
            p_journal_.trace << "Validation: duplicate";
            return;
        }

        bool isTrusted = getApp().getUNL ().nodeInUNL (val->getSignerPublic ());
        if (isTrusted || !getApp().getFeeTrack ().isLoadedLocal ())
        {
            getApp().getJobQueue ().addJob (isTrusted ?
                jtVALIDATION_t : jtVALIDATION_ut, "recvValidation->checkValidation",
                    std::bind(beast::weak_fn(&PeerImp::checkValidation,
                        shared_from_this()), std::placeholders::_1, val,
                            isTrusted, m));
        }
        else
        {
            p_journal_.debug <<
                "Validation: Dropping UNTRUSTED (load)";
        }
    }
    catch (std::exception const& e)
    {
        p_journal_.warning <<
            "Validation: Exception, " << e.what();
        charge (Resource::feeInvalidRequest);
    }
    catch (...)
    {
        p_journal_.warning <<
            "Validation: Unknown exception";
        charge (Resource::feeInvalidRequest);
    }
}

void
PeerImp::onMessage (std::shared_ptr <protocol::TMGetObjectByHash> const& m)
{
    protocol::TMGetObjectByHash& packet = *m;

    if (packet.query ())
    {
        // this is a query
        if (packet.type () == protocol::TMGetObjectByHash::otFETCH_PACK)
        {
            doFetchPack (m);
            return;
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

        p_journal_.trace <<
            "GetObj: " << reply.objects_size () <<
                " of " << packet.objects_size ();
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
                            p_journal_.active(beast::Journal::Severity::kDebug))
                            p_journal_.debug <<
                                "GetObj: Full fetch pack for " << pLSeq;

                        pLSeq = obj.ledgerseq ();
                        pLDo = !getApp().getOPs ().haveLedger (pLSeq);

                        if (!pLDo)
                                p_journal_.debug <<
                                    "GetObj: Late fetch pack for " << pLSeq;
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
               p_journal_.active(beast::Journal::Severity::kDebug))
            p_journal_.debug << "GetObj: Partial fetch pack for " << pLSeq;

        if (packet.type () == protocol::TMGetObjectByHash::otFETCH_PACK)
            getApp().getOPs ().gotFetchPack (progress, pLSeq);
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

bool
PeerImp::sendHello()
{
    bool success;
    std::tie(sharedValue_, success) = makeSharedValue(
        stream_.native_handle(), journal_);
    if (! success)
        return false;

    auto const hello = buildHello (sharedValue_, getApp());
    auto const m = std::make_shared<Message> (
        std::move(hello), protocol::mtHELLO);
    send (m);
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
        p_journal_.info << "Too busy to make fetch pack";
        return;
    }

    if (packet->ledgerhash ().size () != 32)
    {
        p_journal_.warning << "FetchPack hash size malformed";
        charge (Resource::feeInvalidRequest);
        return;
    }

    uint256 hash;
    memcpy (hash.begin (), packet->ledgerhash ().data (), 32);

    getApp().getJobQueue ().addJob (jtPACK, "MakeFetchPack",
        std::bind (&NetworkOPs::makeFetchPack, &getApp().getOPs (),
            std::placeholders::_1, std::weak_ptr<PeerImp> (shared_from_this ()),
                packet, hash, UptimeTimer::getInstance ().getElapsedSeconds ()));
}

void
PeerImp::doProofOfWork (Job&, std::weak_ptr <PeerImp> peer,
    ProofOfWork::pointer pow)
{
    if (peer.expired ())
        return;

    uint256 solution = pow->solve ();

    if (solution.isZero ())
    {
        p_journal_.warning << "Failed to solve proof of work";
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
    STTx::pointer stx)
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
            return charge (Resource::feeUnwantedData);
        }

        auto validate = (flags & SF_SIGGOOD) ? Validate::NO : Validate::YES;
        auto tx = std::make_shared<Transaction> (stx, validate);

        if (tx->getStatus () == INVALID)
        {
            getApp().getHashRouter ().setFlag (stx->getTransactionID (), SF_BAD);
            return charge (Resource::feeInvalidSignature);
        }
        else
        {
            getApp().getHashRouter ().setFlag (stx->getTransactionID (), SF_SIGGOOD);
        }

        bool const trusted (flags & SF_TRUSTED);
        getApp().getOPs ().processTransaction (tx, trusted, false, false);
    }
    catch (...)
    {
        getApp().getHashRouter ().setFlag (stx->getTransactionID (), SF_BAD);
        charge (Resource::feeInvalidRequest);
    }
}

// Called from our JobQueue
void
PeerImp::checkPropose (Job& job,
    std::shared_ptr <protocol::TMProposeSet> const& packet,
        LedgerProposal::pointer proposal, uint256 consensusLCL)
{
    bool sigGood = false;
    bool isTrusted = (job.getType () == jtPROPOSAL_t);

    p_journal_.trace <<
        "Checking " << (isTrusted ? "trusted" : "UNTRUSTED") << " proposal";

    assert (packet);
    protocol::TMProposeSet& set = *packet;

    uint256 prevLedger;

    if (set.has_previousledger ())
    {
        // proposal includes a previous ledger
        p_journal_.trace <<
            "proposal with previous ledger";
        memcpy (prevLedger.begin (), set.previousledger ().data (), 256 / 8);

        if (! cluster() && !proposal->checkSign (set.signature ()))
        {
            p_journal_.warning <<
                "Proposal with previous ledger fails sig check";
            charge (Resource::feeInvalidSignature);
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
            p_journal_.warning <<
                "Ledger proposal fails signature check";
            proposal->setSignature (set.signature ());
        }
    }

    if (isTrusted)
    {
        getApp().getOPs ().processTrustedProposal (
            proposal, packet, publicKey_, prevLedger, sigGood);
    }
    else if (sigGood && (prevLedger == consensusLCL))
    {
        // relay untrusted proposal
        p_journal_.trace <<
            "relaying UNTRUSTED proposal";
        std::set<Peer::id_t> peers;

        if (getApp().getHashRouter ().swapSet (
            proposal->getSuppressionID (), peers, SF_RELAYED))
        {
            overlay_.foreach (send_if_not (
                std::make_shared<Message> (set, protocol::mtPROPOSE_LEDGER),
                peer_in_set(peers)));
        }
    }
    else
    {
        p_journal_.debug <<
            "Not relaying UNTRUSTED proposal";
    }
}

void
PeerImp::checkValidation (Job&, STValidation::pointer val,
    bool isTrusted, std::shared_ptr<protocol::TMValidation> const& packet)
{
    try
    {
        // VFALCO Which functions throw?
        uint256 signingHash = val->getSigningHash();
        if (! cluster() && !val->isValid (signingHash))
        {
            p_journal_.warning <<
                "Validation is invalid";
            charge (Resource::feeInvalidRequest);
            return;
        }

        //----------------------------------------------------------------------
        {
            STValidation const& sv (*val);
            Validators::ReceivedValidation rv;
            rv.ledgerHash = sv.getLedgerHash ();
            rv.publicKey = sv.getSignerPublic();
            getApp ().getValidators ().on_receive_validation (rv);
        }
        //----------------------------------------------------------------------

        std::set<Peer::id_t> peers;
        if (getApp().getOPs ().recvValidation (val, std::to_string(id())) &&
                getApp().getHashRouter ().swapSet (
                    signingHash, peers, SF_RELAYED))
        {
            overlay_.foreach (send_if_not (
                std::make_shared<Message> (*packet, protocol::mtVALIDATION),
                peer_in_set(peers)));
        }
    }
    catch (...)
    {
        p_journal_.trace <<
            "Exception processing validation";
        charge (Resource::feeInvalidRequest);
    }
}

// VFALCO NOTE This function is way too big and cumbersome.
void
PeerImp::getLedger (std::shared_ptr<protocol::TMGetLedger> const& m)
{
    protocol::TMGetLedger& packet = *m;
    SHAMap::pointer map;
    protocol::TMLedgerData reply;
    bool fatLeaves = true, fatRoot = false;

    if (packet.has_requestcookie ())
        reply.set_requestcookie (packet.requestcookie ());

    std::string logMe;

    if (packet.itype () == protocol::liTS_CANDIDATE)
    {
        // Request is for a transaction candidate set
        p_journal_.trace <<
            "GetLedger: Tx candidate set";

        if ((!packet.has_ledgerhash () || packet.ledgerhash ().size () != 32))
        {
            charge (Resource::feeInvalidRequest);
            p_journal_.warning << "GetLedger: Tx candidate set invalid";
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
                p_journal_.debug <<
                    "GetLedger: Routing Tx set request";

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
                    p_journal_.info <<
                        "GetLedger: Route TX set failed";
                    return;
                }

                Peer::ptr const& selectedPeer = usablePeers [
                    rand () % usablePeers.size ()];
                packet.set_requestcookie (id ());
                selectedPeer->send (std::make_shared<Message> (
                    packet, protocol::mtGET_LEDGER));
                return;
            }

            p_journal_.error <<
                "GetLedger: Can't provide map ";
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
        if (getApp().getFeeTrack().isLoadedLocal() && ! cluster())
        {
            p_journal_.debug <<
                "GetLedger: Too busy";
            return;
        }

        // Figure out what ledger they want
        p_journal_.trace <<
            "GetLedger: Received";
        Ledger::pointer ledger;

        if (packet.has_ledgerhash ())
        {
            uint256 ledgerhash;

            if (packet.ledgerhash ().size () != 32)
            {
                charge (Resource::feeInvalidRequest);
                p_journal_.warning <<
                    "GetLedger: Invalid request";
                return;
            }

            memcpy (ledgerhash.begin (), packet.ledgerhash ().data (), 32);
            logMe += "LedgerHash:";
            logMe += to_string (ledgerhash);
            ledger = getApp().getLedgerMaster ().getLedgerByHash (ledgerhash);

            if (!ledger && p_journal_.trace)
                p_journal_.trace <<
                    "GetLedger: Don't have " << ledgerhash;

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
                    p_journal_.trace <<
                        "GetLedger: Cannot route";
                    return;
                }

                Peer::ptr const& selectedPeer = usablePeers [
                    rand () % usablePeers.size ()];
                packet.set_requestcookie (id ());
                selectedPeer->send (
                    std::make_shared<Message> (packet, protocol::mtGET_LEDGER));
                p_journal_.debug <<
                    "GetLedger: Request routed";
                return;
            }
        }
        else if (packet.has_ledgerseq ())
        {
            if (packet.ledgerseq() <
                    getApp().getLedgerMaster().getEarliestFetch())
            {
                p_journal_.debug <<
                    "GetLedger: Early ledger request";
                return;
            }
            ledger = getApp().getLedgerMaster ().getLedgerBySeq (
                packet.ledgerseq ());
            if (!ledger && p_journal_.debug)
                p_journal_.debug <<
                    "GetLedger: Don't have " << packet.ledgerseq ();
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
            p_journal_.warning <<
                "GetLedger: Unknown request";
            return;
        }

        if ((!ledger) || (packet.has_ledgerseq () && (
            packet.ledgerseq () != ledger->getLedgerSeq ())))
        {
            charge (Resource::feeInvalidRequest);

            if (p_journal_.warning && ledger)
                p_journal_.warning <<
                    "GetLedger: Invalid sequence";

            return;
        }

            if (!packet.has_ledgerseq() && (ledger->getLedgerSeq() <
                getApp().getLedgerMaster().getEarliestFetch()))
            {
                p_journal_.debug <<
                    "GetLedger: Early ledger request";
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
            p_journal_.trace <<
                "GetLedger: Base data";
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
        p_journal_.warning <<
            "GetLedger: Can't find map or empty request";
        charge (Resource::feeInvalidRequest);
        return;
    }

    p_journal_.trace <<
        "GetLeder: " << logMe;

    for (int i = 0; i < packet.nodeids ().size (); ++i)
    {
        SHAMapNodeID mn (packet.nodeids (i).data (), packet.nodeids (i).size ());

        if (!mn.isValid ())
        {
            p_journal_.warning <<
                "GetLedger: Invalid node " << logMe;
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
                p_journal_.trace <<
                    "GetLedger: getNodeFat got " << rawNodes.size () << " nodes";
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
                p_journal_.warning <<
                    "GetLedger: getNodeFat returns false";
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

            p_journal_.warning <<
                "getNodeFat( " << mn << ") throws exception: " << info;
        }
    }

    Message::pointer oPacket = std::make_shared<Message> (
        reply, protocol::mtLEDGER_DATA);
    send (oPacket);
}

// VFALCO TODO Make this non-static
void
PeerImp::peerTXData (Job&, uint256 const& hash,
    std::shared_ptr <protocol::TMLedgerData> const& pPacket,
        beast::Journal journal)
{
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
            charge (Resource::feeInvalidRequest);
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

        san =  getApp().getOPs().gotTXData (shared_from_this(),
            hash, nodeIDs, nodeData);
    }

    if (san.isInvalid ())
        charge (Resource::feeUnwantedData);
}

} // ripple
