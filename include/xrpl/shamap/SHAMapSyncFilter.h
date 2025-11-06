#ifndef XRPL_SHAMAP_SHAMAPSYNCFILTER_H_INCLUDED
#define XRPL_SHAMAP_SHAMAPSYNCFILTER_H_INCLUDED

#include <xrpl/shamap/SHAMapTreeNode.h>

#include <optional>

/** Callback for filtering SHAMap during sync. */
namespace ripple {

class SHAMapSyncFilter
{
public:
    virtual ~SHAMapSyncFilter() = default;
    SHAMapSyncFilter() = default;
    SHAMapSyncFilter(SHAMapSyncFilter const&) = delete;
    SHAMapSyncFilter&
    operator=(SHAMapSyncFilter const&) = delete;

    // Note that the nodeData is overwritten by this call
    virtual void
    gotNode(
        bool fromFilter,
        SHAMapHash const& nodeHash,
        std::uint32_t ledgerSeq,
        Blob&& nodeData,
        SHAMapNodeType type) const = 0;

    virtual std::optional<Blob>
    getNode(SHAMapHash const& nodeHash) const = 0;
};

}  // namespace ripple

#endif
