#pragma once

#include <xrpld/app/main/Application.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/TaggedCache.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/shamap/SHAMapSyncFilter.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace xrpl {

// Sync filters allow low-level SHAMapSync code to interact correctly with
// higher-level structures such as caches and transaction stores

// This class is needed on both add and check functions
// sync filter for transaction sets during consensus building
class ConsensusTransSetSF : public SHAMapSyncFilter
{
public:
    using NodeCache = TaggedCache<SHAMapHash, Blob>;

    /**
     * The size a node's hash-prefixed wire data must reach before gotNode()
     * tries to parse and resubmit it as a transaction.
     *
     * A threshold rather than a derived bound: the smallest a hash-prefixed
     * SHAMap leaf can be is the 4-byte HashPrefix plus kMinShaMapItemBytes, and
     * nothing that size is a signed transaction either. The extra byte is the
     * long-standing threshold this check has always used, kept as it was.
     */
    static constexpr std::size_t kMinTxNodeBytesToParse =
        sizeof(std::uint32_t) + kMinShaMapItemBytes + 1;

    ConsensusTransSetSF(Application& app, NodeCache& nodeCache);

    // Note that the nodeData is overwritten by this call
    void
    gotNode(
        bool fromFilter,
        SHAMapHash const& nodeHash,
        std::uint32_t ledgerSeq,
        Blob&& nodeData,
        SHAMapNodeType type) const override;

    [[nodiscard]] std::optional<Blob>
    getNode(SHAMapHash const& nodeHash) const override;

private:
    Application& app_;
    NodeCache& nodeCache_;
    beast::Journal const j_;
};

}  // namespace xrpl
