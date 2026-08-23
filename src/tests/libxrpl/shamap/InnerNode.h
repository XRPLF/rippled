#pragma once

#include <xrpl/basics/Blob.h>
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
    unsigned int branch;
    SHAMapHash hash;
};

/**
 * Serialize an inner node in the wire format's full form.
 *
 * A full inner node is all 16 branch hashes back to back in branch order,
 * followed by the wire type byte. A branch `children` does not name is left
 * zero.
 *
 * A zero hash and an absent branch are the same thing here, because the parser
 * derives which branches exist from which hashes are non-zero. So a child
 * cannot be given a zero hash, and asking for one yields an empty branch
 * instead.
 *
 * @param children The branch and hash of each child to record. Throws if a
 *        branch is at or above SHAMapInnerNode::kBranchFactor, or if one
 *        branch is named twice.
 * @return The serialized node, for SHAMapTreeNode::makeFromWire().
 */
[[nodiscard]] inline Blob
fullInnerBlob(std::vector<InnerChild> const& children)
{
    // Indexed rather than appended, since the full form carries a slot for every branch and takes
    // each child's position from the order the slots are written in.
    std::array<uint256, SHAMapInnerNode::kBranchFactor> hashes{};

    // Refused rather than left to the last writer, so a caller that names one branch twice learns
    // of it here instead of reading back a node holding only the second hash. The compressed form's
    // parser refuses an out-of-range branch, so the full form must not be the weaker of the two.
    std::uint32_t seen = 0;
    for (auto const& child : children)
    {
        if (child.branch >= SHAMapInnerNode::kBranchFactor)
            Throw<std::logic_error>("fullInnerBlob: branch is past the last one");

        auto const bit = 1u << child.branch;
        if ((seen & bit) != 0)
            Throw<std::logic_error>("fullInnerBlob: branch named twice");
        seen |= bit;

        hashes.at(child.branch) = child.hash.asUInt256();
    }

    Serializer s;
    for (auto const& hash : hashes)
        s.addBitString(hash);
    s.add8(kWireTypeInner);

    return s.getData();
}

/**
 * Assemble an inner node from what fullInnerBlob() produces.
 *
 * The node's hash is already correct: makeFromWire() reports the hash as
 * invalid, so the parser computes one. No caller needs updateHash().
 *
 * @param children As for fullInnerBlob().
 * @return The node, or nullptr if the bytes do not parse.
 */
[[nodiscard]] inline SHAMapTreeNodePtr
makeFullInnerNode(std::vector<InnerChild> const& children)
{
    auto const blob = fullInnerBlob(children);
    return SHAMapTreeNode::makeFromWire(makeSlice(blob));
}

/**
 * Assemble an inner node in the wire format's compressed form.
 *
 * A compressed inner node is one 33-byte chunk per child, each a hash followed
 * by the branch it sits on, and then the wire type byte. Its hash is already
 * correct, for the same reason makeFullInnerNode()'s is.
 *
 * The parser refuses a branch at or above SHAMapInnerNode::kBranchFactor, and
 * lets a repeated branch overwrite the hash recorded for it, so a caller
 * wanting either of those can ask for it here.
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
