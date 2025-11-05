#include <xrpld/app/consensus/RCLCxPeerPos.h>

#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

// Used to construct received proposals
RCLCxPeerPos::RCLCxPeerPos(
    PublicKey const& publicKey,
    Slice const& signature,
    uint256 const& suppression,
    Proposal&& proposal)
    : publicKey_(publicKey)
    , suppression_(suppression)
    , proposal_(std::move(proposal))
{
    // The maximum allowed size of a signature is 72 bytes; we verify
    // this elsewhere, but we want to be extra careful here:
    XRPL_ASSERT(
        signature.size() != 0 && signature.size() <= signature_.capacity(),
        "ripple::RCLCxPeerPos::RCLCxPeerPos : valid signature size");

    if (signature.size() != 0 && signature.size() <= signature_.capacity())
        signature_.assign(signature.begin(), signature.end());
}

bool
RCLCxPeerPos::checkSign() const
{
    return verifyDigest(
        publicKey(), proposal_.signingHash(), signature(), false);
}

Json::Value
RCLCxPeerPos::getJson() const
{
    auto ret = proposal().getJson();

    if (publicKey().size())
        ret[jss::peer_id] = toBase58(TokenType::NodePublic, publicKey());

    return ret;
}

uint256
proposalUniqueId(
    uint256 const& proposeHash,
    uint256 const& previousLedger,
    std::uint32_t proposeSeq,
    NetClock::time_point closeTime,
    Slice const& publicKey,
    Slice const& signature)
{
    Serializer s(512);
    s.addBitString(proposeHash);
    s.addBitString(previousLedger);
    s.add32(proposeSeq);
    s.add32(closeTime.time_since_epoch().count());
    s.addVL(publicKey);
    s.addVL(signature);

    return s.getSHA512Half();
}

}  // namespace ripple
