#pragma once

#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/shamap/SHAMapInnerNode.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace xrpl::tests {

/**
 * One child of an inner node a test assembles by hand.
 */
struct InnerChild
{
    unsigned int branch{};
    SHAMapHash hash;
};

/**
 * Assemble an inner node in the wire format's full form.
 *
 * A full inner node is all 16 branch hashes back to back in branch order,
 * followed by the wire type byte. A branch absent from `children`, or given a
 * zero hash, yields an empty branch, since the parser derives which branches
 * exist from which hashes are non-zero. The node's hash is already correct,
 * since makeFromWire() computes it.
 *
 * @param children The branch and hash of each child to record. Throws if a
 *        branch is at or above SHAMapInnerNode::kBranchFactor, or if one
 *        branch is named twice.
 * @return The node, or nullptr if the bytes do not parse.
 */
[[nodiscard]] inline SHAMapTreeNodePtr
makeFullInnerNode(std::vector<InnerChild> const& children)
{
    // The full form carries a slot for every branch, so a child's position comes from the slot it
    // is written to.
    std::array<UInt256, SHAMapInnerNode::kBranchFactor> hashes{};

    // A duplicate branch is refused here, and one past the last to match the compressed parser.
    std::uint32_t seen = 0;
    for (auto const& child : children)
    {
        if (child.branch >= SHAMapInnerNode::kBranchFactor)
            Throw<std::logic_error>("makeFullInnerNode: branch is past the last one");

        auto const bit = 1u << child.branch;
        if ((seen & bit) != 0)
            Throw<std::logic_error>("makeFullInnerNode: branch named twice");
        seen |= bit;

        hashes.at(child.branch) = child.hash.asUInt256();
    }

    Serializer s;
    for (auto const& hash : hashes)
        s.addBitString(hash);
    s.add8(kWireTypeInner);

    return SHAMapTreeNode::makeFromWire(makeSlice(s.peekData()));
}

/**
 * Assemble an inner node in the wire format's compressed form.
 *
 * A compressed inner node is one 33-byte chunk per child, each a hash followed
 * by the branch it sits on, and then the wire type byte. Its hash is already
 * correct, for the same reason makeFullInnerNode()'s is.
 *
 * Nothing is checked here, unlike in makeFullInnerNode(). The branch travels as
 * one byte, so any value up to 255 reaches the parser, which refuses a branch
 * at or above SHAMapInnerNode::kBranchFactor and lets a repeated branch
 * overwrite the hash recorded for it.
 *
 * @param children The branch and hash of each child to record.
 * @return The node, or nullptr if the bytes do not parse.
 */
[[nodiscard]] inline SHAMapTreeNodePtr
makeCompressedInnerNode(std::vector<InnerChild> const& children)
{
    Serializer s;
    for (auto const& child : children)
    {
        s.addBitString(child.hash.asUInt256());
        s.add8(static_cast<unsigned char>(child.branch));
    }
    s.add8(kWireTypeCompressedInner);

    return SHAMapTreeNode::makeFromWire(makeSlice(s.peekData()));
}

}  // namespace xrpl::tests
