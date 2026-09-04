#include <xrpld/app/ledger/detail/LedgerReplayMsgHandler.h>

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/LedgerReplayer.h>
#include <xrpld/app/main/Application.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapMissingNode.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <boost/smart_ptr/intrusive_ptr.hpp>

#include <xrpl.pb.h>

#include <cstdint>
#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace xrpl {
LedgerReplayMsgHandler::LedgerReplayMsgHandler(Application& app, LedgerReplayer& replayer)
    : app_(app), replayer_(replayer), journal_(app.getJournal("LedgerReplayMsgHandler"))
{
}

protocol::TMProofPathResponse
LedgerReplayMsgHandler::processProofPathRequest(
    std::shared_ptr<protocol::TMProofPathRequest> const& msg)
{
    protocol::TMProofPathRequest& packet = *msg;
    protocol::TMProofPathResponse reply;

    if (!packet.has_key() || !packet.has_ledgerhash() || !packet.has_type() ||
        packet.ledgerhash().size() != UInt256::size() || packet.key().size() != UInt256::size() ||
        !protocol::TMLedgerMapType_IsValid(packet.type()))
    {
        JLOG(journal_.debug()) << "getProofPath: Invalid request";
        reply.set_error(protocol::TMReplyError::reBAD_REQUEST);
        return reply;
    }
    reply.set_key(packet.key());
    reply.set_ledgerhash(packet.ledgerhash());
    reply.set_type(packet.type());

    UInt256 const key = UInt256::fromRaw(packet.key());
    UInt256 const ledgerHash = UInt256::fromRaw(packet.ledgerhash());
    auto ledger = app_.getLedgerMaster().getLedgerByHash(ledgerHash);
    if (!ledger)
    {
        JLOG(journal_.debug()) << "getProofPath: Don't have ledger " << ledgerHash;
        reply.set_error(protocol::TMReplyError::reNO_LEDGER);
        return reply;
    }

    auto const path = [&]() -> std::optional<std::vector<Blob>> {
        switch (packet.type())
        {
            case protocol::lmACCOUNT_STATE:
                return ledger->stateMap().getProofPath(key);
            case protocol::lmTRANSACTION:
                return ledger->txMap().getProofPath(key);
            default:
                // should not be here
                // because already tested with TMLedgerMapType_IsValid()
                return {};
        }
    }();

    if (!path)
    {
        JLOG(journal_.debug()) << "getProofPath: Don't have the node " << key << " of ledger "
                               << ledgerHash;
        reply.set_error(protocol::TMReplyError::reNO_NODE);
        return reply;
    }

    // pack header
    Serializer nData(128);
    addRaw(ledger->header(), nData);
    reply.set_ledgerheader(nData.getDataPtr(), nData.getLength());
    // pack path
    for (auto const& b : *path)
        reply.add_path(b.data(), b.size());

    JLOG(journal_.debug()) << "getProofPath for the node " << key << " of ledger " << ledgerHash
                           << " path length " << path->size();
    return reply;
}

ReplayMsgStatus
LedgerReplayMsgHandler::processProofPathResponse(
    std::shared_ptr<protocol::TMProofPathResponse> const& msg)
{
    protocol::TMProofPathResponse const& reply = *msg;
    if (reply.has_error())
    {
        JLOG(journal_.debug()) << "ProofPathResponse: peer reported error";
        return ReplayMsgStatus::BadData;
    }
    if (!reply.has_key() || !reply.has_ledgerhash() || !reply.has_type() ||
        !reply.has_ledgerheader() || reply.path_size() == 0 ||
        reply.ledgerhash().size() != UInt256::size() || reply.key().size() != UInt256::size())
    {
        JLOG(journal_.debug()) << "ProofPathResponse: malformed (missing or wrong-size fields)";
        return ReplayMsgStatus::Malformed;
    }

    if (reply.type() != protocol::lmACCOUNT_STATE)
    {
        JLOG(journal_.debug()) << "ProofPathResponse: malformed (unsupported map type)";
        return ReplayMsgStatus::Malformed;
    }

    // deserialize the header
    LedgerHeader info;
    try
    {
        info = deserializeHeader(makeSlice(reply.ledgerheader()));
    }
    catch (std::exception const& e)
    {
        JLOG(journal_.debug()) << "ProofPathResponse: malformed header (" << e.what() << ")";
        return ReplayMsgStatus::Malformed;
    }
    UInt256 const replyHash = UInt256::fromRaw(reply.ledgerhash());
    if (calculateLedgerHash(info) != replyHash)
    {
        JLOG(journal_.debug()) << "ProofPathResponse: malformed (hash mismatch)";
        return ReplayMsgStatus::Malformed;
    }
    info.hash = replyHash;

    UInt256 const key = UInt256::fromRaw(reply.key());
    if (key != keylet::skip().key)
    {
        JLOG(journal_.debug()) << "ProofPathResponse: malformed (unexpected key " << key << ")";
        return ReplayMsgStatus::Malformed;
    }

    // verify the skip list
    std::vector<Blob> path;
    path.reserve(reply.path_size());
    for (int i = 0; i < reply.path_size(); ++i)
    {
        path.emplace_back(reply.path(i).begin(), reply.path(i).end());
    }

    if (!SHAMap::verifyProofPath(info.accountHash, key, path))
    {
        JLOG(journal_.debug()) << "ProofPathResponse: malformed (proof path verify failed)";
        return ReplayMsgStatus::Malformed;
    }

    // deserialize the SHAMapItem
    SHAMapTreeNodePtr node;
    try
    {
        node = SHAMapTreeNode::makeFromWire(makeSlice(path.front()));
    }
    catch (std::exception const& e)
    {
        JLOG(journal_.debug()) << "ProofPathResponse: malformed SHAMap node (" << e.what() << ")";
        return ReplayMsgStatus::Malformed;
    }
    if (!node || !node->isLeaf())
    {
        JLOG(journal_.debug()) << "ProofPathResponse: malformed (not a leaf node)";
        return ReplayMsgStatus::Malformed;
    }

    if (auto item = safeDowncast<SHAMapLeafNode*>(node.get())->peekItem())
    {
        replayer_.gotSkipList(info, item);
        return ReplayMsgStatus::Ok;
    }

    JLOG(journal_.debug()) << "ProofPathResponse: malformed (no SHAMapItem)";
    return ReplayMsgStatus::Malformed;
}

protocol::TMReplayDeltaResponse
LedgerReplayMsgHandler::processReplayDeltaRequest(
    std::shared_ptr<protocol::TMReplayDeltaRequest> const& msg)
{
    protocol::TMReplayDeltaRequest const& packet = *msg;
    protocol::TMReplayDeltaResponse reply;

    if (!packet.has_ledgerhash() || packet.ledgerhash().size() != UInt256::size())
    {
        JLOG(journal_.debug()) << "getReplayDelta: Invalid request";
        reply.set_error(protocol::TMReplyError::reBAD_REQUEST);
        return reply;
    }
    reply.set_ledgerhash(packet.ledgerhash());

    UInt256 const ledgerHash = UInt256::fromRaw(packet.ledgerhash());
    auto ledger = app_.getLedgerMaster().getLedgerByHash(ledgerHash);
    if (!ledger || !ledger->isImmutable())
    {
        JLOG(journal_.debug()) << "getReplayDelta: Don't have ledger " << ledgerHash;
        reply.set_error(protocol::TMReplyError::reNO_LEDGER);
        return reply;
    }

    // pack header
    Serializer nData(128);
    addRaw(ledger->header(), nData);
    reply.set_ledgerheader(nData.getDataPtr(), nData.getLength());
    // pack transactions
    auto const& txMap = ledger->txMap();
    txMap.visitLeaves([&](boost::intrusive_ptr<SHAMapItem const> const& txNode) {
        reply.add_transaction(txNode->data(), txNode->size());
    });

    JLOG(journal_.debug()) << "getReplayDelta for ledger " << ledgerHash << " txMap hash "
                           << txMap.getHash().asUInt256();
    return reply;
}

ReplayMsgStatus
LedgerReplayMsgHandler::processReplayDeltaResponse(
    std::shared_ptr<protocol::TMReplayDeltaResponse> const& msg)
{
    protocol::TMReplayDeltaResponse const& reply = *msg;
    if (reply.has_error())
    {
        JLOG(journal_.debug()) << "ReplayDeltaResponse: peer reported error";
        return ReplayMsgStatus::BadData;
    }
    if (!reply.has_ledgerheader() || !reply.has_ledgerhash() ||
        reply.ledgerhash().size() != UInt256::size())
    {
        JLOG(journal_.debug()) << "ReplayDeltaResponse: malformed (missing or wrong-size fields)";
        return ReplayMsgStatus::Malformed;
    }

    LedgerHeader info;
    try
    {
        info = deserializeHeader(makeSlice(reply.ledgerheader()));
    }
    catch (std::exception const& e)
    {
        JLOG(journal_.debug()) << "ReplayDeltaResponse: malformed header (" << e.what() << ")";
        return ReplayMsgStatus::Malformed;
    }
    UInt256 const replyHash = UInt256::fromRaw(reply.ledgerhash());
    if (calculateLedgerHash(info) != replyHash)
    {
        JLOG(journal_.debug()) << "ReplayDeltaResponse: malformed (hash mismatch)";
        return ReplayMsgStatus::Malformed;
    }
    info.hash = replyHash;

    auto numTxns = reply.transaction_size();
    std::map<std::uint32_t, std::shared_ptr<STTx const>> orderedTxns;
    SHAMap txMap(SHAMapType::TRANSACTION, app_.getNodeFamily());
    try
    {
        for (int i = 0; i < numTxns; ++i)
        {
            // deserialize:
            // -- TxShaMapItem for building a ShaMap for verification
            // -- Tx
            // -- TxMetaData for Tx ordering
            Serializer const shaMapItemData(
                reply.transaction(i).data(), reply.transaction(i).size());

            SerialIter txMetaSit(makeSlice(reply.transaction(i)));
            SerialIter txSit(txMetaSit.getSlice(txMetaSit.getVLDataLength()));
            SerialIter metaSit(txMetaSit.getSlice(txMetaSit.getVLDataLength()));

            auto tx = std::make_shared<STTx const>(txSit);
            if (!tx)
            {
                JLOG(journal_.debug()) << "ReplayDeltaResponse: malformed (tx deserialize)";
                return ReplayMsgStatus::Malformed;
            }
            auto tid = tx->getTransactionID();
            STObject meta(metaSit, sfMetadata);
            orderedTxns.emplace(meta[sfTransactionIndex], std::move(tx));

            if (!txMap.addGiveItem(
                    SHAMapNodeType::TnTransactionMd, makeShamapitem(tid, shaMapItemData.slice())))
            {
                JLOG(journal_.debug()) << "ReplayDeltaResponse: malformed (tx map add)";
                return ReplayMsgStatus::Malformed;
            }
        }
    }
    catch (std::exception const& e)
    {
        JLOG(journal_.debug()) << "ReplayDeltaResponse: malformed transactions (" << e.what()
                               << ")";
        return ReplayMsgStatus::Malformed;
    }

    if (txMap.getHash().asUInt256() != info.txHash)
    {
        JLOG(journal_.debug()) << "ReplayDeltaResponse: malformed (transactions verify failed)";
        return ReplayMsgStatus::Malformed;
    }

    replayer_.gotReplayDelta(info, std::move(orderedTxns));
    return ReplayMsgStatus::Ok;
}

}  // namespace xrpl
