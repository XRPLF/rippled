#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>
#include <test/overlay/CapturePeer.h>

#include <xrpld/app/consensus/RCLCxPeerPos.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/core/Config.h>
#include <xrpld/overlay/detail/OverlayImpl.h>
#include <xrpld/overlay/detail/PeerImp.h>
#include <xrpld/overlay/detail/ProtocolVersion.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base64.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/config/Constants.h>
#include <xrpl/core/HashRouter.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STValidation.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/Sign.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/server/Manifest.h>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/socket_base.hpp>

#include <xrpl.pb.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace xrpl::test {

// Proposals, validations and manifests that carry a Dilithium key are dropped while featureQuantum
// is disabled in the validated ledger.
class QuantumPeerGate_test : public beast::unit_test::Suite
{
    static std::shared_ptr<CapturePeer>
    createPeer(jtx::Env& env)
    {
        return makeCapturePeer(env, randomKeyPair(KeyType::Secp256k1).first);
    }

    // A serialized manifest of `master`, signed by `signing` unless it is a revocation.
    static std::string
    manifestString(
        std::pair<PublicKey, SecretKey> const& master,
        std::optional<std::pair<PublicKey, SecretKey>> const& signing,
        std::uint32_t sequence)
    {
        auto const masterType = publicKeyType(master.first);
        if (!masterType)
            Throw<std::invalid_argument>("manifestString: unknown master key type");
        STObject st(sfGeneric);
        st[sfSequence] = sequence;
        st[sfPublicKey] = master.first;
        if (signing)
        {
            auto const signingType = publicKeyType(signing->first);
            if (!signingType)
                Throw<std::invalid_argument>("manifestString: unknown signing key type");
            st[sfSigningPubKey] = signing->first;
            sign(st, HashPrefix::Manifest, *signingType, signing->second);
        }
        sign(st, HashPrefix::Manifest, *masterType, master.second, sfMasterSignature);
        Serializer s;
        st.add(s);
        return std::string(static_cast<char const*>(s.data()), s.size());
    }

    static FeatureBitset
    features(bool quantum)
    {
        auto const all = jtx::testableAmendments();
        return quantum ? all : all - featureQuantum;
    }

    // true when the HashRouter already held `key`, i.e. the handler reached its suppression step
    static bool
    suppressed(jtx::Env& env, UInt256 const& key)
    {
        return !env.app().getHashRouter().addSuppressionPeer(key, 9999);
    }

    void
    testProposalShape()
    {
        testcase("Proposal key and signature shape");
        std::unordered_set<UInt256, beast::Uhash<>> const none;
        std::unordered_set<UInt256, beast::Uhash<>> const quantumSet{featureQuantum};
        Rules const off{none};
        Rules const on{quantumSet};

        auto const secp = randomKeyPair(KeyType::Secp256k1).first;
        auto const dil = randomKeyPair(KeyType::Dilithium).first;
        auto const ed = randomKeyPair(KeyType::Ed25519).first;
        std::string const s64(64, 'x'), s72(72, 'x'), s73(73, 'x'), s2420(2420, 'x');

        BEAST_EXPECT(xrpl::detail::proposalKeyAndSignatureValid(secp.slice(), makeSlice(s64), off));
        BEAST_EXPECT(xrpl::detail::proposalKeyAndSignatureValid(secp.slice(), makeSlice(s72), off));
        BEAST_EXPECT(
            !xrpl::detail::proposalKeyAndSignatureValid(secp.slice(), makeSlice(s73), off));
        BEAST_EXPECT(xrpl::detail::proposalKeyAndSignatureValid(dil.slice(), makeSlice(s2420), on));
        BEAST_EXPECT(
            !xrpl::detail::proposalKeyAndSignatureValid(dil.slice(), makeSlice(s2420), off));
        BEAST_EXPECT(!xrpl::detail::proposalKeyAndSignatureValid(ed.slice(), makeSlice(s64), on));
    }

    void
    testProposal()
    {
        testcase("Dilithium proposal");
        for (bool const quantum : {false, true})
        {
            jtx::Env env{*this, features(quantum)};
            auto const peer = createPeer(env);

            auto const keys = randomKeyPair(KeyType::Dilithium);
            UInt256 const proposeHash{1};
            UInt256 const prevLedger{2};
            NetClock::time_point const closeTime = env.app().getTimeKeeper().closeTime();
            std::string const sig(2420, 'x');

            auto const m = std::make_shared<protocol::TMProposeSet>();
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
            auto const peer = createPeer(env);

            auto const keys = randomKeyPair(KeyType::Dilithium);
            auto const val = std::make_shared<STValidation>(
                env.app().getTimeKeeper().closeTime(),
                keys.first,
                keys.second,
                calcNodeID(keys.first),
                [](STValidation& v) {
                    v.setFieldU32(sfLedgerSequence, 3);
                    v.setFieldH256(sfLedgerHash, UInt256{3});
                });
            Serializer s;
            val->add(s);

            auto const m = std::make_shared<protocol::TMValidation>();
            m->set_validation(s.data(), s.size());
            peer->onMessage(m);

            BEAST_EXPECT(suppressed(env, sha512Half(s.slice())) == quantum);
        }
    }

    void
    testManifests()
    {
        testcase("Manifests with Dilithium keys");
        for (bool const quantum : {false, true})
        {
            jtx::Env env{*this, features(quantum)};
            auto const peer = createPeer(env);
            auto& overlay = dynamic_cast<OverlayImpl&>(env.app().getOverlay());
            auto const& manifests = env.app().getValidatorManifests();

            auto const send = [&](std::string const& manifest) {
                auto const m = std::make_shared<protocol::TMManifests>();
                m->add_list()->set_stobject(manifest);
                overlay.onManifests(m, peer);
            };

            // a secp256k1 master key with a Dilithium signing key
            auto const secpMaster = randomKeyPair(KeyType::Secp256k1);
            auto const dilSigning = randomKeyPair(KeyType::Dilithium);
            send(manifestString(secpMaster, dilSigning, 1));
            BEAST_EXPECT(
                (manifests.getSigningKey(secpMaster.first) == dilSigning.first) == quantum);

            // a Dilithium master key with a secp256k1 signing key
            auto const dilMaster = randomKeyPair(KeyType::Dilithium);
            auto const secpSigning = randomKeyPair(KeyType::Secp256k1);
            send(manifestString(dilMaster, secpSigning, 1));
            BEAST_EXPECT(
                (manifests.getSigningKey(dilMaster.first) == secpSigning.first) == quantum);

            // a revocation carries no signing key and needs no amendment
            auto const revokedMaster = randomKeyPair(KeyType::Secp256k1);
            send(manifestString(
                revokedMaster, std::nullopt, std::numeric_limits<std::uint32_t>::max()));
            BEAST_EXPECT(manifests.revoked(revokedMaster.first));
        }
    }

    void
    testValidatorListCollection()
    {
        testcase("Validator list signed by a Dilithium publisher key");
        using namespace std::chrono_literals;
        for (bool const quantum : {false, true})
        {
            auto const publisher = randomKeyPair(KeyType::Secp256k1);
            auto const publisherSigning = randomKeyPair(KeyType::Dilithium);
            auto const validator = randomKeyPair(KeyType::Secp256k1).first;
            jtx::Env env{
                *this,
                jtx::envconfig([&publisher](std::unique_ptr<Config> cfg) {
                    cfg->section(Sections::kValidatorListKeys).append(strHex(publisher.first));
                    return cfg;
                }),
                features(quantum)};
            auto const peer = createPeer(env);

            auto const expiration = env.timeKeeper().now() + 3600s;
            std::string const data = R"({"sequence":1,"expiration":)" +
                std::to_string(expiration.time_since_epoch().count()) +
                R"(,"validators":[{"validation_public_key":")" + strHex(validator) + R"("}]})";
            auto const sig = sign(publisherSigning.first, publisherSigning.second, makeSlice(data));

            auto const m = std::make_shared<protocol::TMValidatorListCollection>();
            m->set_version(2);
            m->set_manifest(base64Encode(manifestString(publisher, publisherSigning, 1)));
            auto* const blob = m->add_blobs();
            blob->set_blob(base64Encode(data));
            blob->set_signature(strHex(sig));
            peer->onMessage(m);

            BEAST_EXPECT(env.app().getValidators().listed(validator) == quantum);
        }
    }

    void
    testSocketBuffers()
    {
        testcase("Peer raises its socket buffers");
        jtx::Env env{*this};
        auto& overlay = dynamic_cast<OverlayImpl&>(env.app().getOverlay());

        CapturePeer::SocketType socket(env.app().getIOContext());
        socket.open(boost::asio::ip::tcp::v4());
        boost::asio::socket_base::receive_buffer_size receiveBefore;
        boost::asio::socket_base::send_buffer_size sendBefore;
        socket.get_option(receiveBefore);
        socket.get_option(sendBefore);

        auto stream = std::make_unique<CapturePeer::StreamType>(
            std::move(socket), detail::capturePeerSslContext());
        auto const& opened = stream->next_layer().socket();
        beast::ip::Endpoint const local(
            boost::asio::ip::make_address("172.1.1.1"), detail::kCapturePeerPort);
        auto const remote = detail::nextCapturePeerRemote();
        auto const consumer = overlay.resourceManager().newInboundEndpoint(remote);
        auto [slot, _] = overlay.peerFinder().newInboundSlot(local, remote);
        if (!BEAST_EXPECT(slot))
            return;
        auto const peer = std::make_shared<CapturePeer>(
            env.app(),
            detail::nextCapturePeerId(),
            slot,
            HttpRequestType{},
            randomKeyPair(KeyType::Secp256k1).first,
            newestSupportedProtocolVersion(),
            consumer,
            std::move(stream),
            overlay);
        overlay.addActive(peer);

        boost::asio::socket_base::receive_buffer_size receiveAfter;
        boost::asio::socket_base::send_buffer_size sendAfter;
        opened.get_option(receiveAfter);
        opened.get_option(sendAfter);
        BEAST_EXPECT(receiveAfter.value() >= receiveBefore.value());
        BEAST_EXPECT(sendAfter.value() >= sendBefore.value());
    }

public:
    void
    run() override
    {
        testProposalShape();
        testProposal();
        testValidation();
        testManifests();
        testValidatorListCollection();
        testSocketBuffers();
    }
};

BEAST_DEFINE_TESTSUITE(QuantumPeerGate, overlay, xrpl);

}  // namespace xrpl::test
