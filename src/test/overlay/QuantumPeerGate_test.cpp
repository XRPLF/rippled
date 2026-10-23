#include <test/jtx/Env.h>

#include <xrpld/app/consensus/RCLCxPeerPos.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/detail/OverlayImpl.h>
#include <xrpld/overlay/detail/PeerImp.h>
#include <xrpld/overlay/detail/ProtocolVersion.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/make_SSLContext.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/core/HashRouter.h>
#include <xrpl/peerfinder/Slot.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STValidation.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/Sign.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/server/Manifest.h>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>

#include <xrpl.pb.h>

#include <memory>
#include <string>
#include <unordered_set>
#include <utility>

namespace xrpl::test {

// Proposals, validations and manifests that carry a Dilithium key are dropped while featureQuantum
// is disabled in the validated ledger.
class QuantumPeerGate_test : public beast::unit_test::Suite
{
    using middle_type = boost::beast::tcp_stream;
    using stream_type = boost::beast::ssl_stream<middle_type>;
    using socket_type = boost::asio::ip::tcp::socket;

    class PeerTest : public PeerImp
    {
    public:
        PeerTest(
            Application& app,
            std::shared_ptr<peer_finder::Slot> const& slot,
            http_request_type&& request,
            PublicKey const& publicKey,
            ProtocolVersion protocol,
            resource::Consumer consumer,
            std::unique_ptr<QuantumPeerGate_test::stream_type>&& streamPtr,
            OverlayImpl& overlay)
            : PeerImp(
                  app,
                  id++,
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
        send(std::shared_ptr<Message> const&) override
        {
        }

    private:
        inline static Peer::id_t id = 0;
    };

    std::shared_ptr<boost::asio::ssl::context> context_{makeSslContext("")};

    std::shared_ptr<PeerTest>
    createPeer(jtx::Env& env)
    {
        auto& overlay = dynamic_cast<OverlayImpl&>(env.app().getOverlay());
        boost::beast::http::request<boost::beast::http::dynamic_body> request;
        auto streamPtr =
            std::make_unique<stream_type>(socket_type(env.app().getIOContext()), *context_);
        beast::ip::Endpoint const local(boost::asio::ip::make_address("172.1.1.1"), 51235);
        beast::ip::Endpoint const remote(boost::asio::ip::make_address("172.1.1.2"), 51235);
        PublicKey const key(randomKeyPair(KeyType::Secp256k1).first);
        auto consumer = overlay.resourceManager().newInboundEndpoint(remote);
        auto [slot, _] = overlay.peerFinder().newInboundSlot(local, remote);
        auto peer = std::make_shared<PeerTest>(
            env.app(),
            slot,
            std::move(request),
            key,
            ProtocolVersion{1, 7},
            consumer,
            std::move(streamPtr),
            overlay);
        overlay.addActive(peer);
        return peer;
    }

    static FeatureBitset
    features(bool quantum)
    {
        auto const all = jtx::testableAmendments();
        return quantum ? all : all - featureQuantum;
    }

    // true when the HashRouter already held `key`, i.e. the handler reached its suppression step
    static bool
    suppressed(jtx::Env& env, uint256 const& key)
    {
        return !env.app().getHashRouter().addSuppressionPeer(key, 9999);
    }

    void
    testProposalShape()
    {
        testcase("Proposal key and signature shape");
        std::unordered_set<uint256, beast::Uhash<>> const none;
        std::unordered_set<uint256, beast::Uhash<>> const quantumSet{featureQuantum};
        Rules const off{none};
        Rules const on{quantumSet};

        auto const secp = randomKeyPair(KeyType::Secp256k1).first;
        auto const dil = randomKeyPair(KeyType::Dilithium).first;
        auto const ed = randomKeyPair(KeyType::Ed25519).first;
        std::string const s64(64, 'x'), s72(72, 'x'), s73(73, 'x'), s2420(2420, 'x');

        BEAST_EXPECT(detail::proposalKeyAndSignatureValid(secp.slice(), makeSlice(s64), off));
        BEAST_EXPECT(detail::proposalKeyAndSignatureValid(secp.slice(), makeSlice(s72), off));
        BEAST_EXPECT(!detail::proposalKeyAndSignatureValid(secp.slice(), makeSlice(s73), off));
        BEAST_EXPECT(detail::proposalKeyAndSignatureValid(dil.slice(), makeSlice(s2420), on));
        BEAST_EXPECT(!detail::proposalKeyAndSignatureValid(dil.slice(), makeSlice(s2420), off));
        BEAST_EXPECT(!detail::proposalKeyAndSignatureValid(ed.slice(), makeSlice(s64), on));
    }

    void
    testProposal()
    {
        testcase("Dilithium proposal");
        for (bool const quantum : {false, true})
        {
            jtx::Env env{*this, features(quantum)};
            auto peer = createPeer(env);

            auto const keys = randomKeyPair(KeyType::Dilithium);
            uint256 const proposeHash{1};
            uint256 const prevLedger{2};
            NetClock::time_point const closeTime = env.app().getTimeKeeper().closeTime();
            std::string const sig(2420, 'x');

            auto m = std::make_shared<protocol::TMProposeSet>();
            m->set_proposeseq(0);
            m->set_currenttxhash(proposeHash.data(), proposeHash.size());
            m->set_previousledger(prevLedger.data(), prevLedger.size());
            m->set_nodepubkey(keys.first.data(), keys.first.size());
            m->set_closetime(closeTime.time_since_epoch().count());
            m->set_signature(sig);
            peer->onMessage(m);

            auto const key = proposalUniqueId(
                proposeHash, prevLedger, 0, closeTime, keys.first.slice(), makeSlice(sig));
            BEAST_EXPECT(suppressed(env, key) == quantum);
        }
    }

    void
    testValidation()
    {
        testcase("Dilithium validation");
        for (bool const quantum : {false, true})
        {
            jtx::Env env{*this, features(quantum)};
            auto peer = createPeer(env);

            auto const keys = randomKeyPair(KeyType::Dilithium);
            auto const val = std::make_shared<STValidation>(
                env.app().getTimeKeeper().closeTime(),
                keys.first,
                keys.second,
                calcNodeID(keys.first),
                [](STValidation& v) {
                    v.setFieldU32(sfLedgerSequence, 3);
                    v.setFieldH256(sfLedgerHash, uint256{3});
                });
            Serializer s;
            val->add(s);

            auto m = std::make_shared<protocol::TMValidation>();
            m->set_validation(s.data(), s.size());
            peer->onMessage(m);

            BEAST_EXPECT(suppressed(env, sha512Half(s.slice())) == quantum);
        }
    }

    void
    testManifests()
    {
        testcase("Manifest with a Dilithium signing key");
        for (bool const quantum : {false, true})
        {
            jtx::Env env{*this, features(quantum)};
            auto peer = createPeer(env);

            auto const master = randomKeyPair(KeyType::Secp256k1);
            auto const signing = randomKeyPair(KeyType::Dilithium);
            STObject st(sfGeneric);
            st[sfSequence] = 1;
            st[sfPublicKey] = master.first;
            st[sfSigningPubKey] = signing.first;
            sign(st, HashPrefix::Manifest, KeyType::Dilithium, signing.second);
            sign(st, HashPrefix::Manifest, KeyType::Secp256k1, master.second, sfMasterSignature);
            Serializer s;
            st.add(s);

            auto m = std::make_shared<protocol::TMManifests>();
            m->add_list()->set_stobject(s.data(), s.size());
            dynamic_cast<OverlayImpl&>(env.app().getOverlay()).onManifests(m, peer);

            BEAST_EXPECT(
                (env.app().getValidatorManifests().getSigningKey(master.first) == signing.first) ==
                quantum);
        }
    }

public:
    void
    run() override
    {
        testProposalShape();
        testProposal();
        testValidation();
        testManifests();
    }
};

BEAST_DEFINE_TESTSUITE(QuantumPeerGate, overlay, xrpl);

}  // namespace xrpl::test
