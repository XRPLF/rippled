#include <test/jtx/Env.h>
#include <test/overlay/CapturePeer.h>

#include <xrpld/app/consensus/RCLCxPeerPos.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/detail/OverlayImpl.h>
#include <xrpld/overlay/detail/PeerImp.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/core/HashRouter.h>
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
#include <xrpl/server/Manifest.h>

#include <xrpl.pb.h>

#include <memory>
#include <string>
#include <unordered_set>

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
            auto const peer = createPeer(env);

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
