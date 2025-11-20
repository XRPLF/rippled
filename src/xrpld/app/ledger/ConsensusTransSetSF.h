#ifndef XRPL_APP_LEDGER_CONSENSUSTRANSSETSF_H_INCLUDED
#define XRPL_APP_LEDGER_CONSENSUSTRANSSETSF_H_INCLUDED

#include <xrpld/app/main/Application.h>

#include <xrpl/basics/TaggedCache.h>
#include <xrpl/shamap/SHAMapSyncFilter.h>

namespace ripple {

// Sync filters allow low-level SHAMapSync code to interact correctly with
// higher-level structures such as caches and transaction stores

// This class is needed on both add and check functions
// sync filter for transaction sets during consensus building
class ConsensusTransSetSF : public SHAMapSyncFilter
{
public:
    using NodeCache = TaggedCache<SHAMapHash, Blob>;

    ConsensusTransSetSF(Application& app, NodeCache& nodeCache);

    // Note that the nodeData is overwritten by this call
    void
    gotNode(
        bool fromFilter,
        SHAMapHash const& nodeHash,
        std::uint32_t ledgerSeq,
        Blob&& nodeData,
        SHAMapNodeType type) const override;

    std::optional<Blob>
    getNode(SHAMapHash const& nodeHash) const override;

private:
    Application& app_;
    NodeCache& m_nodeCache;
    beast::Journal const j_;
};

}  // namespace ripple

#endif
