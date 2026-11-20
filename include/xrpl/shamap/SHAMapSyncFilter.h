#pragma once

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <cstdint>
#include <optional>

/**
 * Callback for filtering SHAMap during sync.
 */
namespace xrpl {

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

    /**
     * Fetch the node data for a hash, if this filter holds it.
     *
     * Postcondition: the data returned hashes to `nodeHash`. Callers adopt that
     * hash without recomputing it and publish the node to the map's caches. An
     * implementation checks the digest before answering, unless its own storage
     * ties the data to the key.
     *
     * @param nodeHash the hash of the node wanted.
     * @return the node's hash-prefixed wire data, or nothing.
     */
    [[nodiscard]] virtual std::optional<Blob>
    getNode(SHAMapHash const& nodeHash) const = 0;
};

}  // namespace xrpl
