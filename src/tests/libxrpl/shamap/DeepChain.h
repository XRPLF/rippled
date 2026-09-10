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
 * A chain of inner nodes, each with one real child (and, when built with a decoy, an
 * additional unresolvable second child), from the root down to one deepest node, in
 * wire form.
 *
 * Built bottom-up, so the root hash commits to the whole shape and every node
 * hashes correctly, which lets a test supply one level at a time and have each
 * accepted on its own merits. Every node sits on the branch pathKey selects at
 * its depth, so a receiver descending towards that key walks the whole chain.
 *
 * Two shapes, separating a chain a peer could honestly send from one it could
 * not:
 *
 * - the constructors run inner nodes all the way to SHAMap::kLeafDepth,
 *   where no valid tree can hold one (see the badDepth check in
 *   SHAMap::addKnownNode), so feeding it invalidates the map;
 * - toLeaf() stops at a real transaction leaf, so feeding it completes an
 *   acquisition.
 *
 * Shared by the gtest suites and the daemon's own, which is why nothing here
 * reaches outside libxrpl: xrpl_tests links only xrpl.libxrpl, while Peer,
 * PeerSet and the protobuf reply builder are all xrpld. The xrpld half of this
 * helper is test::packetFor() in src/test/app/AcquireTestHelpers.h.
 */
struct DeepChain
{
    // nodes[d] is the deserialized node for depth d.
    std::vector<SHAMapTreeNodePtr> nodes;
    SHAMapHash rootHash;

    // The key whose path through the tree this chain spells out. Zero for a
    // fabricated chain, which therefore sits on branch 0 at every depth.
    uint256 pathKey;

    // The depth of the deepest node, which is the last one nodesBelowRoot() hands out.
    unsigned int deepestDepth{SHAMap::kLeafDepth};

    /**
     * The payload size of the leaf toLeaf() builds, which is the smallest a
     * SHAMap item may be. Published so a caller can relate it to a threshold of
     * its own, as AcquireTestHelpers.h does.
     */
    static constexpr std::size_t kLeafItemBytes = kMinShaMapItemBytes;

    /**
     * A chain of inner nodes reaching SHAMap::kLeafDepth, which no valid tree
     * can hold.
     *
     * @param seed Varies the whole chain, so two chains can coexist without one
     *        resolving the other's nodes. Caches and fetch packs are keyed by
     *        hash, so identically-seeded chains are the same chain.
     */
    explicit DeepChain(unsigned int seed = 1) : DeepChain(std::nullopt, seed, Decoy::No)
    {
    }

    /**
     * The same chain, with a second and unresolvable child at every level.
     *
     * On a backed map descendAsync() then posts a real asynchronous read at
     * every level, which is what leaves reads in flight when a walk reaches
     * kLeafDepth. Offered only for this shape: the decoy sits on branch 1,
     * which is free only because a fabricated chain's pathKey is zero and
     * so every real child sits on branch 0.
     *
     * @param seed Varies the whole chain. See the constructor.
     * @return The chain.
     */
    [[nodiscard]] static DeepChain
    withDecoys(unsigned int seed = 1)
    {
        return DeepChain{std::nullopt, seed, Decoy::Yes};
    }

    /**
     * A chain ending in a real transaction leaf, which completes an
     * acquisition.
     *
     * @param depth Where the leaf sits, at most SHAMap::kLeafDepth. Zero puts
     *        the leaf at the root. Deeper is sparser than a one-transaction set
     *        would really be, but the sync path judges nodes by their hashes
     *        rather than by how sparse they are.
     * @param seed Varies the leaf's contents, and so the whole chain. See the
     *        constructor.
     * @return The chain.
     */
    [[nodiscard]] static DeepChain
    toLeaf(unsigned int depth, unsigned int seed = 1)
    {
        return DeepChain{std::optional{depth}, seed, Decoy::No};
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
     * Reports rather than asserts, since this header is shared with a binary
     * that has no test framework of its own to assert through.
     *
     * @param map The map to fill.
     * @return Whether the root and every node above the deepest one was
     *         accepted - a property of the chain, not of the map.
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
     * SHAMap::kLeafDepth that no valid tree can hold.
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
    // Whether each level carries a second child that is never stored anywhere.
    enum class Decoy { No, Yes };

    /**
     * Build any of the shapes.
     *
     * @param leafDepth Where a real transaction leaf sits, or nullopt to run
     *        inner nodes all the way to SHAMap::kLeafDepth instead.
     * @param seed Varies the chain's contents. See the public entry points.
     * @param decoy Whether every level carries an unresolvable second child.
     */
    DeepChain(std::optional<unsigned int> leafDepth, unsigned int seed, Decoy decoy)
        : nodes(leafDepth.value_or(SHAMap::kLeafDepth) + 1)
    {
        if (!leafDepth)
        {
            // No leaf, so the deepest inner node points at a child that is never fetched.
            buildInnersDownTo(SHAMap::kLeafDepth, SHAMapHash{uint256{seed}}, decoy);
            return;
        }

        // Exactly kLeafItemBytes of payload, the smallest a leaf item may be, which keeps the leaf
        // below the size at which a receiver tries to parse one as a transaction. Checked rather
        // than assumed, since a caller relates that constant to a threshold of its own.
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

        // A transaction leaf's key is the hash of its own contents, so the chain above it
        // has no say in where it sits: it has to follow this key's nibbles.
        pathKey = leafKey(*leaf);
        deepestDepth = *leafDepth;
        nodes[*leafDepth] = leaf;

        if (*leafDepth == 0)
        {
            // The leaf is the root, so there is nothing above it to build.
            rootHash = leaf->getHash();
            return;
        }

        buildInnersDownTo(*leafDepth - 1, leaf->getHash(), decoy);
    }

    /**
     * Fill in inner nodes, each with one real child (and, under Decoy::Yes, an
     * additional unresolvable second child), from the root down to the given depth,
     * and record the root hash.
     *
     * Bottom-up, since each node's hash covers the child hash below it.
     *
     * @param deepest The depth of the deepest inner node to build. May be
     *        SHAMap::kLeafDepth, which is the fabricated chain's whole point.
     * @param childHash What that deepest inner node points at.
     * @param decoy Whether to add an unresolvable second child at every level.
     */
    void
    buildInnersDownTo(unsigned int deepest, SHAMapHash childHash, Decoy decoy)
    {
        for (auto depth = deepest + 1; depth-- > 0;)
        {
            // A key has only 64 nibbles, so selectBranch() at SHAMap::kLeafDepth would index one
            // byte past the end of the 32-byte key. A fabricated chain's pathKey is zero, so
            // branch 0 is the position such a node claims anyway.
            auto const branch =
                depth == SHAMap::kLeafDepth ? 0u : selectBranch(idAt(depth), pathKey);

            Serializer s;
            s.addBitString(childHash.asUInt256());
            s.add8(static_cast<unsigned char>(branch));

            if (decoy == Decoy::Yes)
            {
                // The decoy sits at branch 1, which only stays free of the real child because
                // every caller that passes Decoy::Yes leaves pathKey at its default of zero. If a
                // caller ever combined a non-zero pathKey with a decoy, the compressed-inner-node
                // parser would silently let the decoy overwrite the real child's hash instead of
                // rejecting the duplicate branch, so guard the assumption rather than rely on it.
                if (branch == 1)
                    Throw<std::logic_error>("DeepChain: decoy branch collides with real child");

                // Derived from the depth so it differs per level - each posts its own read - and
                // is deterministic and cannot collide with a real node hash.
                uint256 decoyHash;
                decoyHash.begin()[0] = 0xDE;
                decoyHash.begin()[1] = 0xC0;
                decoyHash.begin()[2] = static_cast<unsigned char>(depth);
                s.addBitString(decoyHash);
                s.add8(1);  // the unresolvable decoy sits at branch 1
            }

            s.add8(kWireTypeCompressedInner);

            auto node = SHAMapTreeNode::makeFromWire(makeSlice(s.peekData()));
            childHash = node->getHash();
            nodes[depth] = std::move(node);
        }

        rootHash = childHash;
    }
};

}  // namespace xrpl::tests
