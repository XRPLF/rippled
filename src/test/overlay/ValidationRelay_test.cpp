#include <test/jtx/Env.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Message.h>
#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/detail/OverlayImpl.h>
#include <xrpld/overlay/detail/PeerImp.h>
#include <xrpld/overlay/detail/ProtocolVersion.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/make_SSLContext.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/core/HashRouter.h>
#include <xrpl/peerfinder/Slot.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STValidation.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/resource/Charge.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/resource/Fees.h>
#include <xrpl/server/Handoff.h>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>

#include <xrpl.pb.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <ranges>
#include <utility>
#include <vector>

namespace xrpl::test {

using namespace jtx;

// Drives PeerImp::onMessage(TMValidation) directly to prove that a validation
// delivered in a non-canonical wire encoding (fields reordered) is rejected:
// the peer is charged a fee and the message is dropped before its suppression
// key is registered. Accepting such encodings would let a malicious relay
// bypass duplicate detection by reordering the fields of a valid validation.
class ValidationRelay_test : public beast::unit_test::Suite
{
    using MiddleType = boost::beast::tcp_stream;
    using StreamType = boost::beast::ssl_stream<MiddleType>;
    using SocketType = boost::asio::ip::tcp::socket;
    using SharedContext = std::shared_ptr<boost::asio::ssl::context>;

    class PeerTest : public PeerImp
    {
    public:
        struct Params
        {
            Application& app;
            std::shared_ptr<peer_finder::Slot> slot;
            HttpRequestType request;
            PublicKey publicKey;
            ProtocolVersion protocol;
            resource::Consumer consumer;
            std::unique_ptr<ValidationRelay_test::StreamType> streamPtr;
            OverlayImpl& overlay;
        };

        explicit PeerTest(Params params)
            : PeerImp(
                  params.app,
                  id++,
                  params.slot,
                  std::move(params.request),
                  params.publicKey,
                  params.protocol,
                  params.consumer,
                  std::move(params.streamPtr),
                  params.overlay)
        {
        }

        ~PeerTest() override = default;

        void
        run() override
        {
        }

        void
        send(std::shared_ptr<Message> const&) override
        {
        }

        static void
        resetId()
        {
            id = 0;
        }

        // Exposes the protected fee accumulator (PeerImp::currentFeeCharge) so
        // the test can assert the punitive charge applied when a validation is
        // delivered in non-canonical form.
        resource::Charge
        feeCharge() const
        {
            return currentFeeCharge();
        }

    private:
        inline static Peer::ID id = 0;
    };

    SharedContext context_{makeSslContext("")};
    ProtocolVersion protocolVersion_{1, 7};

    std::shared_ptr<PeerTest>
    createPeer(jtx::Env& env)
    {
        auto& overlay = safeDowncast<OverlayImpl&>(env.app().getOverlay());
        boost::beast::http::request<boost::beast::http::dynamic_body> request;
        auto streamPtr =
            std::make_unique<StreamType>(SocketType(env.app().getIOContext()), *context_);

        beast::ip::Endpoint const local(boost::asio::ip::make_address("172.1.1.1"), 51235);
        beast::ip::Endpoint const remote(boost::asio::ip::make_address("172.1.1.2"), 51235);

        PublicKey const key(std::get<0>(randomKeyPair(KeyType::Ed25519)));
        auto const consumer = overlay.resourceManager().newInboundEndpoint(remote);
        auto [slot, _] = overlay.peerFinder().newInboundSlot(local, remote);

        auto peer = std::make_shared<PeerTest>(PeerTest::Params{
            .app = env.app(),
            .slot = slot,
            .request = std::move(request),
            .publicKey = key,
            .protocol = protocolVersion_,
            .consumer = consumer,
            .streamPtr = std::move(streamPtr),
            .overlay = overlay,
        });

        overlay.addActive(peer);
        return peer;
    }

    // Build a signed validation whose sign time is current relative to the
    // environment, so that PeerImp::onMessage passes the isCurrent() gate.
    static std::shared_ptr<STValidation>
    makeValidation(
        jtx::Env& env,
        std::uint32_t ledgerSeq,
        uint256 const& ledgerHash,
        uint256 const& consensusHash)
    {
        auto const keys = randomKeyPair(KeyType::Secp256k1);
        return std::make_shared<STValidation>(
            env.app().getTimeKeeper().closeTime(),
            keys.first,
            keys.second,
            calcNodeID(keys.first),
            [&](STValidation& v) {
                v.setFieldH256(sfLedgerHash, ledgerHash);
                v.setFieldU32(sfLedgerSequence, ledgerSeq);
                v.setFieldH256(sfConsensusHash, consensusHash);
                v.setFlag(kVfFullValidation);
            });
    }

    // Re-encode a validation with its fields emitted in reverse order. A
    // malicious relay can do this without invalidating the signature, which is
    // computed over the canonically sorted fields.
    static Blob
    reorderFields(STValidation const& val)
    {
        std::vector<STBase const*> fields;
        for (std::size_t i = 0; i < val.getCount(); ++i)
        {
            STBase const& f = val.peekAtIndex(i);
            if (f.getSType() != STI_NOTPRESENT && f.getFName().shouldInclude(true))
                fields.push_back(&f);
        }

        Serializer s;
        for (STBase const* f : std::views::reverse(fields))
        {
            f->addFieldID(s);
            f->add(s);
        }
        return s.peekData();
    }

    static std::shared_ptr<protocol::TMValidation>
    makeMessage(Blob const& blob)
    {
        auto m = std::make_shared<protocol::TMValidation>();
        m->set_validation(blob.data(), blob.size());
        return m;
    }

    void
    testCanonicalValidationAccepted()
    {
        testcase("Canonical validation is accepted without penalty");

        Env env{*this};
        PeerTest::resetId();
        auto const peer = createPeer(env);

        // An honest, already-canonical validation must pass the canonical-form
        // check, incur no punitive fee, and reach the suppression stage. This
        // positive control confirms the harness drives onMessage past its
        // early-return gates.
        auto const val = makeValidation(env, 1, uint256{1}, uint256{2});
        Blob const canonical = val->getSerialized();
        uint256 const canonicalKey = sha512Half(makeSlice(canonical));

        peer->onMessage(makeMessage(canonical));

        // No punitive charge: the fee remains at the default trivial level.
        BEAST_EXPECT(peer->feeCharge() == resource::kFeeTrivialPeer);

        // A probe peer id distinct from the one used by onMessage; only the
        // presence of the key entry matters for the returned status.
        Peer::ID constexpr kProbe = 0xFFFF;

        // The handler registered the key, so this probe finds it already
        // present (addSuppressionPeer returns false).
        BEAST_EXPECT(!env.app().getHashRouter().addSuppressionPeer(canonicalKey, kProbe));
    }

    void
    testNonCanonicalValidationCharged()
    {
        testcase("Non-canonical validation is charged and dropped");

        Env env{*this};
        PeerTest::resetId();
        auto const peer = createPeer(env);

        // A fresh validation delivered with its fields reordered on the wire.
        // The signature is still valid (it is computed over the canonically
        // sorted fields), but the encoding itself is not canonical.
        auto const val = makeValidation(env, 2, uint256{3}, uint256{4});
        Blob const canonical = val->getSerialized();
        Blob const reordered = reorderFields(*val);
        uint256 const canonicalKey = sha512Half(makeSlice(canonical));
        uint256 const reorderedKey = sha512Half(makeSlice(reordered));

        // Sanity: the reordered encoding is genuinely different bytes.
        BEAST_EXPECT(reordered != canonical);
        BEAST_EXPECT(reorderedKey != canonicalKey);

        peer->onMessage(makeMessage(reordered));

        // The peer is charged the invalid-data fee for the non-canonical
        // encoding.
        BEAST_EXPECT(peer->feeCharge() == resource::kFeeInvalidData);

        // A probe peer id distinct from the one used by onMessage; only the
        // presence of the key entry matters for the returned status.
        Peer::ID constexpr kProbe = 0xFFFF;

        // The handler returned before registering any suppression key, so
        // neither the canonical nor the reordered key was recorded: both probes
        // create a new entry (addSuppressionPeer returns true).
        BEAST_EXPECT(env.app().getHashRouter().addSuppressionPeer(canonicalKey, kProbe));
        BEAST_EXPECT(env.app().getHashRouter().addSuppressionPeer(reorderedKey, kProbe));
    }

    void
    run() override
    {
        testCanonicalValidationAccepted();
        testNonCanonicalValidationCharged();
    }
};

BEAST_DEFINE_TESTSUITE(ValidationRelay, overlay, xrpl);

}  // namespace xrpl::test
