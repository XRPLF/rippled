#pragma once

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/shamap/SHAMapSyncFilter.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace xrpl::tests {

/**
 * A sync filter that serves the nodes it was given, by hash, and records
 * nothing.
 *
 * Serving is opt-in: a node absent from the set looks unavailable. Each node is
 * held in its prefixed form, which is what SHAMap parses a filter's answer
 * from.
 */
class ServingFilter : public SHAMapSyncFilter
{
public:
    ServingFilter() = default;

    /**
     * @param hash The hash to serve the node under.
     * @param blob The node's prefixed form.
     */
    ServingFilter(SHAMapHash const& hash, Blob blob)
    {
        serve(hash, std::move(blob));
    }

    /**
     * @param nodes Each node's hash and prefixed form.
     */
    explicit ServingFilter(std::vector<std::pair<SHAMapHash, Blob>> nodes)
    {
        for (auto& [hash, blob] : nodes)
            serve(hash, std::move(blob));
    }

    void
    gotNode(
        bool,
        SHAMapHash const&,
        std::uint32_t,
        Blob&&,  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
        SHAMapNodeType) const override
    {
    }

    [[nodiscard]] std::optional<Blob>
    getNode(SHAMapHash const& hash) const override
    {
        if (auto const it = nodes_.find(hash); it != nodes_.end())
            return it->second;
        return std::nullopt;
    }

    /**
     * Offer a node back to the map, as a fetch pack does.
     *
     * @param node The node to serve, keyed by its own hash.
     */
    void
    serve(SHAMapTreeNodePtr const& node)
    {
        Serializer s;
        node->serializeWithPrefix(s);
        serve(node->getHash(), s.modData());
    }

    /**
     * Offer a node whose prefixed form the caller already holds.
     *
     * @param hash The hash to serve the node under.
     * @param blob The node's prefixed form.
     */
    void
    serve(SHAMapHash const& hash, Blob blob)
    {
        nodes_.emplace(hash, std::move(blob));
    }

private:
    std::map<SHAMapHash, Blob> nodes_;
};

}  // namespace xrpl::tests
