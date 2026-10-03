#include <xrpld/app/ledger/ConsensusTransSetSF.h>

#include <xrpld/app/ledger/TransactionMaster.h>
#include <xrpld/app/misc/Transaction.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/TaggedCache.ipp>  // IWYU pragma: keep
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/digest.h>  // IWYU pragma: keep
#include <xrpl/server/NetworkOPs.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace xrpl {

ConsensusTransSetSF::ConsensusTransSetSF(Application& app, NodeCache& nodeCache)
    : app_(app), nodeCache_(nodeCache), j_(app.getJournal("TransactionAcquire"))
{
}

void
ConsensusTransSetSF::gotNode(
    bool fromFilter,
    SHAMapHash const& nodeHash,
    std::uint32_t,
    Blob&& nodeData,  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
    SHAMapNodeType type) const
{
    if (fromFilter)
        return;

    nodeCache_.insert(nodeHash, nodeData);

    if ((type == SHAMapNodeType::TnTransactionNm) && (nodeData.size() >= kMinTxNodeBytesToParse))
    {
        // this is a transaction, and we didn't have it
        JLOG(j_.debug()) << "Node on our acquiring TX set is TXN we may not have";

        try
        {
            // skip prefix
            Serializer const s(nodeData.data() + 4, nodeData.size() - 4);
            SerialIter sit(s.slice());
            auto stx = std::make_shared<STTx const>(std::ref(sit));
            XRPL_ASSERT(
                stx->getTransactionID() == nodeHash.asUInt256(),
                "xrpl::ConsensusTransSetSF::gotNode : transaction hash "
                "match");
            auto const pap = &app_;
            app_.getJobQueue().addJob(
                JtTransaction, "TxsToTxn", [pap, stx]() { pap->getOPs().submitTransaction(stx); });
        }
        catch (std::exception const& ex)
        {
            JLOG(j_.warn()) << "Fetched invalid transaction in proposed set. Exception: "
                            << ex.what();
        }
    }
}

std::optional<Blob>
ConsensusTransSetSF::getNode(SHAMapHash const& nodeHash) const
{
    // Both arms answer for their own data, as getNode()'s postcondition requires. A caller adopts
    // the hash this was asked for, so the digest is computed here.
    Blob nodeData;
    {
        // One lock spans the read, the digest and the drop, so the entry dropped is the entry
        // digested. The cache's mutex is recursive, so retrieve() and del() re-enter it.
        std::unique_lock const sl(nodeCache_.peekMutex());

        if (nodeCache_.retrieve(nodeHash, nodeData))
        {
            // Only the cache's filing ties the blob to this key, so the digest is taken here.
            if (sha512Half(makeSlice(nodeData)) == nodeHash.asUInt256())
                return nodeData;

            // The transaction arm below reads a different store, which may hold this key.
            nodeCache_.del(nodeHash, false);
            JLOG(j_.warn()) << "Cached node " << nodeHash
                            << " does not hash to its key, dropped it";
        }
    }

    auto txn = app_.getMasterTransaction().fetchFromCache(nodeHash.asUInt256());

    if (txn)
    {
        // this is a transaction, and we have it
        JLOG(j_.trace()) << "Node in our acquiring TX set is TXN we have";
        auto const& stx = txn->getSTransaction();

        // This serialization has to reproduce the node hash asked for, so the identity is tested
        // here. A transaction's id is the digest of exactly the bytes serialized below and is held
        // on the transaction, so the test reads it rather than digesting them again.
        if (stx->getTransactionID() != nodeHash.asUInt256())
        {
            JLOG(j_.warn()) << "Transaction for node " << nodeHash
                            << " does not serialize to it, discarding it";
            return std::nullopt;
        }

        Serializer s;
        s.add32(HashPrefix::TransactionId);
        stx->add(s);
        return std::move(s.modData());
    }

    return std::nullopt;
}

}  // namespace xrpl
