
#include <xrpld/app/consensus/RCLCxPeerPos.h>
#include <xrpld/app/ledger/Ledger.h>
#include <xrpld/app/misc/HashRouter.h>
#include <xrpld/overlay/detail/handlers/ProtocolMessageHandler.h>
#include <xrpld/shamap/SHAMap.h>

#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STValidation.h>
#include <xrpl/protocol/messages.h>

namespace ripple {

// Helper function to check for valid uint256 values in protobuf buffers
static bool
stringIsUint256Sized(std::string const& pBuffStr)
{
    return pBuffStr.size() == uint256::size();
}

void
ProtocolMessageHandler::onMessage(
    std::shared_ptr<protocol::TMProposeSet> const& m)
{
    protocol::TMProposeSet& set = *m;

    auto const sig = makeSlice(set.signature());

    // Preliminary check for the validity of the signature: A DER encoded
    // signature can't be longer than 72 bytes.
    if ((std::clamp<std::size_t>(sig.size(), 64, 72) != sig.size()) ||
        (publicKeyType(makeSlice(set.nodepubkey())) != KeyType::secp256k1))
    {
        JLOG(p_journal_.warn()) << "Proposal: malformed";
        fee_.update(
            Resource::feeInvalidSignature,
            " signature can't be longer than 72 bytes");
        return;
    }

    if (!stringIsUint256Sized(set.currenttxhash()) ||
        !stringIsUint256Sized(set.previousledger()))
    {
        JLOG(p_journal_.warn()) << "Proposal: malformed";
        fee_.update(Resource::feeMalformedRequest, "bad hashes");
        return;
    }

    // RH TODO: when isTrusted = false we should probably also cache a key
    // suppression for 30 seconds to avoid doing a relatively expensive
    // lookup every time a spam packet is received
    PublicKey const publicKey{makeSlice(set.nodepubkey())};
    auto const isTrusted = app_.validators().trusted(publicKey);

    // If the operator has specified that untrusted proposals be dropped
    // then this happens here I.e. before further wasting CPU verifying the
    // signature of an untrusted key
    if (!isTrusted)
    {
        // report untrusted proposal messages
        overlay_.reportInboundTraffic(
            TrafficCount::category::proposal_untrusted,
            Message::messageSize(*m));

        if (app_.config().RELAY_UNTRUSTED_PROPOSALS == -1)
            return;
    }

    uint256 const proposeHash{set.currenttxhash()};
    uint256 const prevLedger{set.previousledger()};

    NetClock::time_point const closeTime{NetClock::duration{set.closetime()}};

    uint256 const suppression = proposalUniqueId(
        proposeHash,
        prevLedger,
        set.proposeseq(),
        closeTime,
        publicKey.slice(),
        sig);

    if (auto [added, relayed] =
            app_.getHashRouter().addSuppressionPeerWithStatus(suppression, id_);
        !added)
    {
        // Count unique messages (Slots has it's own 'HashRouter'), which a
        // peer receives within IDLED seconds since the message has been
        // relayed.
        if (relayed && (stopwatch().now() - *relayed) < reduce_relay::IDLED)
            overlay_.updateSlotAndSquelch(
                suppression, publicKey, id_, protocol::mtPROPOSE_LEDGER);

        // report duplicate proposal messages
        overlay_.reportInboundTraffic(
            TrafficCount::category::proposal_duplicate,
            Message::messageSize(*m));

        JLOG(p_journal_.trace()) << "Proposal: duplicate";

        return;
    }

    if (!isTrusted)
    {
        if (tracking_.load() == Tracking::diverged)
        {
            JLOG(p_journal_.debug())
                << "Proposal: Dropping untrusted (peer divergence)";
            return;
        }

        if (!cluster() && app_.getFeeTrack().isLoadedLocal())
        {
            JLOG(p_journal_.debug()) << "Proposal: Dropping untrusted (load)";
            return;
        }
    }

    JLOG(p_journal_.trace())
        << "Proposal: " << (isTrusted ? "trusted" : "untrusted");

    auto proposal = RCLCxPeerPos(
        publicKey,
        sig,
        suppression,
        RCLCxPeerPos::Proposal{
            prevLedger,
            set.proposeseq(),
            proposeHash,
            closeTime,
            app_.timeKeeper().closeTime(),
            calcNodeID(app_.validatorManifests().getMasterKey(publicKey))});

    std::weak_ptr<PeerImp> weak = shared_from_this();
    app_.getJobQueue().addJob(
        isTrusted ? jtPROPOSAL_t : jtPROPOSAL_ut,
        "recvPropose->checkPropose",
        [weak, isTrusted, m, proposal]() {
            if (auto peer = weak.lock())
                peer->checkPropose(isTrusted, m, proposal);
        });
}

}  // namespace ripple