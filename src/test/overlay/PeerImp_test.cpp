#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Compression.h>
#include <xrpld/overlay/Message.h>
#include <xrpld/overlay/detail/Handshake.h>
#include <xrpld/overlay/detail/OverlayImpl.h>
#include <xrpld/overlay/detail/PeerImp.h>
#include <xrpld/overlay/detail/ProtocolVersion.h>
#include <xrpld/peerfinder/Slot.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/make_SSLContext.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STValidation.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/server/Handoff.h>
#include <xrpl/shamap/SHAMapNodeID.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>

#include <xrpl.pb.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::test {

class PeerImp_test : public beast::unit_test::suite
{
    using socket_type = boost::asio::ip::tcp::socket;
    using middle_type = boost::beast::tcp_stream;
    using stream_type = boost::beast::ssl_stream<middle_type>;
    using shared_context = std::shared_ptr<boost::asio::ssl::context>;

    class PeerTest : public PeerImp
    {
    public:
        PeerTest(
            Application& app,
            Peer::id_t id,
            std::shared_ptr<PeerFinder::Slot> const& slot,
            http_request_type&& request,
            PublicKey const& publicKey,
            ProtocolVersion protocol,
            Resource::Consumer consumer,
            std::unique_ptr<PeerImp_test::stream_type>&& streamPtr,
            OverlayImpl& overlay)
            : PeerImp(
                  app,
                  id,
                  slot,
                  std::move(request),
                  publicKey,
                  protocol,
                  consumer,
                  std::move(streamPtr),
                  overlay)
        {
        }

        void
        run() override
        {
        }

        void
        send(std::shared_ptr<Message> const& m) override
        {
            sent_.push_back(m);
        }

        std::vector<std::shared_ptr<Message>> const&
        sent() const
        {
            return sent_;
        }

        void
        clearSent()
        {
            sent_.clear();
        }

    private:
        std::vector<std::shared_ptr<Message>> sent_;
    };

    shared_context context_{make_SSLContext("")};
    Peer::id_t nextID_{1};
    std::uint16_t nextPort_{41000};

    static std::unique_ptr<Config>
    config()
    {
        return jtx::envconfig([](std::unique_ptr<Config> cfg) {
            cfg->LEDGER_REPLAY = true;
            cfg->TX_REDUCE_RELAY_ENABLE = true;
            cfg->TX_REDUCE_RELAY_METRICS = true;
            cfg->TX_REDUCE_RELAY_MIN_PEERS = 10;
            cfg->TX_RELAY_PERCENTAGE = 10;
            cfg->RELAY_UNTRUSTED_PROPOSALS = 1;
            cfg->RELAY_UNTRUSTED_VALIDATIONS = 1;
            return cfg;
        });
    }

    template <class MessageType>
    MessageType
    parse(std::shared_ptr<Message> const& message)
    {
        MessageType parsed;
        auto const& buffer = message->getBuffer(compression::Compressed::Off);
        BEAST_EXPECT(buffer.size() >= compression::headerBytes);
        BEAST_EXPECT(parsed.ParseFromArray(
            buffer.data() + compression::headerBytes, buffer.size() - compression::headerBytes));
        return parsed;
    }

    static std::string
    bytes(uint256 const& value)
    {
        return {reinterpret_cast<char const*>(value.data()), value.size()};
    }

    static uint256
    digest(std::string const& value)
    {
        return sha512Half(value);
    }

    protocol::TMValidation
    makeValidationMessage(
        jtx::Env& env,
        std::string const& name,
        NetClock::duration signOffset = NetClock::duration{0},
        std::uint32_t ledgerSeq = 1)
    {
        auto const secret = randomSecretKey();
        auto const publicKey = derivePublicKey(KeyType::secp256k1, secret);
        auto validation = std::make_shared<STValidation>(
            env.app().getTimeKeeper().closeTime() + signOffset,
            publicKey,
            secret,
            calcNodeID(publicKey),
            [&](STValidation& v) {
                v.setFieldU32(sfLedgerSequence, ledgerSeq);
                v.setFieldH256(sfLedgerHash, digest(name));
            });

        auto const serialized = validation->getSerialized();
        protocol::TMValidation message;
        message.set_validation(serialized.data(), serialized.size());
        return message;
    }

    void
    pump(boost::asio::io_context& ioc)
    {
        ioc.restart();
        for (int i = 0; i < 32 && ioc.poll_one() != 0; ++i)
        {
        }
    }

    std::shared_ptr<PeerTest>
    createPeer(
        jtx::Env& env,
        bool txReduceRelay = true,
        bool activateSlot = false,
        ProtocolVersion protocol = make_protocol(2, 2))
    {
        auto& overlay = dynamic_cast<OverlayImpl&>(env.app().getOverlay());
        http_request_type request;
        request.insert("User-Agent", "PeerImp_test/1.0");
        request.insert("Server-Domain", "peerimp.example");
        request.insert("Network-ID", "21337");
        request.insert("Crawl", "public");
        if (txReduceRelay)
        {
            request.insert("X-Protocol-Ctl", makeFeaturesRequestHeader(false, true, true, false));
        }

        auto streamPtr =
            std::make_unique<stream_type>(socket_type(env.app().getIOContext()), *context_);

        auto const port = nextPort_++;
        beast::IP::Endpoint const local(boost::asio::ip::make_address("172.16.0.1"), port);
        beast::IP::Endpoint const remote(boost::asio::ip::make_address("172.16.0.2"), port);

        PublicKey const key(std::get<0>(randomKeyPair(KeyType::ed25519)));
        auto consumer = overlay.resourceManager().newInboundEndpoint(remote);
        auto [slot, result] = overlay.peerFinder().new_inbound_slot(local, remote);
        BEAST_EXPECT(result == PeerFinder::Result::success);

        if (activateSlot)
            BEAST_EXPECT(
                overlay.peerFinder().activate(slot, key, true) == PeerFinder::Result::success);

        auto peer = std::make_shared<PeerTest>(
            env.app(),
            nextID_++,
            slot,
            std::move(request),
            key,
            protocol,
            consumer,
            std::move(streamPtr),
            overlay);

        overlay.add_active(peer);
        return peer;
    }

    void
    testIdentityAndFeatures()
    {
        testcase("identity and features");

        jtx::Env env(*this, config());
        auto peer = createPeer(env);

        BEAST_EXPECT(peer->crawl());
        BEAST_EXPECT(peer->getVersion() == "PeerImp_test/1.0");
        BEAST_EXPECT(peer->compressionEnabled() == false);
        BEAST_EXPECT(peer->txReduceRelayEnabled());
        BEAST_EXPECT(peer->supportsFeature(ProtocolFeature::ValidatorListPropagation));
        BEAST_EXPECT(peer->supportsFeature(ProtocolFeature::ValidatorList2Propagation));
        BEAST_EXPECT(peer->supportsFeature(ProtocolFeature::LedgerReplay));

        PublicKey const publisher(std::get<0>(randomKeyPair(KeyType::ed25519)));
        BEAST_EXPECT(!peer->publisherListSequence(publisher));
        peer->setPublisherListSequence(publisher, 7);
        BEAST_EXPECT(peer->publisherListSequence(publisher).value_or(0) == 7);

        auto json = peer->json();
        BEAST_EXPECT(json[jss::public_key].isString());
        BEAST_EXPECT(json[jss::address].isString());
        BEAST_EXPECT(json[jss::inbound].asBool());
        BEAST_EXPECT(json[jss::server_domain].asString() == "peerimp.example");
        BEAST_EXPECT(json[jss::network_id].asString() == "21337");
        BEAST_EXPECT(json[jss::version].asString() == "PeerImp_test/1.0");
        BEAST_EXPECT(json[jss::protocol].asString() == "XRPL/2.2");
    }

    void
    testLedgerState()
    {
        testcase("ledger state");

        jtx::Env env(*this, config());
        auto peer = createPeer(env);

        auto const closed = digest("closed");
        auto const previous = digest("previous");

        auto status = std::make_shared<protocol::TMStatusChange>();
        status->set_newevent(protocol::neACCEPTED_LEDGER);
        status->set_newstatus(protocol::nsVALIDATING);
        status->set_ledgerseq(10);
        status->set_firstseq(8);
        status->set_lastseq(12);
        status->set_ledgerhash(bytes(closed));
        status->set_ledgerhashprevious(bytes(previous));

        peer->onMessage(status);

        std::uint32_t minSeq = 0;
        std::uint32_t maxSeq = 0;
        peer->ledgerRange(minSeq, maxSeq);
        BEAST_EXPECT(minSeq == 8);
        BEAST_EXPECT(maxSeq == 12);
        BEAST_EXPECT(peer->hasLedger(closed, 0));
        BEAST_EXPECT(peer->hasLedger(previous, 0));
        BEAST_EXPECT(peer->hasRange(8, 12));
        BEAST_EXPECT(peer->json()[jss::status].asString() == "validating");
        BEAST_EXPECT(peer->json()[jss::complete_ledgers].asString() == "8 - 12");

        peer->checkTracking(10, 10);
        BEAST_EXPECT(peer->hasLedger(uint256{}, 9));
        peer->checkTracking(1, 1000);
        BEAST_EXPECT(!peer->hasRange(8, 12));

        peer->cycleStatus();
        BEAST_EXPECT(peer->getClosedLedgerHash().isZero());

        auto lost = std::make_shared<protocol::TMStatusChange>();
        lost->set_newevent(protocol::neLOST_SYNC);
        peer->onMessage(lost);
        BEAST_EXPECT(peer->getClosedLedgerHash().isZero());
    }

    void
    testPingAndTxQueue()
    {
        testcase("ping and tx queue");

        jtx::Env env(*this, config());
        auto peer = createPeer(env);

        auto ping = std::make_shared<protocol::TMPing>();
        ping->set_type(protocol::TMPing::ptPING);
        ping->set_seq(42);

        peer->onMessage(ping);
        BEAST_EXPECT(peer->sent().size() == 1);
        auto pong = parse<protocol::TMPing>(peer->sent().back());
        BEAST_EXPECT(pong.type() == protocol::TMPing::ptPONG);
        BEAST_EXPECT(pong.seq() == 42);

        peer->clearSent();
        auto const hash1 = digest("tx1");
        auto const hash2 = digest("tx2");
        peer->addTxQueue(hash1);
        pump(env.app().getIOContext());
        peer->addTxQueue(hash2);
        pump(env.app().getIOContext());
        peer->removeTxQueue(hash1);
        pump(env.app().getIOContext());
        peer->sendTxQueue();
        pump(env.app().getIOContext());

        if (!peer->sent().empty())
        {
            auto have = parse<protocol::TMHaveTransactions>(peer->sent().back());
            BEAST_EXPECT(have.hashes_size() == 1);
            BEAST_EXPECT(have.hashes(0) == bytes(hash2));
        }
    }

    void
    testProtocolMessages()
    {
        testcase("protocol messages");

        jtx::Env env(*this, config());
        auto peer = createPeer(env, true, true);
        peer->checkTracking(10, 10);

        std::vector<PeerFinder::Endpoint> endpoints = {
            {beast::IP::Endpoint::from_string("8.8.8.8:51235"), 2},
            {beast::IP::Endpoint::from_string("9.9.9.9:51235"), 3},
        };
        peer->sendEndpoints(endpoints.begin(), endpoints.end());
        BEAST_EXPECT(peer->sent().size() == 1);
        auto sentEndpoints = parse<protocol::TMEndpoints>(peer->sent().back());
        BEAST_EXPECT(sentEndpoints.version() == 2);
        BEAST_EXPECT(sentEndpoints.endpoints_v2_size() == 2);

        auto incomingEndpoints = std::make_shared<protocol::TMEndpoints>();
        incomingEndpoints->set_version(2);
        auto* incoming = incomingEndpoints->add_endpoints_v2();
        incoming->set_endpoint("8.8.4.4:51235");
        incoming->set_hops(2);
        peer->onMessage(incomingEndpoints);

        peer->clearSent();
        auto have = std::make_shared<protocol::TMHaveTransactions>();
        auto const missing = digest("missing");
        have->add_hashes(missing.data(), missing.size());
        peer->onMessage(have);
        env.app().getJobQueue().rendezvous();
        BEAST_EXPECT(peer->sent().size() == 1);
        auto request = parse<protocol::TMGetObjectByHash>(peer->sent().back());
        BEAST_EXPECT(request.type() == protocol::TMGetObjectByHash::otTRANSACTIONS);
        BEAST_EXPECT(request.query());
        BEAST_EXPECT(request.objects_size() == 1);
        BEAST_EXPECT(request.objects(0).hash() == bytes(missing));

        auto txs = std::make_shared<protocol::TMTransactions>();
        peer->onMessage(txs);
    }

    void
    testFeatureAndMessageGuards()
    {
        testcase("feature and message guards");

        jtx::Env env(*this, config());
        auto peer = createPeer(env, false);

        BEAST_EXPECT(!peer->txReduceRelayEnabled());

        auto manifests = std::make_shared<protocol::TMManifests>();
        peer->onMessage(manifests);

        peer->onMessage(std::make_shared<protocol::TMCluster>());

        auto haveTransactions = std::make_shared<protocol::TMHaveTransactions>();
        haveTransactions->add_hashes("bad", 3);
        peer->onMessage(haveTransactions);

        auto transactions = std::make_shared<protocol::TMTransactions>();
        peer->onMessage(transactions);

        auto txRequest = std::make_shared<protocol::TMGetObjectByHash>();
        txRequest->set_type(protocol::TMGetObjectByHash::otTRANSACTIONS);
        txRequest->set_query(true);
        peer->onMessage(txRequest);

        peer->onMessage(std::make_shared<protocol::TMProofPathRequest>());
        peer->onMessage(std::make_shared<protocol::TMProofPathResponse>());
        peer->onMessage(std::make_shared<protocol::TMReplayDeltaRequest>());
        peer->onMessage(std::make_shared<protocol::TMReplayDeltaResponse>());

        auto unsupported = createPeer(env, true, false, make_protocol(1, 0));
        unsupported->onMessage(std::make_shared<protocol::TMValidatorList>());

        auto listCollection = std::make_shared<protocol::TMValidatorListCollection>();
        listCollection->set_version(1);
        peer->onMessage(listCollection);

        auto endpoints = std::make_shared<protocol::TMEndpoints>();
        endpoints->set_version(1);
        peer->onMessage(endpoints);
    }

    void
    testLedgerRequestValidation()
    {
        testcase("ledger request validation");

        jtx::Env env(*this, config());
        auto const hash = digest("ledger");
        SHAMapNodeID const nodeID(64, hash);

        auto send = [&](auto&& setup, bool wait = false) {
            auto peer = createPeer(env);
            auto message = std::make_shared<protocol::TMGetLedger>();
            setup(*message);
            peer->onMessage(message);
            if (wait)
                env.app().getJobQueue().rendezvous();
        };

        send([](protocol::TMGetLedger& message) { message.set_itype(protocol::liTS_CANDIDATE); });

        send([](protocol::TMGetLedger& message) { message.set_itype(protocol::liBASE); });

        send([&](protocol::TMGetLedger& message) {
            message.set_itype(protocol::liBASE);
            message.set_ledgerhash("bad");
        });

        send([&](protocol::TMGetLedger& message) {
            message.set_itype(protocol::liBASE);
            message.set_ledgerseq(env.closed()->seq() + 1000);
        });

        send([&](protocol::TMGetLedger& message) {
            message.set_itype(protocol::liTX_NODE);
            message.set_ledgerhash(bytes(hash));
        });

        send([&](protocol::TMGetLedger& message) {
            message.set_itype(protocol::liTX_NODE);
            message.set_ledgerhash(bytes(hash));
            message.add_nodeids("bad");
        });

        send([&](protocol::TMGetLedger& message) {
            message.set_itype(protocol::liBASE);
            message.set_ledgerhash(bytes(hash));
            message.set_querydepth(1);
        });

        send(
            [&](protocol::TMGetLedger& message) {
                message.set_itype(protocol::liTX_NODE);
                message.set_ledgerhash(bytes(hash));
                message.add_nodeids(nodeID.getRawString());
                message.set_querytype(protocol::qtINDIRECT);
            },
            true);
    }

    void
    testLedgerDataAndObjectRequests()
    {
        testcase("ledger data and object requests");
        {
            jtx::Env env(*this, config());
            auto peer = createPeer(env);
            auto message = std::make_shared<protocol::TMLedgerData>();
            message->set_ledgerhash("bad");
            message->set_ledgerseq(1);
            message->set_type(protocol::liAS_NODE);
            message->add_nodes()->set_nodedata("node");
            peer->onMessage(message);
        }

        {
            jtx::Env env(*this, config());
            auto peer = createPeer(env);
            auto const ledgerHash = digest("ledgerdata-candidate");
            auto message = std::make_shared<protocol::TMLedgerData>();
            message->set_ledgerhash(bytes(ledgerHash));
            message->set_ledgerseq(1);
            message->set_type(protocol::liTS_CANDIDATE);
            message->add_nodes()->set_nodedata("node");
            peer->onMessage(message);
        }

        {
            jtx::Env env(*this, config());
            auto peer = createPeer(env);
            auto const ledgerHash = digest("ledgerdata-empty");
            auto message = std::make_shared<protocol::TMLedgerData>();
            message->set_ledgerhash(bytes(ledgerHash));
            message->set_ledgerseq(1);
            message->set_type(protocol::liAS_NODE);
            peer->onMessage(message);
        }

        {
            jtx::Env env(*this, config());
            auto target = createPeer(env);
            auto sender = createPeer(env);
            auto const ledgerHash = digest("ledgerdata-relay");
            auto relayed = std::make_shared<protocol::TMLedgerData>();
            relayed->set_ledgerhash(bytes(ledgerHash));
            relayed->set_ledgerseq(1);
            relayed->set_type(protocol::liAS_NODE);
            relayed->set_requestcookie(target->id());
            relayed->add_nodes()->set_nodedata("node");
            sender->onMessage(relayed);

            BEAST_EXPECT(target->sent().size() == 1);
            auto relayedPacket = parse<protocol::TMLedgerData>(target->sent().back());
            BEAST_EXPECT(relayedPacket.ledgerhash() == bytes(ledgerHash));
            BEAST_EXPECT(!relayedPacket.has_requestcookie());
        }

        {
            jtx::Env env(*this, config());
            auto peer = createPeer(env);
            auto fetchPack = std::make_shared<protocol::TMGetObjectByHash>();
            fetchPack->set_type(protocol::TMGetObjectByHash::otFETCH_PACK);
            fetchPack->set_query(true);
            fetchPack->set_ledgerhash("bad");
            peer->onMessage(fetchPack);
        }

        {
            jtx::Env env(*this, config());
            auto peer = createPeer(env);
            auto txQuery = std::make_shared<protocol::TMGetObjectByHash>();
            txQuery->set_type(protocol::TMGetObjectByHash::otTRANSACTIONS);
            txQuery->set_query(true);
            txQuery->add_objects()->set_hash("bad");
            peer->onMessage(txQuery);
            env.app().getJobQueue().rendezvous();
        }

        {
            jtx::Env env(*this, config());
            auto peer = createPeer(env);
            auto fetchPackReply = std::make_shared<protocol::TMGetObjectByHash>();
            fetchPackReply->set_type(protocol::TMGetObjectByHash::otFETCH_PACK);
            fetchPackReply->set_query(false);
            auto* object = fetchPackReply->add_objects();
            object->set_hash(bytes(digest("fetch-pack")));
            object->set_ledgerseq(1);
            object->set_data("blob");
            peer->onMessage(fetchPackReply);
        }
    }

    void
    testValidationProposalAndSquelchHandlers()
    {
        testcase("validation proposal and squelch handlers");

        using namespace std::chrono_literals;

        {
            jtx::Env env(*this, config());
            auto peer = createPeer(env);
            auto tooSmall = std::make_shared<protocol::TMValidation>();
            tooSmall->set_validation("small");
            peer->onMessage(tooSmall);
        }

        {
            jtx::Env env(*this, config());
            auto peer = createPeer(env);
            auto malformed = std::make_shared<protocol::TMValidation>();
            malformed->set_validation(std::string(60, 'x'));
            peer->onMessage(malformed);
        }

        {
            jtx::Env env(*this, config());
            auto current =
                std::make_shared<protocol::TMValidation>(makeValidationMessage(env, "current"));
            auto first = createPeer(env);
            auto second = createPeer(env);
            first->onMessage(current);
            env.app().getJobQueue().rendezvous();
            second->onMessage(current);
            env.app().getJobQueue().rendezvous();
        }

        {
            jtx::Env env(*this, config());
            auto peer = createPeer(env);
            peer->checkTracking(1, 1000);
            auto diverged =
                std::make_shared<protocol::TMValidation>(makeValidationMessage(env, "diverged"));
            peer->onMessage(diverged);
            env.app().getJobQueue().rendezvous();
        }

        {
            jtx::Env env(*this, config());
            auto peer = createPeer(env);
            auto stale =
                std::make_shared<protocol::TMValidation>(makeValidationMessage(env, "stale", -24h));
            peer->onMessage(stale);
        }

        {
            jtx::Env env(*this, config());
            auto peer = createPeer(env);
            auto proposal = std::make_shared<protocol::TMProposeSet>();
            proposal->set_proposeseq(1);
            proposal->set_currenttxhash(bytes(digest("current")));
            proposal->set_nodepubkey("node");
            proposal->set_closetime(1);
            proposal->set_signature("sig");
            proposal->set_previousledger(bytes(digest("previous")));
            peer->onMessage(proposal);
        }

        {
            jtx::Env env(*this, config());
            auto peer = createPeer(env);
            auto invalidTxSet = std::make_shared<protocol::TMHaveTransactionSet>();
            invalidTxSet->set_status(protocol::tsHAVE);
            invalidTxSet->set_hash("bad");
            peer->onMessage(invalidTxSet);
        }

        {
            jtx::Env env(*this, config());
            auto peer = createPeer(env);
            auto validTxSet = std::make_shared<protocol::TMHaveTransactionSet>();
            validTxSet->set_status(protocol::tsHAVE);
            validTxSet->set_hash(bytes(digest("txset")));
            peer->onMessage(validTxSet);
            peer->onMessage(validTxSet);
        }

        {
            jtx::Env env(*this, config());
            auto peer = createPeer(env);
            auto missingPubkey = std::make_shared<protocol::TMSquelch>();
            missingPubkey->set_squelch(true);
            peer->onMessage(missingPubkey);
            pump(env.app().getIOContext());

            auto invalidPubkey = std::make_shared<protocol::TMSquelch>();
            invalidPubkey->set_squelch(true);
            invalidPubkey->set_validatorpubkey("bad");
            peer->onMessage(invalidPubkey);
            pump(env.app().getIOContext());
        }
    }

    void
    run() override
    {
        testIdentityAndFeatures();
        testLedgerState();
        testPingAndTxQueue();
        testProtocolMessages();
        testFeatureAndMessageGuards();
        testLedgerRequestValidation();
        testLedgerDataAndObjectRequests();
        testValidationProposalAndSquelchHandlers();
    }
};

BEAST_DEFINE_TESTSUITE(PeerImp, overlay, xrpl);

}  // namespace xrpl::test
