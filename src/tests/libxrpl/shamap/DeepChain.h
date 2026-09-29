#pragma once

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapAddNode.h>
#include <xrpl/shamap/SHAMapLeafNode.h>
#include <xrpl/shamap/SHAMapNodeID.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace xrpl::tests {

/**
 * A chain of inner nodes in wire form, from the root down to one deepest node,
 * each with one real child.
 *
 * Built bottom-up, so every node hashes correctly and the root hash commits to
 * the whole shape. Each node sits on the branch pathKey selects at its depth.
 *
 * Two shapes: the constructors run inner nodes all the way to
 * SHAMap::kLeafDepth, a depth only a leaf may occupy, and toLeaf() ends at a
 * real transaction leaf.
 *
 * Depends only on libxrpl, so either test tree can include it. The xrpld
 * counterpart is test::packetFor() in src/test/app/AcquireTestHelpers.h.
 */
struct DeepChain
{
    // nodes[d] is the deserialized node for depth d.
    std::vector<SHAMapTreeNodePtr> nodes;
    SHAMapHash rootHash;

    // The key whose path through the tree this chain spells out. Zero for a chain
    // built without a leaf, which therefore sits on branch 0 at every depth.
    UInt256 pathKey;

    // The depth of the deepest node, the last one nodesBelowRoot() hands out.
    unsigned int deepestDepth{SHAMap::kLeafDepth};

    /**
     * The payload size of the leaf toLeaf() builds, which is the smallest a
     * SHAMap item may be.
     */
    static constexpr std::size_t kLeafItemBytes = kMinShaMapItemBytes;

    /**
     * A chain of inner nodes reaching SHAMap::kLeafDepth, a depth only a leaf
     * may occupy.
     *
     * @param seed Varies the whole chain, so two chains coexist with distinct
     *        nodes. Caches and fetch packs are keyed by hash, so
     *        identically-seeded chains are the same chain.
     */
    explicit DeepChain(unsigned int seed = 1) : DeepChain(std::nullopt, seed)
    {
    }

    /**
     * A chain ending in a real transaction leaf, which completes an
     * acquisition.
     *
     * @param depth Where the leaf sits, at most SHAMap::kLeafDepth. Zero puts
     *        the leaf at the root. A deeper value throws std::logic_error, since
     *        no leaf can sit there.
     * @param seed Varies the leaf's contents, and so the whole chain. See the
     *        constructor.
     * @return The chain.
     */
    [[nodiscard]] static DeepChain
    toLeaf(unsigned int depth, unsigned int seed = 1)
    {
        if (depth > SHAMap::kLeafDepth)
            Throw<std::logic_error>("DeepChain: leaf depth past SHAMap::kLeafDepth");
        return DeepChain{std::optional{depth}, seed};
    }

    /**
     * The node the chain holds at the given depth, root first.
     *
     * @param depth The depth of the node to return, at most deepestDepth.
     * @return The node.
     */
    [[nodiscard]] SHAMapTreeNodePtr
    nodeAt(unsigned int depth) const
    {
        return nodes[depth];
    }

    /**
     * Where the node at the given depth claims to belong, which is on the
     * path to pathKey.
     *
     * @param depth The depth of the node to locate.
     * @return The node's claimed position.
     */
    [[nodiscard]] SHAMapNodeID
    idAt(unsigned int depth) const
    {
        return SHAMapNodeID::createID(depth, pathKey);
    }

    /**
     * The same node in the prefixed form used for storage and fetch packs,
     * which is what hashes to the node's own hash.
     *
     * @param depth The depth of the node to serialize.
     * @return The node's prefixed serialized form.
     */
    [[nodiscard]] Blob
    prefixedNodeAt(unsigned int depth) const
    {
        Serializer s;
        nodeAt(depth)->serializeWithPrefix(s);
        return s.modData();
    }

    /**
     * Every node below the root, down to and including the deepest one.
     *
     * @param firstDepth The shallowest node to include, so a caller can feed
     *        the chain in more than one batch.
     * @return The nodes, each with its claimed position.
     */
    [[nodiscard]] std::vector<std::pair<SHAMapNodeID, SHAMapTreeNodePtr>>
    nodesBelowRoot(unsigned int firstDepth = 1) const
    {
        std::vector<std::pair<SHAMapNodeID, SHAMapTreeNodePtr>> data;
        for (auto depth = firstDepth; depth <= deepestDepth; ++depth)
            data.emplace_back(idAt(depth), nodeAt(depth));
        return data;
    }

    /**
     * Fill a synching map, stopping one level short of the deepest node so the
     * caller offers that one itself.
     *
     * Reports rather than asserts: the two binaries sharing this header use
     * different test frameworks.
     *
     * @param map The map to fill.
     * @return Whether the root and every node above the deepest one was
     *         accepted, which is a property of the chain.
     */
    [[nodiscard]] bool
    fill(SHAMap& map) const
    {
        if (!map.addRootNode(rootHash, nodeAt(0), nullptr).isGood())
            return false;

        for (auto depth = 1u; depth < deepestDepth; ++depth)
        {
            if (!map.addKnownNode(idAt(depth), nodeAt(depth), nullptr).isUseful())
                return false;
        }

        return true;
    }

    /**
     * Offer the deepest node, which for a fabricated chain is the inner node at
     * SHAMap::kLeafDepth, a depth only a leaf may occupy.
     *
     * @param map The map to offer the node to, filled by fill() first.
     * @return The verdict addKnownNode() reached.
     */
    [[nodiscard]] SHAMapAddNode
    addOffendingNode(SHAMap& map) const
    {
        return map.addKnownNode(idAt(deepestDepth), nodeAt(deepestDepth), nullptr);
    }

private:
    /**
     * Build any of the shapes.
     *
     * @param leafDepth Where a real transaction leaf sits, or nullopt to run
     *        inner nodes all the way to SHAMap::kLeafDepth instead.
     * @param seed Varies the chain's contents. See the public entry points.
     */
    DeepChain(std::optional<unsigned int> leafDepth, unsigned int seed)
        : nodes(leafDepth.value_or(SHAMap::kLeafDepth) + 1)
    {
        if (!leafDepth)
        {
            // With no leaf depth given, the deepest inner node points at a child that stays
            // unresolvable.
            buildInnersDownTo(SHAMap::kLeafDepth, SHAMapHash{UInt256{seed}});
            return;
        }

        // Exactly kLeafItemBytes of payload, the smallest a leaf item may be. Checked rather than
        // assumed, since a caller relates that constant to a threshold of its own.
        Serializer payload;
        payload.add32(seed);
        payload.add32(0);
        payload.add32(0);
        if (payload.size() != kLeafItemBytes)
            Throw<std::logic_error>("DeepChain: unexpected leaf payload size");

        Serializer wire;
        wire.addRaw(payload.peekData());
        wire.add8(kWireTypeTransaction);

        auto const leaf = SHAMapTreeNode::makeFromWire(makeSlice(wire.peekData()));

        // A transaction leaf's key is the hash of its own contents, so its position follows
        // this key's nibbles.
        pathKey = leafKey(*leaf);
        deepestDepth = *leafDepth;
        nodes[*leafDepth] = leaf;

        if (*leafDepth == 0)
        {
            // The leaf is the root, so the build stops here.
            rootHash = leaf->getHash();
            return;
        }

        buildInnersDownTo(*leafDepth - 1, leaf->getHash());
    }

    /**
     * Fill in inner nodes, each with one real child, from the root down to the
     * given depth, and record the root hash.
     *
     * Bottom-up, since each node's hash covers the child hash below it.
     *
     * @param deepest The depth of the deepest inner node to build. May be
     *        SHAMap::kLeafDepth, which is the fabricated chain's whole point.
     * @param childHash What that deepest inner node points at.
     */
    void
    buildInnersDownTo(unsigned int deepest, SHAMapHash childHash)
    {
        for (auto depth = deepest + 1; depth-- > 0;)
        {
            // A key has 64 nibbles, so SHAMap::kLeafDepth is one past the last nibble
            // selectBranch() reads from a 32-byte key. A fabricated chain's pathKey is zero, so
            // branch 0 is the position such a node claims.
            auto const branch =
                depth == SHAMap::kLeafDepth ? 0u : selectBranch(idAt(depth), pathKey);

            Serializer s;
            s.addBitString(childHash.asUInt256());
            s.add8(static_cast<unsigned char>(branch));

            s.add8(kWireTypeCompressedInner);

            auto node = SHAMapTreeNode::makeFromWire(makeSlice(s.peekData()));
            childHash = node->getHash();
            nodes[depth] = std::move(node);
        }

        rootHash = childHash;
    }
};

}  // namespace xrpl::tests
