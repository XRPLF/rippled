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

#include <ripple/app/main/CollectorManager.h>
#include <ripple/basics/ResolverAsio.h>
#include <ripple/basics/comparators.h>
#include <ripple/basics/make_SSLContext.h>
#include <ripple/basics/random.h>
#include <ripple/beast/unit_test.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/overlay/impl/P2POverlayImpl.h>
#include <ripple/overlay/make_Overlay.h>
#include <ripple/server/Server.h>
#include <ripple/server/Session.h>
#include <ripple.pb.h>
#include <test/jtx/Env.h>

#include <boost/asio.hpp>
#include <boost/bimap.hpp>
#include <boost/regex.hpp>
#include <boost/thread.hpp>
#include <boost/tokenizer.hpp>

#include <chrono>
#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>

namespace ripple {

namespace test {

using set_of = boost::bimaps::set_of<std::string, ripple::less<std::string>>;
using bimap = boost::bimap<set_of, set_of>;

/** Unit-tests to test Overlay (peer-2-peer only) network.
 * There are two tests: 1) overlay_net_test, which creates a small network of
 * five interconnected nodes; 2) overlay_xrpl_test, which attempts to
 * replicate complete XRPL network overlay as described by the adjacency
 * matrix. The matrix format is ip1,ip2,[in|out]. Where ip1 and ip2 are IP
 * addresses of two connected nodes and [in|out] describe whether ip2 is
 * an incoming or outgoing connection. The matrix adjacency-xrpl.txt is
 * included in the repo. The overlay simulation can be run as:
 * ./rippled --unittest overlay_xrpl --unittest-arg
 *     <path>/rippled/src/test/overlay/adjacency-xrpl.txt
 * where <path> is rippled folder location. At the end of the test
 * the adjacency matrix of created overlay is generated into network.out file.
 */

static std::string
mkName(std::string const& n, int i)
{
    return n + std::to_string(i);
}

/** Overlay total counts of endpoint messages, inbound/outbound peers,
 * and deactivated peers.
 */
struct Counts
{
    static inline std::atomic_uint64_t msgSendCnt = 0;
    static inline std::atomic_uint64_t msgRecvCnt = 0;
    static inline std::atomic_uint32_t inPeersCnt = 0;
    static inline std::atomic_uint32_t outPeersCnt = 0;
    static inline std::atomic_uint32_t deactivateCnt = 0;
    static bool
    deactivated()
    {
        return deactivateCnt == inPeersCnt + outPeersCnt;
    }
};

class OverlayImplTest;
class VirtualNetwork;
class ServerHandler;

/** Represents a virtual node in the overlay. It contains all objects
 * required for Overlay and Peer instantiation.
 */
struct VirtualNode
{
    VirtualNode(
        VirtualNetwork& net,
        boost::asio::io_service& service,
        std::string const& ip,
        bool isFixed,
        std::unordered_map<std::string, std::string> const& bootstrap,
        std::uint16_t peerPort,
        std::uint16_t out_max,
        std::uint16_t in_max)
        : ip_(ip)
        , id_(sid_)
        , io_service_(service)
        , config_(mkConfig(
              ip,
              std::to_string(peerPort),
              isFixed,
              bootstrap,
              out_max,
              in_max))
        , logs_(std::make_unique<jtx::SuiteLogs>(net))
        , timeKeeper_(std::make_unique<ManualTimeKeeper>())
        , collector_(make_CollectorManager(
              config_->section(SECTION_INSIGHT),
              logs_->journal("Collector")))
        , resourceManager_(Resource::make_Manager(
              collector_->collector(),
              logs_->journal("Resource")))
        , resolver_(ResolverAsio::New(
              io_service_,
              logs_->journal(mkName("Overlay", id_))))
        , identity_(randomKeyPair(KeyType::secp256k1))
        , overlay_(std::make_shared<OverlayImplTest>(
              *this,
              peerPort,
              mkName("Overlay", id_)))
        , serverPort_(1)
        , serverHandler_(std::make_unique<ServerHandler>(*overlay_))
        , server_(make_Server(
              *serverHandler_,
              io_service_,
              logs_->journal(mkName("Server", id_))))
        , name_(ip)
        , out_max_(out_max)
        , in_max_(in_max)
    {
        serverPort_.back().ip = beast::IP::Address::from_string(ip);
        serverPort_.back().port = peerPort;
        serverPort_.back().protocol.insert("peer");
        serverPort_.back().context = make_SSLContext("");
        sid_++;
    }
    void
    run();
    static std::unique_ptr<Config>
    mkConfig(
        std::string const& ip,
        std::string const& peerPort,
        bool isFixed,  // if true then ips_fixed, otherwise ips
        std::unordered_map<std::string, std::string> const& bootstrap,
        std::uint16_t out_max,
        std::uint16_t in_max)
    {
        auto config = std::make_unique<Config>();
        config->overwrite(ConfigSection::nodeDatabase(), "type", "memory");
        config->overwrite(ConfigSection::nodeDatabase(), "path", "main");
        config->deprecatedClearSection(ConfigSection::importNodeDatabase());
        config->legacy("database_path", "");

        (*config)["server"].append("port_peer");
        (*config)["port_peer"].set("ip", ip);
        (*config)["port_peer"].set("port", peerPort);
        (*config)["port_peer"].set("protocol", "peer");

        config->PEER_PRIVATE = false;
        config->PEERS_OUT_MAX = out_max;
        config->PEERS_IN_MAX = in_max;

        (*config)["ssl_verify"].append("0");
        for (auto it : bootstrap)
        {
            if (it.first == ip)
                continue;
            if (isFixed)
                config->IPS_FIXED.push_back(it.first + " " + peerPort);
            else
                config->IPS.push_back(it.first + " " + peerPort);
        }
        config->setupControl(true, true, false);
        return config;
    }
    static inline int sid_ = 0;
    std::string ip_;
    int id_;
    boost::asio::io_service& io_service_;
    std::unique_ptr<Config> config_;
    std::unique_ptr<jtx::SuiteLogs> logs_;
    std::unique_ptr<ManualTimeKeeper> timeKeeper_;
    std::unique_ptr<CollectorManager> collector_;
    std::unique_ptr<Resource::Manager> resourceManager_;
    std::unique_ptr<ResolverAsio> resolver_;
    std::pair<PublicKey, SecretKey> identity_;
    std::shared_ptr<OverlayImplTest> overlay_;
    std::vector<Port> serverPort_;
    std::unique_ptr<ServerHandler> serverHandler_;
    std::unique_ptr<Server> server_;
    std::string name_;
    std::uint16_t out_max_;
    std::uint16_t in_max_;
};

/** Represents the Overlay - collection of VirtualNode. Unit tests inherit
 * from this class. It contains one and only io_service for all async
 * operations in the network.
 */
class VirtualNetwork : public beast::unit_test::suite
{
protected:
    // total number of configured outbound peers
    std::uint16_t tot_out_ = 0;
    // total number of configured inbound peers
    std::uint16_t tot_in_ = 0;
    bool log_ = true;
    boost::asio::io_service io_service_;
    boost::thread_group tg_;
    std::mutex nodesMutex_;
    // nodes collection
    std::unordered_map<int, std::shared_ptr<VirtualNode>> nodes_;
    // time test started
    std::chrono::time_point<std::chrono::steady_clock> start_;
    // bootstrap nodes, either ips_fixed or ips
    std::unordered_map<std::string, std::string> bootstrap_;
    // map of global ip to local ip (172.0.x.x)
    bimap ip2Local_;
    std::string baseIp_ = "172.0";
    std::uint16_t static constexpr maxSubaddr_ = 255;

public:
    virtual ~VirtualNetwork() = default;
    VirtualNetwork()
    {
        start_ = std::chrono::steady_clock::now();
    }

    void
    stop();

    boost::asio::io_service&
    io_service()
    {
        return io_service_;
    }

    /** Represents epoch time in seconds since the start of the test.
     */
    std::size_t
    timeSinceStart()
    {
        using namespace std::chrono;
        return duration_cast<seconds>(steady_clock::now() - start_).count();
    }

    std::string
    getGlobalIp(std::string const& localIp)
    {
        return ip2Local_.right.at(localIp);
    }

    std::string
    getLocalIp(std::string const& globalIp)
    {
        return ip2Local_.left.at(globalIp);
    }

protected:
    void
    add(std::shared_ptr<VirtualNode> const& node)
    {
        std::lock_guard l(nodesMutex_);
        nodes_.emplace(node->id_, node);
    }
    void
    mkNode(
        std::string const& ip,
        bool isFixed,
        std::uint16_t out_max,
        std::uint16_t in_max,
        std::uint16_t peerPort = 51235)
    {
        {
            if (out_max == 0)
            {
                out_max++;
                in_max++;
            }
            tot_out_ += out_max;
            tot_in_ += in_max;
            if (log_)
            {
                std::cout << nodes_.size() << " " << ip << " "
                          << ip2Local_.right.at(ip) << " " << out_max << " "
                          << in_max << " " << tot_out_ << " " << tot_in_ << " "
                          << (bootstrap_.find(ip) != bootstrap_.end()
                                  ? bootstrap_[ip]
                                  : "")
                          << "                                \r" << std::flush;
            }
            auto node = std::make_shared<VirtualNode>(
                *this,
                io_service_,
                ip,
                isFixed,
                bootstrap_,
                peerPort,
                out_max,
                in_max);
            add(node);
            node->run();
        }
    }
};

/** P2P required configuration */
class P2PConfigTest : public P2PConfig
{
    VirtualNode const& node_;

public:
    P2PConfigTest(VirtualNode const& node) : node_(node)
    {
    }
    Config const&
    config() const override
    {
        return *node_.config_;
    }
    Logs&
    logs() const override
    {
        return *node_.logs_;
    }
    bool
    isValidator() const override
    {
        return true;
    }
    std::pair<PublicKey, SecretKey> const&
    identity() const override
    {
        return node_.identity_;
    }
    std::optional<std::string>
    clusterMember(PublicKey const&) const override
    {
        return {};
    }
    bool
    reservedPeer(PublicKey const& key) const override
    {
        return false;
    }
    std::optional<std::pair<uint256, uint256>>
    clHashes() const override
    {
        return {};
    }
    NetClock::time_point
    now() const override
    {
        return node_.timeKeeper_->now();
    }
};

/** Thin Application layer peer implementation.
 */
class PeerImpTest : public P2PeerImp<PeerImpTest>
{
    VirtualNode& node_;

public:
    ~PeerImpTest();

    PeerImpTest(
        VirtualNode& node,
        id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        std::unique_ptr<stream_type>&& stream_ptr,
        P2POverlayImpl<PeerImpTest>& overlay)
        : P2PeerImp(
              overlay.p2pConfig(),
              id,
              slot,
              std::move(request),
              publicKey,
              protocol,
              std::move(stream_ptr),
              overlay)
        , node_(node)
    {
    }

    PeerImpTest(
        VirtualNode& node,
        std::unique_ptr<stream_type>&& stream_ptr,
        const_buffers_type const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        id_t id,
        P2POverlayImpl<PeerImpTest>& overlay)
        : P2PeerImp(
              overlay.p2pConfig(),
              std::move(stream_ptr),
              buffers,
              std::move(slot),
              std::move(response),
              publicKey,
              protocol,
              id,
              overlay)
        , node_(node)
    {
    }

    void
    charge(Resource::Charge const&) override
    {
    }

    bool
    cluster() const override
    {
        return false;
    }

    bool
    isHighLatency() const override
    {
        return false;
    }

    int
    getScore(bool) const override
    {
        return 0;
    }

    PublicKey const&
    getNodePublic() const override
    {
        return node_.identity_.first;
    }

    Json::Value
    json() override
    {
        return {};
    }

    bool
    supportsFeature(ProtocolFeature f) const override
    {
        return false;
    }

    std::optional<std::size_t>
    publisherListSequence(PublicKey const&) const override
    {
        return {};
    }

    void
    setPublisherListSequence(PublicKey const&, std::size_t const) override
    {
    }

    uint256 const&
    getClosedLedgerHash() const override
    {
        static uint256 h{};
        return h;
    }

    bool
    hasLedger(uint256 const& hash, std::uint32_t seq) const override
    {
        return false;
    }

    void
    ledgerRange(std::uint32_t& minSeq, std::uint32_t& maxSeq) const override
    {
    }

    bool
    hasTxSet(uint256 const& hash) const override
    {
        return false;
    }

    void
    cycleStatus() override
    {
    }

    bool
    hasRange(std::uint32_t uMin, std::uint32_t uMax) override
    {
        return false;
    }

private:
    //--------------------------------------------------------------------------
    // Delegate custom handling of events to the application layer.
    void
    onEvtRun() override
    {
    }

    bool
    onEvtSendFilter(std::shared_ptr<Message> const&) override
    {
        Counts::msgSendCnt++;
        return false;
    }

    void
    onEvtClose() override
    {
    }

    void
    onEvtGracefulClose() override
    {
    }

    void
    onEvtShutdown() override
    {
    }

    void
    onEvtDoProtocolStart() override
    {
    }

    void
    onMessageBegin(
        std::uint16_t type,
        std::shared_ptr<::google::protobuf::Message> const& m,
        std::size_t size,
        std::size_t uncompressed_size,
        bool isCompressed) override
    {
    }

    void
    onMessageEnd(
        std::uint16_t type,
        std::shared_ptr<::google::protobuf::Message> const& m) override
    {
    }

    void
    onMessage(std::shared_ptr<protocol::TMEndpoints> const& m) override
    {
        Counts::msgRecvCnt++;
        P2PeerImp<PeerImpTest>::onMessage(m);
    }

    bool
    onEvtProtocolMessage(
        ripple::detail::MessageHeader const& header,
        const_buffers_type const& buffers) override
    {
        // should not get here since only message
        // that can be received is TMEndpoints
        // and it's processed in the p2p layer.
        assert(false);
        return false;
    }
};

/** ConnectAttempt must bind to ip/port so that when it connects
 * to the server's endpoint it's not treated as a duplicate ip.
 * If a client doesn't bind to specific ip then it binds to
 * a default ip, which is going to be the same for all clients.
 * Consequently, clients connecting to the same endpoint are
 * treated as the duplicated endpoints and are disconnected.
 */
class ConnectAttemptTest : public ConnectAttempt<PeerImpTest>
{
public:
    ConnectAttemptTest(
        VirtualNode& node,
        P2PConfig const& p2pConfig,
        boost::asio::io_service& io_service,
        endpoint_type const& remote_endpoint,
        Resource::Consumer usage,
        shared_context const& context,
        std::uint32_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        beast::Journal journal,
        P2POverlayImpl<PeerImpTest>& overlay)
        : ConnectAttempt(
              p2pConfig,
              io_service,
              remote_endpoint,
              usage,
              context,
              id,
              slot,
              journal,
              overlay)
    {
        // Bind to this node configured ip
        auto sec = p2pConfig_.config().section("port_peer");
        socket_.open(boost::asio::ip::tcp::v4());
        socket_.bind(boost::asio::ip::tcp::endpoint(
            boost::asio::ip::address::from_string(
                sec.get<std::string>("ip")->c_str()),
            0));
        boost::asio::socket_base::reuse_address reuseAddress(true);
        socket_.set_option(reuseAddress);
    }
};

/** Thin application layer overlay implementation.
 */
class OverlayImplTest : public P2POverlayImpl<PeerImpTest>
{
private:
    VirtualNode& node_;
    std::atomic_uint16_t nIn_{0};
    std::atomic_uint16_t nOut_{0};

public:
    OverlayImplTest(
        VirtualNode& node,
        std::uint16_t port,
        std::string const& name)
        : P2POverlayImpl<PeerImpTest>(
              std::make_unique<P2PConfigTest>(node),
              setup_Overlay(*node.config_),
              port,
              *node.resourceManager_,
              *node.resolver_,
              node.io_service_,
              *node.config_,
              node.collector_->collector())
        , node_(node)
    {
    }
    ~OverlayImplTest() = default;

    Json::Value
    json() override
    {
        return {};
    }

    void checkTracking(std::uint32_t) override
    {
    }

    void
    broadcast(protocol::TMProposeSet&) override
    {
    }

    void
    broadcast(protocol::TMValidation&) override
    {
    }

    std::set<Peer::id_t>
    relay(
        protocol::TMProposeSet& m,
        uint256 const& uid,
        PublicKey const& validator) override
    {
        return {};
    }

    std::set<Peer::id_t>
    relay(
        protocol::TMValidation& m,
        uint256 const& uid,
        PublicKey const& validator) override
    {
        return {};
    }

    void
    incJqTransOverflow() override
    {
    }

    std::uint64_t
    getJqTransOverflow() const override
    {
        return 0;
    }

    void
    incPeerDisconnect() override
    {
    }

    std::uint64_t
    getPeerDisconnect() const override
    {
        return 0;
    }

    void
    incPeerDisconnectCharges() override
    {
    }

    std::uint64_t
    getPeerDisconnectCharges() const override
    {
        return 0;
    }

    Json::Value
    crawlShards(bool includePublicKey, std::uint32_t hops) override
    {
        return {};
    }

    void
    outputPeers(std::ofstream& of, bimap const& ip2Local)
    {
        std::lock_guard l(mutex_);
        for (auto peer : getActivePeers())
        {
            auto p2Peer =
                std::static_pointer_cast<P2PeerImp<PeerImpTest>>(peer);
            of << ip2Local.right.at(node_.ip_) << ","
               << ip2Local.right.at(
                      peer->getRemoteAddress().address().to_string())
               << "," << (p2Peer->slot()->inbound() ? "in" : "out")
               << std::endl;
        }
    }

    void
    onPeerDeactivate(bool inbound)
    {
        if (inbound)
            nIn_--;
        else
            nOut_--;
    }

    std::pair<std::uint16_t, std::uint16_t>
    getPeersCounts()
    {
        return {nOut_.load(), nIn_.load()};
    }

private:
    bool
    onEvtProcessRequest(http_request_type const& req, Handoff& handoff) override
    {
        return false;
    }

    std::shared_ptr<PeerImpTest>
    mkInboundPeer(
        Peer::id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        Resource::Consumer consumer,
        std::unique_ptr<stream_type>&& stream_ptr) override
    {
        Counts::inPeersCnt++;
        nIn_++;
        return std::make_shared<PeerImpTest>(
            node_,
            id,
            slot,
            std::move(request),
            publicKey,
            protocol,
            std::move(stream_ptr),
            *this);
    }

    std::shared_ptr<PeerImpTest>
    mkOutboundPeer(
        std::unique_ptr<stream_type>&& stream_ptr,
        boost::beast::multi_buffer const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        Resource::Consumer usage,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        Peer::id_t id) override
    {
        Counts::outPeersCnt++;
        nOut_++;
        return std::make_shared<PeerImpTest>(
            node_,
            std::move(stream_ptr),
            buffers.data(),
            std::move(slot),
            std::move(response),
            publicKey,
            protocol,
            id,
            *this);
    }

    std::shared_ptr<ConnectAttempt<PeerImpTest>>
    mkConnectAttempt(
        beast::IP::Endpoint const& remote_endpoint,
        Resource::Consumer const& usage,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        std::uint16_t id) override
    {
        return std::make_shared<ConnectAttemptTest>(
            node_,
            p2pConfig(),
            io_service_,
            beast::IPAddressConversion::to_asio_endpoint(remote_endpoint),
            usage,
            setup_.context,
            id++,
            slot,
            p2pConfig().logs().journal("Peer"),
            *this);
    }

    void
    onEvtTimer() override
    {
    }
};

PeerImpTest::~PeerImpTest()
{
    Counts::deactivateCnt++;
    static_cast<OverlayImplTest&>(overlay_).onPeerDeactivate(inbound_);
}

void
VirtualNode::run()
{
    server_->ports(serverPort_);
    overlay_->start();
}

void
VirtualNetwork::stop()
{
    std::lock_guard l(nodesMutex_);

    for (auto& node : nodes_)
    {
        node.second->server_.reset();
        node.second->overlay_->stop();
    }
    io_service_.stop();
}

/** Handoff inbound connection to the OverlayImplTest */
class ServerHandler
{
    OverlayImplTest& overlay_;

public:
    ServerHandler(OverlayImplTest& overlay) : overlay_(overlay)
    {
    }

    bool
    onAccept(Session& session, boost::asio::ip::tcp::endpoint endpoint)
    {
        return true;
    }

    Handoff
    onHandoff(
        Session& session,
        std::unique_ptr<stream_type>&& bundle,
        http_request_type&& request,
        boost::asio::ip::tcp::endpoint remote_address)
    {
        return overlay_.onHandoff(
            std::move(bundle), std::move(request), remote_address);
    }

    Handoff
    onHandoff(
        Session& session,
        http_request_type&& request,
        boost::asio::ip::tcp::endpoint remote_address)
    {
        return onHandoff(
            session,
            {},
            std::forward<http_request_type>(request),
            remote_address);
    }

    void
    onRequest(Session& session)
    {
        if (beast::rfc2616::is_keep_alive(session.request()))
            session.complete();
        else
            session.close(true);
    }

    void
    onWSMessage(
        std::shared_ptr<WSSession> session,
        std::vector<boost::asio::const_buffer> const&)
    {
    }

    void
    onClose(Session& session, boost::system::error_code const&)
    {
    }

    void
    onStopped(Server& server)
    {
    }
};

/** Test Overlay network with five nodes with ip in range 172.0.0.0-172.0.0.4.
 * Ip's must be pre-configured (see overlay_xrpl_test below). The test stops
 * after total of 20 peers or 15 seconds.
 */
class overlay_net_test : public VirtualNetwork
{
protected:
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> overlayTimer_;
    std::uint16_t duration_ = 3600;
    bool resolve_ = false;
    std::uint16_t timeLapse_;

public:
    overlay_net_test() : overlayTimer_(io_service_), timeLapse_(1)
    {
    }

    void
    startNodes(std::vector<std::string> const& nodes)
    {
        for (auto n : nodes)
            mkNode(n, true, 20, 20);
        for (unsigned i = 0; i < boost::thread::hardware_concurrency(); ++i)
            tg_.create_thread(
                boost::bind(&boost::asio::io_service::run, &io_service_));
        tg_.join_all();
    }

    void
    testOverlay()
    {
        testcase("Overlay");
        auto mkIp = [&](auto str) {
            std::string ip = baseIp_ + str;
            ip2Local_.insert(bimap::value_type(ip, ip));
            bootstrap_[ip] = ip;
            return ip;
        };
        std::vector<std::string> nodes = {
            mkIp(".0.0"),
            mkIp(".0.1"),
            mkIp(".0.2"),
            mkIp(".0.3"),
            mkIp(".0.4")};
        setTimer();
        startNodes(nodes);
        std::cout << "peers " << Counts::inPeersCnt << " "
                  << Counts::outPeersCnt << " " << Counts::deactivateCnt
                  << std::endl;
        std::cout << "messages " << Counts::msgRecvCnt << " "
                  << Counts::msgSendCnt << std::endl;
        BEAST_EXPECT(
            Counts::inPeersCnt + Counts::outPeersCnt == 20 &&
            Counts::deactivated());
        BEAST_EXPECT(
            Counts::msgSendCnt > 0 && Counts::msgSendCnt == Counts::msgRecvCnt);
    }

    virtual void
    onOverlayTimer(boost::system::error_code const& ec)
    {
        if (ec || (Counts::outPeersCnt + Counts::inPeersCnt == 20) ||
            timeSinceStart() > 20)
        {
            stop();
        }
        else
            setTimer();
    }

    void
    setTimer()
    {
        overlayTimer_.expires_from_now(std::chrono::seconds(timeLapse_));
        overlayTimer_.async_wait(std::bind(
            &overlay_net_test::onOverlayTimer, this, std::placeholders::_1));
    }

    void
    run() override
    {
        testOverlay();
    }
};

/** Test of the Overlay network. Network configuration - adjacency matrix
 * with the type of connection (outbound/inbound) is passed in as
 * the unit test argument. The matrix can be generated by crawling XRPL
 * network. The global ip's are mapped to local 172.x.x.x ip's, which
 * must pre-configured in the system. On Ubuntu 20.20 (tested system)
 * ip's can be configured as:
 *    ip link add dummy1 type dummy
 *    ip address add 172.0.0.1/255.255.255.0 dev dummy1
 * On MAX OSX ip's can be configured as:
 *    ifconfig lo0 -alias 172.0.0.1
 * In addition, the number of open files must be increased to 65535
 * (On Ubuntu 20.20/MAX OSX: ulimit -n 65535. On Ubuntu may also need to update
 * /etc/security/limits.conf, /etc/sysctl.conf, /etc/pam.d/common-session,
 * /etc/systemd/system.conf). The test runs until no changes are detected in the
 * network - the number of in/out peers remains the same after four minutes or
 * the test duration exceeds duration_ sec. duration_ can be passed to the test
 * as an argument.
 */
class overlay_xrpl_test : public overlay_net_test
{
    // Network configuration of outbound/inbound max peer for each node.
    std::map<std::string, std::map<std::string, std::uint16_t>> netConfig_;
    // Total outbound/inbound peers in the network at each logged time point.
    // We stop when the number of peers doesn't change after a few iterations.
    std::vector<std::uint16_t> tot_peers_out_;
    std::vector<std::uint16_t> tot_peers_in_;
    // options
    std::string adjMatrixPath_ = "";

public:
    overlay_xrpl_test()
    {
        timeLapse_ = 40;
    }
    /** Set bootstrap_, netConfig_, and ip2Local_. Use the adjacency matrix.
     * Map global ip to local ip.
     */
    void
    getNetConfig()
    {
        std::
            map<std::string, std::map<std::string, std::map<std::string, bool>>>
                all;
        std::uint16_t cnt = 1;
        std::fstream file(adjMatrixPath_, std::ios::in);
        assert(file.is_open());
        std::string line;
        // map to local ip
        auto map2Local = [&](std::string const& ip) {
            if (ip2Local_.left.find(ip) == ip2Local_.left.end())
            {
                std::stringstream str;
                str << baseIp_ << "." << (cnt / (maxSubaddr_ + 1)) << "."
                    << (cnt % (maxSubaddr_ + 1));
                ip2Local_.insert(bimap::value_type(ip, str.str()));
                cnt++;
            }
            return ip2Local_.left.at(ip);
        };
        // for each ip figure out out_max and in_max.
        // for an entry ip,ip1,'in|out'
        // increment ip:max_in|max_out.
        // for each ip,ip1,'in' and ip,ip1,'out' entry,
        // if corresponding ip1,ip,'out' or ip1,ip,'in'
        // are not present then increment ip1:max_out|max_in
        while (getline(file, line))
        {
            boost::smatch match;
            boost::regex rx("^([^,]+),([^,]+),(in|out)");
            assert(boost::regex_search(line, match, rx));
            auto const ip = map2Local(match[1]);
            auto const ip1 = map2Local(match[2]);
            auto const ctype = match[3];
            if (all.find(ip) == all.end() || all[ip].find(ip1) == all[ip].end())
                netConfig_[ip][ctype]++;
            all[ip][ip1][ctype] = true;
            if (all.find(ip1) == all.end() ||
                all[ip1].find(ip) == all[ip1].end())
            {
                std::string const t = (ctype == "in") ? "out" : "in";
                all[ip1][ip][t] = true;
                netConfig_[ip1][t] += 1;
            }
        }
        // Figure out which ips in the adjacency matrix represent
        // the bootstrap servers of ripple, alloy, and isrdc. Those
        // servers are added to each node's ips configuration as "local"
        // ip's.
        auto mapLocal = [&](auto ip, auto host) {
            if (auto it1 = ip2Local_.left.find(ip); it1 != ip2Local_.left.end())
                bootstrap_[it1->second] = host;
        };
        // manual resolve for offline testing
        if (resolve_)
        {
            auto resolveManual = [&](auto hosts, auto host) {
                std::for_each(hosts.begin(), hosts.end(), [&](auto& it) {
                    mapLocal(it, host);
                });
            };
            std::vector<std::string> ripple = {
                "34.205.233.231",
                "169.55.164.29",
                "198.11.206.6",
                "169.55.164.21",
                "198.11.206.26",
                "52.25.71.90",
                "3.216.68.48",
                "54.190.253.12"};
            resolveManual(ripple, "r.ripple.ee");
            std::vector<std::string> alloy = {
                "46.4.218.119",
                "88.99.137.170",
                "116.202.148.26",
                "136.243.24.38",
                "95.216.102.188",
                "46.4.138.103",
                "46.4.218.120",
                "116.202.163.130",
                "95.216.102.182",
                "94.130.221.2",
                "95.216.5.218"};
            resolveManual(alloy, "zaphod.alloy.ee");
            std::vector<std::string> isrdc = {"59.185.224.109"};
            resolveManual(isrdc, "sahyadri.isrdc.in");
        }
        else
        {
            auto resolve = [&](auto host) {
                boost::asio::ip::tcp::resolver resolver(io_service_);
                boost::asio::ip::tcp::resolver::query query(host, "80");
                boost::asio::ip::tcp::resolver::iterator iter =
                    resolver.resolve(query);
                std::for_each(iter, {}, [&](auto& it) {
                    auto ip = it.endpoint().address().to_string();
                    mapLocal(ip, host);
                });
            };
            resolve("r.ripple.com");
            resolve("zaphod.alloy.ee");
            resolve("sahyadri.isrdc.in");
        }
    }

    bool
    parseArg()
    {
        if (arg() == "")
            return false;
        boost::char_separator<char> sep(",");
        boost::char_separator<char> sep1(":");
        typedef boost::tokenizer<boost::char_separator<char>> t_tokenizer;
        t_tokenizer tok(arg(), sep);
        for (auto it = tok.begin(); it != tok.end(); ++it)
        {
            if (adjMatrixPath_ == "")
                adjMatrixPath_ = *it;
            else if ((*it).substr(0, 3) == "ip:")
                baseIp_ = (*it).substr(3);
            else if (*it == "nolog")
                log_ = false;
            else if ((*it).substr(0, 9) == "duration:")
                duration_ = std::stoi((*it).substr(9));
            else if (*it == "resolve")
                resolve_ = true;
            else
            {
                std::cout << "invalid argument " << *it << std::endl;
                return false;
            }
        }
        return (adjMatrixPath_ != "");
    }

    void
    testXRPLOverlay()
    {
        testcase("XRPLOverlay");
        if (!parseArg())
        {
            fail("adjacency matrix must be provided");
            return;
        }

        std::remove("stop");

        getNetConfig();
        startNodes();
        BEAST_EXPECT(Counts::deactivated());
        BEAST_EXPECT(
            Counts::msgSendCnt > 0 && Counts::msgSendCnt == Counts::msgRecvCnt);
    }

    void
    startNodes()
    {
        std::vector<std::string> ips;
        ips.reserve(netConfig_.size());
        std::for_each(netConfig_.begin(), netConfig_.end(), [&](auto it) {
            ips.push_back(it.first);
        });
        std::shuffle(ips.begin(), ips.end(), default_prng());
        for (auto ip : ips)
            mkNode(ip, false, netConfig_[ip]["out"], netConfig_[ip]["in"]);
        std::cout << "total out: " << tot_out_ << ", total in: " << tot_in_
                  << "                             \n";
        setTimer();
        for (unsigned i = 0; i < boost::thread::hardware_concurrency(); ++i)
            tg_.create_thread(
                boost::bind(&boost::asio::io_service::run, &io_service_));
        tg_.join_all();
    }

    void
    outputNetwork()
    {
        std::ofstream of("network.out");
        for (auto [id, node] : nodes_)
            node->overlay_->outputPeers(of, ip2Local_);
    }

    void
    onOverlayTimer(boost::system::error_code const& ec) override
    {
        if (ec)
        {
            stop();
            return;
        }

        std::ifstream inf("stop");
        if (timeSinceStart() > duration_ || inf.good() || !doLog())
        {
            outputNetwork();
            stop();
        }
        else
        {
            setTimer();
        }
    }

    bool
    doLog()
    {
        using namespace std::chrono;
        std::vector<float> pct_out;
        std::vector<float> pct_in;
        std::vector<float> pct_def_out;
        std::vector<float> pct_def_in;
        std::vector<std::uint16_t> peers_out;
        std::vector<std::uint16_t> peers_in;
        std::uint16_t Nin = 0;
        std::uint16_t Nout = 0;
        float avg_pct_out = 0.;
        float avg_pct_in = 0.;
        float avg_pct_def_out = 0.;
        float avg_pct_def_in = 0.;
        float avg_peers_out = 0.;
        float avg_peers_in = 0.;
        std::uint16_t out_max = 0;
        std::uint16_t in_max = 0;
        std::uint16_t tot_out = 0;
        std::uint16_t tot_in = 0;
        std::uint16_t no_peers = 0;
        std::vector<float> lcache;
        float avg_lcache = 0;
        std::vector<float> bcache;
        float avg_bcache = 0;
        for (auto [id, node] : nodes_)
        {
            (void)id;
            auto [nout, nin] = node->overlay_->getPeersCounts();
            if ((nout + nin) == 0)
                no_peers++;
            if (node->out_max_ > 0)
            {
                tot_out += nout;
                Nout++;
                avg_peers_out += nout;
                if (nout > out_max)
                    out_max = nout;
                peers_out.push_back(nout);
                avg_pct_out += 100. * nout / node->out_max_;
                pct_out.push_back(100. * nout / node->out_max_);
            }
            if (node->in_max_ > 0)
            {
                tot_in += nin;
                Nin++;
                avg_peers_in += nin;
                if (nin > in_max)
                    in_max = nin;
                peers_in.push_back(nin);
                avg_pct_in += 100. * nin / node->in_max_;
                pct_in.push_back(100. * nin / node->in_max_);
            }
            std::uint16_t deflt = 21;
            if (node->in_max_ > 0 && (node->out_max_ + node->in_max_ <= deflt))
            {
                avg_pct_def_in += 100. * nin / node->in_max_;
                avg_pct_def_out += 100. * nout / node->out_max_;
                pct_def_out.push_back(100. * nout / node->out_max_);
                pct_def_in.push_back(100. * nin / node->in_max_);
            }
            avg_lcache += node->overlay_->peerFinder().livecacheSize();
            lcache.push_back(node->overlay_->peerFinder().livecacheSize());
            avg_bcache += node->overlay_->peerFinder().bootcacheSize();
            bcache.push_back(node->overlay_->peerFinder().bootcacheSize());
        }
        auto stats = [](auto& avg, auto N, auto sample) {
            avg = avg / N;
            float sd = 0.;
            for (auto d : sample)
                sd += (d - avg) * (d - avg);
            if (N > 1)
                sd = sqrt(sd) / (N - 1);
            return sd;
        };
        auto sd_peers_out = stats(avg_peers_out, Nout, peers_out);
        auto sd_peers_in = stats(avg_peers_in, Nin, peers_in);
        auto sd_pct_out = stats(avg_pct_out, Nout, pct_out);
        auto sd_pct_in = stats(avg_pct_in, Nin, pct_in);
        auto sd_pct_def_out =
            stats(avg_pct_def_out, pct_def_out.size(), pct_def_out);
        auto sd_pct_def_in =
            stats(avg_pct_def_in, pct_def_in.size(), pct_def_in);
        auto sd_lcache = stats(avg_lcache, lcache.size(), lcache);
        auto sd_bcache = stats(avg_bcache, bcache.size(), bcache);
        {
            std::cout.precision(2);
            std::cout << timeSinceStart() << ", out: " << tot_out
                      << ", in: " << tot_in << ", snd: " << Counts::msgSendCnt
                      << ", rcv: " << Counts::msgRecvCnt
                      << ", deact: " << Counts::deactivateCnt
                      << ", max out/in: " << out_max << "/" << in_max
                      << ", avg out/in: " << avg_peers_out << "/"
                      << sd_peers_out << ", " << avg_peers_in << "/"
                      << sd_peers_in << ", avg pct out/in: " << avg_pct_out
                      << "/" << sd_pct_out << ", " << avg_pct_in << "/"
                      << sd_pct_in
                      << ", avg pct default out/in: " << avg_pct_def_out << "/"
                      << sd_pct_def_out << ", " << avg_pct_def_in << "/"
                      << sd_pct_def_in << ", no peers: " << no_peers
                      << ", live cache: " << avg_lcache << "/" << sd_lcache
                      << ", boot cache: " << avg_bcache << "/" << sd_bcache
                      << std::endl
                      << std::flush;
        }
        if (tot_peers_in_.size() > 0 &&
            tot_peers_in_[tot_peers_in_.size() - 1] != tot_in)
            tot_peers_in_.clear();
        if (tot_peers_out_.size() > 0 &&
            tot_peers_out_[tot_peers_out_.size() - 1] != tot_out)
            tot_peers_out_.clear();
        tot_peers_in_.push_back(tot_in);
        tot_peers_out_.push_back(tot_out);
        // stop if the network doesn't change
        if (tot_peers_in_.size() >= 6 && tot_peers_out_.size() >= 6)
        {
            return false;
        }
        return true;
    }

    void
    run() override
    {
        testXRPLOverlay();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(overlay_net, overlay, ripple);
BEAST_DEFINE_TESTSUITE_MANUAL(overlay_xrpl, overlay, ripple);

}  // namespace test

}  // namespace ripple