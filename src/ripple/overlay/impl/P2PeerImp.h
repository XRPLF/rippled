//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2021 Ripple Labs Inc.

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

#ifndef RIPPLE_OVERLAY_P2PEERIMP_H_INCLUDED
#define RIPPLE_OVERLAY_P2PEERIMP_H_INCLUDED

#include <ripple/basics/Log.h>
#include <ripple/beast/utility/WrappedSink.h>
#include <ripple/overlay/impl/P2POverlayImpl.h>
#include <ripple/overlay/impl/ProtocolMessage.h>
#include <ripple/overlay/impl/ProtocolVersion.h>
#include <ripple/overlay/impl/Tuning.h>
#include <ripple/peerfinder/PeerfinderManager.h>
#include <ripple/protocol/Protocol.h>

#include <boost/circular_buffer.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/thread/shared_mutex.hpp>

#include <cstdint>
#include <optional>
#include <queue>
#include <shared_mutex>

namespace ripple {

/** Represents connected remote p2p layer peer. Implements p2p methods
 * declared in Peer and other p2p methods required for the overlay support
 * such as protocol message send/receive and starting the protocol loop.
 */
template <typename PeerImp_t>
class P2PeerImp : public Peer,
                  public std::enable_shared_from_this<P2PeerImp<PeerImp_t>>,
                  public P2POverlayImpl<PeerImp_t>::Child
{
protected:
    using clock_type = std::chrono::steady_clock;
    using error_code = boost::system::error_code;
    using socket_type = boost::asio::ip::tcp::socket;
    using middle_type = boost::beast::tcp_stream;
    using stream_type = boost::beast::ssl_stream<middle_type>;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using Compressed = compression::Compressed;
    using mutable_buffers_type =
        boost::beast::multi_buffer::mutable_buffers_type;
    using const_buffers_type = boost::beast::multi_buffer::const_buffers_type;

private:
    P2PConfig const& p2pConfig_;
    beast::WrappedSink sink_;
    std::unique_ptr<stream_type> stream_ptr_;
    socket_type& socket_;
    stream_type& stream_;
    bool detaching_ = false;
    std::string name_;
    boost::shared_mutex mutable nameMutex_;
    boost::beast::multi_buffer read_buffer_;
    http_request_type request_;
    http_response_type response_;
    std::queue<std::shared_ptr<Message>> send_queue_;
    bool gracefulClose_ = false;
    std::uint32_t large_sendq_ = 0;

protected:
    id_t const id_;
    beast::Journal const journal_;
    boost::asio::strand<boost::asio::executor> const strand_;

    // Updated at each stage of the connection process to reflect
    // the current conditions as closely as possible.
    beast::IP::Endpoint const remote_address_;

    bool const inbound_;

    // Protocol version to use for this link
    ProtocolVersion const protocol_;

    // Node public key of peer.
    PublicKey const publicKey_;

    std::shared_ptr<PeerFinder::Slot> const slot_;
    boost::beast::http::fields const& headers_;

    Compressed const compressionEnabled_ = Compressed::Off;

    class Metrics
    {
    public:
        Metrics() = default;
        Metrics(Metrics const&) = delete;
        Metrics&
        operator=(Metrics const&) = delete;
        Metrics(Metrics&&) = delete;
        Metrics&
        operator=(Metrics&&) = delete;

        void
        add_message(std::uint64_t bytes);
        std::uint64_t
        average_bytes() const;
        std::uint64_t
        total_bytes() const;

    private:
        boost::shared_mutex mutable mutex_;
        boost::circular_buffer<std::uint64_t> rollingAvg_{30, 0ull};
        clock_type::time_point intervalStart_{clock_type::now()};
        std::uint64_t totalBytes_{0};
        std::uint64_t accumBytes_{0};
        std::uint64_t rollingAvgBytes_{0};
    };

    struct
    {
        Metrics sent;
        Metrics recv;
    } metrics_;

public:
    P2PeerImp(P2PeerImp const&) = delete;
    P2PeerImp&
    operator=(P2PeerImp const&) = delete;

    /** Create an active incoming peer from an established ssl connection. */
    P2PeerImp(
        P2PConfig const& p2pConfig,
        id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        std::unique_ptr<stream_type>&& stream_ptr,
        P2POverlayImpl<PeerImp_t>& overlay);

    /** Create outgoing, handshaked peer. */
    // VFALCO legacyPublicKey should be implied by the Slot
    P2PeerImp(
        P2PConfig const& p2pConfig,
        std::unique_ptr<stream_type>&& stream_ptr,
        const_buffers_type const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        id_t id,
        P2POverlayImpl<PeerImp_t>& overlay);

    virtual ~P2PeerImp();

    std::shared_ptr<PeerFinder::Slot> const&
    slot()
    {
        return slot_;
    }

    /** Work-around for calling shared_from_this in constructors */
    void
    run();

    /** Called when Overlay gets a stop request. */
    void
    stop() override;

    //
    // Network
    //

    void
    send(std::shared_ptr<Message> const& m) override;

    /** Send a set of PeerFinder endpoints as a protocol message. */
    template <
        class FwdIt,
        class = typename std::enable_if_t<std::is_same<
            typename std::iterator_traits<FwdIt>::value_type,
            PeerFinder::Endpoint>::value>>
    void
    sendEndpoints(FwdIt first, FwdIt last);

    beast::IP::Endpoint
    getRemoteAddress() const override
    {
        return remote_address_;
    }

    bool
    isSocketOpen() const
    {
        return socket_.is_open();
    }

    socket_type::executor_type
    getSocketExecutor() const
    {
        return socket_.get_executor();
    }

    std::size_t
    getSendQueueSize() const
    {
        return send_queue_.size();
    }

    std::uint32_t
    incLargeSendQueue()
    {
        auto q = large_sendq_;
        large_sendq_++;
        return q;
    }

    //
    // Identity
    //

    Peer::id_t
    id() const override
    {
        return id_;
    }

    PublicKey const&
    getNodePublic() const override
    {
        return publicKey_;
    }

    /** Return the version of rippled that the peer is running, if reported. */
    std::string
    getVersion() const;

    bool
    compressionEnabled() const override
    {
        return compressionEnabled_ == Compressed::On;
    }

protected:
    void
    fail(std::string const& reason);

    void
    close();

    static std::string
    makePrefix(id_t id);

    std::string
    name() const;

    std::string
    domain() const;

    /** Messages handled in the p2p layer. Currently it is
     * TMEndpoints only.
     */
    virtual void
    onMessage(std::shared_ptr<protocol::TMEndpoints> const& m);

private:
    void
    fail(std::string const& name, error_code ec);

    void
    gracefulClose();

    /** Called when SSL shutdown completes */
    void
    onShutdown(error_code ec);

    void
    doAccept();

    //
    // protocol message loop
    //

    /** Starts the protocol message loop */
    void
    doProtocolStart();

    /** Called when protocol message bytes are received */
    void
    onReadMessage(error_code ec, std::size_t bytes_transferred);

    /** Called when protocol messages bytes are sent */
    void
    onWriteMessage(error_code ec, std::size_t bytes_transferred);

    /** Called right before the onMessage() message handler */
    virtual void
    onMessageBegin(
        std::uint16_t type,
        std::shared_ptr<::google::protobuf::Message> const& m,
        std::size_t size,
        std::size_t uncompressed_size,
        bool isCompressed) = 0;

    /** Called after the onMessage() message handler */
    virtual void
    onMessageEnd(
        std::uint16_t type,
        std::shared_ptr<::google::protobuf::Message> const& m) = 0;

    //--------------------------------------------------------------------------
    // Delegate custom handling of events to the application layer.
    //--------------------------------------------------------------------------
    /** Called in run() */
    virtual void
    onEvtRun() = 0;

    /** Called in send()
     * @param m protocol message to send
     * @return true if the message is filtered, false otherwise.
     */
    virtual bool
    onEvtSendFilter(std::shared_ptr<Message> const& m) = 0;

    /** Called in close() if the socket is open */
    virtual void
    onEvtClose() = 0;

    /** Called in gracefulClose() */
    virtual void
    onEvtGracefulClose() = 0;

    /** Called in shutdown() */
    virtual void
    onEvtShutdown() = 0;

    /** Called in doProtocolStart() */
    virtual void
    onEvtDoProtocolStart() = 0;

    /** Called in invokeProtocolMessage() in ProtocolMessage module
     * @param header protocol message header
     * @param buffers serialized message including the header
     * @return true if the message is handled, false on error
     */
    virtual bool
    onEvtProtocolMessage(
        detail::MessageHeader const& header,
        const_buffers_type const& buffers) = 0;

    /** Protocol message parsing and message handling invocation is
     * defined in ProtocolMessage module. It requires access to
     * onMessageBegin/End(), onMessage(), and onEvtProtocolMessage().
     */
    friend struct detail::PM;
};

template <typename PeerImp_t>
P2PeerImp<PeerImp_t>::P2PeerImp(
    P2PConfig const& p2pConfig,
    id_t id,
    std::shared_ptr<PeerFinder::Slot> const& slot,
    http_request_type&& request,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    std::unique_ptr<stream_type>&& stream_ptr,
    P2POverlayImpl<PeerImp_t>& overlay)
    : P2POverlayImpl<PeerImp_t>::Child(overlay)
    , p2pConfig_(p2pConfig)
    , sink_(p2pConfig.logs().journal("Peer"), makePrefix(id))
    , stream_ptr_(std::move(stream_ptr))
    , socket_(stream_ptr_->next_layer().socket())
    , stream_(*stream_ptr_)
    , request_(std::move(request))
    , id_(id)
    , journal_(sink_)
    , strand_(socket_.get_executor())
    , remote_address_(slot->remote_endpoint())
    , inbound_(true)
    , protocol_(protocol)
    , publicKey_(publicKey)
    , slot_(slot)
    , headers_(request_)
    , compressionEnabled_(
          peerFeatureEnabled(
              headers_,
              FEATURE_COMPR,
              "lz4",
              p2pConfig_.config().COMPRESSION)
              ? Compressed::On
              : Compressed::Off)
{
}

template <typename PeerImp_t>
P2PeerImp<PeerImp_t>::P2PeerImp(
    P2PConfig const& p2pConfig,
    std::unique_ptr<stream_type>&& stream_ptr,
    const_buffers_type const& buffers,
    std::shared_ptr<PeerFinder::Slot>&& slot,
    http_response_type&& response,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    id_t id,
    P2POverlayImpl<PeerImp_t>& overlay)
    : P2POverlayImpl<PeerImp_t>::Child(overlay)
    , p2pConfig_(p2pConfig)
    , sink_(p2pConfig_.logs().journal("Peer"), makePrefix(id))
    , stream_ptr_(std::move(stream_ptr))
    , socket_(stream_ptr_->next_layer().socket())
    , stream_(*stream_ptr_)
    , response_(std::move(response))
    , id_(id)
    , journal_(sink_)
    , strand_(socket_.get_executor())
    , remote_address_(slot->remote_endpoint())
    , inbound_(false)
    , protocol_(protocol)
    , publicKey_(publicKey)
    , slot_(std::move(slot))
    , headers_(response_)
    , compressionEnabled_(
          peerFeatureEnabled(
              headers_,
              FEATURE_COMPR,
              "lz4",
              p2pConfig_.config().COMPRESSION)
              ? Compressed::On
              : Compressed::Off)
{
    read_buffer_.commit(boost::asio::buffer_copy(
        read_buffer_.prepare(boost::asio::buffer_size(buffers)), buffers));
}

template <typename PeerImp_t>
P2PeerImp<PeerImp_t>::~P2PeerImp()
{
    this->overlay_.peerFinder().on_closed(slot_);
    this->overlay_.onPeerDeactivate(id_);
}

template <typename PeerImp_t>
void
P2PeerImp<PeerImp_t>::run()
{
    if (!strand_.running_in_this_thread())
        return post(
            strand_, std::bind(&P2PeerImp::run, this->shared_from_this()));

    onEvtRun();

    if (inbound_)
        doAccept();
    else
        doProtocolStart();

    // Anything else that needs to be done with the connection should be
    // done in doProtocolStart
}

template <typename PeerImp_t>
void
P2PeerImp<PeerImp_t>::stop()
{
    if (!strand_.running_in_this_thread())
        return post(
            strand_, std::bind(&P2PeerImp::stop, this->shared_from_this()));
    if (socket_.is_open())
    {
        // The rationale for using different severity levels is that
        // outbound connections are under our control and may be logged
        // at a higher level, but inbound connections are more numerous and
        // uncontrolled so to prevent log flooding the severity is reduced.
        //
        if (inbound_)
        {
            JLOG(journal_.debug()) << "Stop";
        }
        else
        {
            JLOG(journal_.info()) << "Stop";
        }
    }
    close();
}

//------------------------------------------------------------------------------

template <typename PeerImp_t>
void
P2PeerImp<PeerImp_t>::send(std::shared_ptr<Message> const& m)
{
    if (!strand_.running_in_this_thread())
        return post(
            strand_, std::bind(&P2PeerImp::send, this->shared_from_this(), m));
    if (gracefulClose_)
        return;
    if (detaching_)
        return;

    if (onEvtSendFilter(m))
        return;

    auto sendq_size = send_queue_.size();

    if (sendq_size < Tuning::targetSendQueue)
    {
        // To detect a peer that does not read from their
        // side of the connection, we expect a peer to have
        // a small senq periodically
        large_sendq_ = 0;
    }
    else if (auto sink = journal_.debug();
             sink && (sendq_size % Tuning::sendQueueLogFreq) == 0)
    {
        std::string const n = name();
        sink << (n.empty() ? remote_address_.to_string() : n)
             << " sendq: " << sendq_size;
    }

    send_queue_.push(m);

    if (sendq_size != 0)
        return;

    boost::asio::async_write(
        stream_,
        boost::asio::buffer(
            send_queue_.front()->getBuffer(compressionEnabled_)),
        bind_executor(
            strand_,
            std::bind(
                &P2PeerImp::onWriteMessage,
                this->shared_from_this(),
                std::placeholders::_1,
                std::placeholders::_2)));
}

template <typename PeerImp_t>
std::string
P2PeerImp<PeerImp_t>::getVersion() const
{
    if (inbound_)
        return headers_["User-Agent"].to_string();
    return headers_["Server"].to_string();
}

template <typename PeerImp_t>
void
P2PeerImp<PeerImp_t>::close()
{
    assert(strand_.running_in_this_thread());
    if (socket_.is_open())
    {
        detaching_ = true;  // DEPRECATED
        error_code ec;
        // timer_.cancel(ec);
        onEvtClose();
        socket_.close(ec);
        if (inbound_)
        {
            JLOG(journal_.debug()) << "Closed";
        }
        else
        {
            JLOG(journal_.info()) << "Closed";
        }
    }
}

template <typename PeerImp_t>
void
P2PeerImp<PeerImp_t>::fail(std::string const& reason)
{
    if (!strand_.running_in_this_thread())
        return post(
            strand_,
            std::bind(
                (void (Peer::*)(std::string const&)) & P2PeerImp::fail,
                this->shared_from_this(),
                reason));
    if (journal_.active(beast::severities::kWarning) && socket_.is_open())
    {
        std::string const n = name();
        JLOG(journal_.warn()) << (n.empty() ? remote_address_.to_string() : n)
                              << " failed: " << reason;
    }
    close();
}

template <typename PeerImp_t>
void
P2PeerImp<PeerImp_t>::fail(std::string const& name, error_code ec)
{
    assert(strand_.running_in_this_thread());
    if (socket_.is_open())
    {
        JLOG(journal_.warn())
            << name << " from " << toBase58(TokenType::NodePublic, publicKey_)
            << " at " << remote_address_.to_string() << ": " << ec.message();
    }
    close();
}

template <typename PeerImp_t>
void
P2PeerImp<PeerImp_t>::gracefulClose()
{
    assert(strand_.running_in_this_thread());
    assert(socket_.is_open());
    assert(!gracefulClose_);
    gracefulClose_ = true;
#if 0
    // Flush messages
    while(send_queue_.size() > 1)
        send_queue_.pop_back();
#endif
    if (send_queue_.size() > 0)
        return;
    // setTimer();
    onEvtGracefulClose();
    stream_.async_shutdown(bind_executor(
        strand_,
        std::bind(
            &P2PeerImp::onShutdown,
            this->shared_from_this(),
            std::placeholders::_1)));
}

template <typename PeerImp_t>
std::string
P2PeerImp<PeerImp_t>::makePrefix(id_t id)
{
    std::stringstream ss;
    ss << "[" << std::setfill('0') << std::setw(3) << id << "] ";
    return ss.str();
}

template <typename PeerImp_t>
void
P2PeerImp<PeerImp_t>::onShutdown(error_code ec)
{
    onEvtShutdown();
    // If we don't get eof then something went wrong
    if (!ec)
    {
        JLOG(journal_.error()) << "onShutdown: expected error condition";
        return close();
    }
    if (ec != boost::asio::error::eof)
        return fail("onShutdown", ec);
    close();
}

//------------------------------------------------------------------------------
template <typename PeerImp_t>
void
P2PeerImp<PeerImp_t>::doAccept()
{
    assert(read_buffer_.size() == 0);

    JLOG(journal_.debug()) << "doAccept: " << remote_address_;

    auto const sharedValue = makeSharedValue(*stream_ptr_, journal_);

    // This shouldn't fail since we already computed
    // the shared value successfully in OverlayImpl
    if (!sharedValue)
        return fail("makeSharedValue: Unexpected failure");

    JLOG(journal_.info()) << "Protocol: " << to_string(protocol_);
    JLOG(journal_.info()) << "Public Key: "
                          << toBase58(TokenType::NodePublic, publicKey_);

    if (auto member = p2pConfig_.clusterMember(publicKey_))
    {
        {
            std::unique_lock lock{nameMutex_};
            name_ = *member;
        }
        JLOG(journal_.info()) << "Cluster name: " << *member;
    }

    // XXX Set timer: connection is in grace period to be useful.
    // XXX Set timer: connection idle (idle may vary depending on connection
    // type.)

    auto write_buffer = std::make_shared<boost::beast::multi_buffer>();

    boost::beast::ostream(*write_buffer) << makeResponse(
        !this->overlay_.peerFinder().config().peerPrivate,
        request_,
        this->overlay_.setup().public_ip,
        remote_address_.address(),
        *sharedValue,
        this->overlay_.setup().networkID,
        protocol_,
        p2pConfig_);

    // Write the whole buffer and only start protocol when that's done.
    boost::asio::async_write(
        stream_,
        write_buffer->data(),
        boost::asio::transfer_all(),
        bind_executor(
            strand_,
            [this, write_buffer, self = this->shared_from_this()](
                error_code ec, std::size_t bytes_transferred) {
                if (!socket_.is_open())
                    return;
                if (ec == boost::asio::error::operation_aborted)
                    return;
                if (ec)
                    return fail("onWriteResponse", ec);
                if (write_buffer->size() == bytes_transferred)
                    return doProtocolStart();
                return fail("Failed to write header");
            }));
}

template <typename PeerImp_t>
std::string
P2PeerImp<PeerImp_t>::name() const
{
    std::shared_lock read_lock{nameMutex_};
    return name_;
}

template <typename PeerImp_t>
std::string
P2PeerImp<PeerImp_t>::domain() const
{
    return headers_["Server-Domain"].to_string();
}

//------------------------------------------------------------------------------

// Protocol logic

template <typename PeerImp_t>
void
P2PeerImp<PeerImp_t>::doProtocolStart()
{
    onReadMessage(error_code(), 0);

    onEvtDoProtocolStart();
}

// Called repeatedly with protocol message data
template <typename PeerImp_t>
void
P2PeerImp<PeerImp_t>::onReadMessage(
    error_code ec,
    std::size_t bytes_transferred)
{
    if (!socket_.is_open())
        return;
    if (ec == boost::asio::error::operation_aborted)
        return;
    if (ec == boost::asio::error::eof)
    {
        JLOG(journal_.info()) << "EOF";
        return gracefulClose();
    }
    if (ec)
        return fail("onReadMessage", ec);
    if (auto stream = journal_.trace())
    {
        if (bytes_transferred > 0)
            stream << "onReadMessage: " << bytes_transferred << " bytes";
        else
            stream << "onReadMessage";
    }

    metrics_.recv.add_message(bytes_transferred);

    read_buffer_.commit(bytes_transferred);

    auto hint = Tuning::readBufferBytes;

    while (read_buffer_.size() > 0)
    {
        std::size_t bytes_consumed;
        std::tie(bytes_consumed, ec) =
            detail::PM::invokeProtocolMessage(read_buffer_.data(), *this, hint);
        if (ec)
            return fail("onReadMessage", ec);
        if (!socket_.is_open())
            return;
        if (gracefulClose_)
            return;
        if (bytes_consumed == 0)
            break;
        read_buffer_.consume(bytes_consumed);
    }

    // Timeout on writes only
    stream_.async_read_some(
        read_buffer_.prepare(std::max(Tuning::readBufferBytes, hint)),
        bind_executor(
            strand_,
            std::bind(
                &P2PeerImp::onReadMessage,
                this->shared_from_this(),
                std::placeholders::_1,
                std::placeholders::_2)));
}

template <typename PeerImp_t>
void
P2PeerImp<PeerImp_t>::onWriteMessage(
    error_code ec,
    std::size_t bytes_transferred)
{
    if (!socket_.is_open())
        return;
    if (ec == boost::asio::error::operation_aborted)
        return;
    if (ec)
        return fail("onWriteMessage", ec);
    if (auto stream = journal_.trace())
    {
        if (bytes_transferred > 0)
            stream << "onWriteMessage: " << bytes_transferred << " bytes";
        else
            stream << "onWriteMessage";
    }

    metrics_.sent.add_message(bytes_transferred);

    assert(!send_queue_.empty());
    send_queue_.pop();
    if (!send_queue_.empty())
    {
        // Timeout on writes only
        return boost::asio::async_write(
            stream_,
            boost::asio::buffer(
                send_queue_.front()->getBuffer(compressionEnabled_)),
            bind_executor(
                strand_,
                std::bind(
                    &P2PeerImp::onWriteMessage,
                    this->shared_from_this(),
                    std::placeholders::_1,
                    std::placeholders::_2)));
    }

    if (gracefulClose_)
    {
        return stream_.async_shutdown(bind_executor(
            strand_,
            std::bind(
                &P2PeerImp::onShutdown,
                this->shared_from_this(),
                std::placeholders::_1)));
    }
}

template <typename PeerImp_t>
template <class FwdIt, class>
void
P2PeerImp<PeerImp_t>::sendEndpoints(FwdIt first, FwdIt last)
{
    protocol::TMEndpoints tm;

    while (first != last)
    {
        auto& tme2(*tm.add_endpoints_v2());
        tme2.set_endpoint(first->address.to_string());
        tme2.set_hops(first->hops);
        first++;
    }
    tm.set_version(2);

    send(std::make_shared<Message>(tm, protocol::mtENDPOINTS));
}

template <typename PeerImp_t>
void
P2PeerImp<PeerImp_t>::onMessage(std::shared_ptr<protocol::TMEndpoints> const& m)
{
    if (m->version() != 2)
        return;

    std::vector<PeerFinder::Endpoint> endpoints;
    endpoints.reserve(m->endpoints_v2().size());

    for (auto const& tm : m->endpoints_v2())
    {
        auto result = beast::IP::Endpoint::from_string_checked(tm.endpoint());
        if (!result)
        {
            JLOG(journal_.error()) << "failed to parse incoming endpoint: {"
                                   << tm.endpoint() << "}";
            continue;
        }

        // If hops == 0, this Endpoint describes the peer we are connected
        // to -- in that case, we take the remote address seen on the
        // socket and store that in the IP::Endpoint. If this is the first
        // time, then we'll verify that their listener can receive incoming
        // by performing a connectivity test.  if hops > 0, then we just
        // take the address/port we were given

        endpoints.emplace_back(
            tm.hops() > 0 ? *result : remote_address_.at_port(result->port()),
            tm.hops());
    }

    if (!endpoints.empty())
        this->overlay_.peerFinder().on_endpoints(slot_, endpoints);
}

template <typename PeerImp_t>
void
P2PeerImp<PeerImp_t>::Metrics::add_message(std::uint64_t bytes)
{
    using namespace std::chrono_literals;
    std::unique_lock lock{mutex_};

    totalBytes_ += bytes;
    accumBytes_ += bytes;
    auto const timeElapsed = clock_type::now() - intervalStart_;
    auto const timeElapsedInSecs =
        std::chrono::duration_cast<std::chrono::seconds>(timeElapsed);

    if (timeElapsedInSecs >= 1s)
    {
        auto const avgBytes = accumBytes_ / timeElapsedInSecs.count();
        rollingAvg_.push_back(avgBytes);

        auto const totalBytes =
            std::accumulate(rollingAvg_.begin(), rollingAvg_.end(), 0ull);
        rollingAvgBytes_ = totalBytes / rollingAvg_.size();

        intervalStart_ = clock_type::now();
        accumBytes_ = 0;
    }
}

template <typename PeerImp_t>
std::uint64_t
P2PeerImp<PeerImp_t>::Metrics::average_bytes() const
{
    std::shared_lock lock{mutex_};
    return rollingAvgBytes_;
}

template <typename PeerImp_t>
std::uint64_t
P2PeerImp<PeerImp_t>::Metrics::total_bytes() const
{
    std::shared_lock lock{mutex_};
    return totalBytes_;
}

}  // namespace ripple

#endif  // RIPPLE_OVERLAY_P2PEERIMP_H_INCLUDED
