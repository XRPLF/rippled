#include <xrpld/app/consensus/RCLCxPeerPos.h>
#include <xrpld/consensus/ConsensusProposal.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Sign.h>

namespace xrpl {
namespace test {

class RCLCxPeerPos_test : public beast::unit_test::suite
{
    void
    testDilithiumProposalSignature()
    {
        testcase("Dilithium proposal signature verification");

        // Generate dilithium keypair
        auto const [publicKey, secretKey] = randomKeyPair(KeyType::dilithium);

        // Verify key type
        BEAST_EXPECT(*publicKeyType(publicKey) == KeyType::dilithium);

        // Create a proposal with test data
        uint256 const prevLedger{42};
        uint256 const position{100};
        std::uint32_t const proposeSeq = 5;
        auto const closeTime = NetClock::time_point{NetClock::duration{123456}};
        auto const now = NetClock::time_point{NetClock::duration{123456}};
        NodeID const nodeID = calcNodeID(publicKey);

        RCLCxPeerPos::Proposal proposal{
            prevLedger, proposeSeq, position, closeTime, now, nodeID};

        // Calculate signing hash (same as ConsensusProposal::signingHash())
        uint256 const signingHash = sha512Half(
            HashPrefix::proposal,
            std::uint32_t(proposal.proposeSeq()),
            proposal.closeTime().time_since_epoch().count(),
            proposal.prevLedger(),
            proposal.position());

        // Sign the proposal
        auto signature = signDigest(publicKey, secretKey, signingHash);

        BEAST_EXPECT(signature.size() > 0);
        BEAST_EXPECT(signature.size() <= 2420);  // Dilithium sig max size

        // Create unique suppression ID
        Slice const sigSlice{signature.data(), signature.size()};
        uint256 const suppression = proposalUniqueId(
            proposal.position(),
            proposal.prevLedger(),
            proposal.proposeSeq(),
            proposal.closeTime(),
            publicKey.slice(),
            sigSlice);

        // Construct RCLCxPeerPos with the signature
        RCLCxPeerPos peerPos{
            publicKey, sigSlice, suppression, std::move(proposal)};

        // Verify the signature
        BEAST_EXPECT(peerPos.checkSign());
    }

    void
    testInvalidDilithiumSignature()
    {
        testcase("Invalid dilithium proposal signature");

        // Generate dilithium keypair
        auto const [publicKey, secretKey] = randomKeyPair(KeyType::dilithium);

        // Create a proposal
        uint256 const prevLedger{42};
        uint256 const position{100};
        std::uint32_t const proposeSeq = 5;
        auto const closeTime = NetClock::time_point{NetClock::duration{123456}};
        auto const now = NetClock::time_point{NetClock::duration{123456}};
        NodeID const nodeID = calcNodeID(publicKey);

        RCLCxPeerPos::Proposal proposal{
            prevLedger, proposeSeq, position, closeTime, now, nodeID};

        // Calculate signing hash
        uint256 const signingHash = sha512Half(
            HashPrefix::proposal,
            std::uint32_t(proposal.proposeSeq()),
            proposal.closeTime().time_since_epoch().count(),
            proposal.prevLedger(),
            proposal.position());

        // Sign the proposal
        auto signature = signDigest(publicKey, secretKey, signingHash);

        // Corrupt the signature
        if (signature.size() > 10)
        {
            *const_cast<uint8_t*>(signature.data() + 10) ^= 0xFF;  // Flip bits
        }

        // Create unique suppression ID
        Slice const sigSlice{signature.data(), signature.size()};
        uint256 const suppression = proposalUniqueId(
            proposal.position(),
            proposal.prevLedger(),
            proposal.proposeSeq(),
            proposal.closeTime(),
            publicKey.slice(),
            sigSlice);

        // Construct RCLCxPeerPos with the corrupted signature
        RCLCxPeerPos peerPos{
            publicKey, sigSlice, suppression, std::move(proposal)};

        // Verify the signature should fail
        BEAST_EXPECT(!peerPos.checkSign());
    }

    void
    testWrongKeyProposalSignature()
    {
        testcase("Proposal signed with wrong key");

        // Generate two different dilithium keypairs
        auto const [publicKey1, secretKey1] =
            randomKeyPair(KeyType::dilithium);
        auto const [publicKey2, secretKey2] =
            randomKeyPair(KeyType::dilithium);

        // Create a proposal
        uint256 const prevLedger{42};
        uint256 const position{100};
        std::uint32_t const proposeSeq = 5;
        auto const closeTime = NetClock::time_point{NetClock::duration{123456}};
        auto const now = NetClock::time_point{NetClock::duration{123456}};
        NodeID const nodeID = calcNodeID(publicKey1);

        RCLCxPeerPos::Proposal proposal{
            prevLedger, proposeSeq, position, closeTime, now, nodeID};

        // Calculate signing hash
        uint256 const signingHash = sha512Half(
            HashPrefix::proposal,
            std::uint32_t(proposal.proposeSeq()),
            proposal.closeTime().time_since_epoch().count(),
            proposal.prevLedger(),
            proposal.position());

        // Sign with the WRONG secret key
        auto signature = signDigest(publicKey2, secretKey2, signingHash);

        // Create unique suppression ID
        Slice const sigSlice{signature.data(), signature.size()};
        uint256 const suppression = proposalUniqueId(
            proposal.position(),
            proposal.prevLedger(),
            proposal.proposeSeq(),
            proposal.closeTime(),
            publicKey1.slice(),  // Using publicKey1
            sigSlice);

        // Construct RCLCxPeerPos with publicKey1 but signature from secretKey2
        RCLCxPeerPos peerPos{
            publicKey1,  // Wrong public key for this signature
            sigSlice,
            suppression,
            std::move(proposal)};

        // Verify the signature should fail
        BEAST_EXPECT(!peerPos.checkSign());
    }

    void
    testSecp256k1ProposalSignature()
    {
        testcase("secp256k1 proposal signature verification");

        // Generate secp256k1 keypair
        auto const [publicKey, secretKey] =
            randomKeyPair(KeyType::secp256k1);

        // Verify key type
        BEAST_EXPECT(*publicKeyType(publicKey) == KeyType::secp256k1);

        // Create a proposal
        uint256 const prevLedger{42};
        uint256 const position{100};
        std::uint32_t const proposeSeq = 5;
        auto const closeTime = NetClock::time_point{NetClock::duration{123456}};
        auto const now = NetClock::time_point{NetClock::duration{123456}};
        NodeID const nodeID = calcNodeID(publicKey);

        RCLCxPeerPos::Proposal proposal{
            prevLedger, proposeSeq, position, closeTime, now, nodeID};

        // Calculate signing hash
        uint256 const signingHash = sha512Half(
            HashPrefix::proposal,
            std::uint32_t(proposal.proposeSeq()),
            proposal.closeTime().time_since_epoch().count(),
            proposal.prevLedger(),
            proposal.position());

        // Sign the proposal
        auto signature = signDigest(publicKey, secretKey, signingHash);

        BEAST_EXPECT(signature.size() > 0);
        BEAST_EXPECT(signature.size() <= 72);  // secp256k1 sig max size

        // Create unique suppression ID
        Slice const sigSlice{signature.data(), signature.size()};
        uint256 const suppression = proposalUniqueId(
            proposal.position(),
            proposal.prevLedger(),
            proposal.proposeSeq(),
            proposal.closeTime(),
            publicKey.slice(),
            sigSlice);

        // Construct RCLCxPeerPos with the signature
        RCLCxPeerPos peerPos{
            publicKey, sigSlice, suppression, std::move(proposal)};

        // Verify the signature
        BEAST_EXPECT(peerPos.checkSign());
    }

public:
    void
    run() override
    {
        testDilithiumProposalSignature();
        testInvalidDilithiumSignature();
        testWrongKeyProposalSignature();
        testSecp256k1ProposalSignature();
    }
};

BEAST_DEFINE_TESTSUITE(RCLCxPeerPos, app, xrpl);

}  // namespace test
}  // namespace xrpl
