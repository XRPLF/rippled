#include <xrpl/shamap/SHAMapNodeID.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/shamap/SHAMap.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>

namespace xrpl {

static uint256 const&
depthMask(unsigned int depth)
{
    static constexpr auto kMaskSize = SHAMap::kLeafDepth + 1;

    struct MasksT
    {
        uint256 entry[kMaskSize];

        MasksT()
        {
            uint256 selector;
            for (auto i = 0u; i < kMaskSize - 1; i += 2)
            {
                entry[i] = selector;
                *(selector.begin() + (i / 2)) = 0xF0;
                entry[i + 1] = selector;
                *(selector.begin() + (i / 2)) = 0xFF;
            }
            entry[kMaskSize - 1] = selector;
        }
    };

    static MasksT const kMasks;
    return kMasks.entry[depth];
}

// The prefix of `key` at `depth`: the leading nibbles naming the subtree a node at that depth
// identifies, with the remainder of the key masked off.
static uint256
maskedToDepth(uint256 const& key, unsigned int depth)
{
    return key & depthMask(depth);
}

// Whether `id` at `depth` is what `key` looks like once masked down to that depth, i.e.
// whether an ID with this depth and id names a subtree that `key` falls under.
static bool
isPrefixOfAtDepth(uint256 const& id, unsigned int depth, uint256 const& key)
{
    return maskedToDepth(key, depth) == id;
}

// canonicalize the hash to a node ID for this depth
SHAMapNodeID::SHAMapNodeID(unsigned int depth, uint256 const& hash) : id_(hash), depth_(depth)
{
    // Every SHAMapNodeID's depth is stored here, so this is the one place that can stop an
    // out-of-range one from being kept: a depth past kLeafDepth would go on to index depthMask
    // out of bounds, and getRawString would narrow it to a byte, silently renaming the node.
    // Clamp rather than throw, since node IDs are built from peer-supplied depths on the ledger
    // data path, where no caller catches an exception before it reaches a thread boundary.
    if (depth_ > SHAMap::kLeafDepth)
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::SHAMapNodeID::SHAMapNodeID : depth within tree");
        depth_ = SHAMap::kLeafDepth;
        id_ = maskedToDepth(id_, depth_);
        // LCOV_EXCL_STOP
    }

    XRPL_ASSERT(
        depth <= SHAMap::kLeafDepth, "xrpl::SHAMapNodeID::SHAMapNodeID : maximum depth input");
    XRPL_ASSERT(
        isPrefixOf(id_), "xrpl::SHAMapNodeID::SHAMapNodeID : hash and depth inputs do match");
}

std::string
SHAMapNodeID::getRawString() const
{
    Serializer s(33);
    s.addBitString(id_);
    s.add8(depth_);
    return s.getString();
}

SHAMapNodeID
SHAMapNodeID::getChildNodeID(unsigned int branch) const
{
    XRPL_ASSERT(
        branch < SHAMap::kBranchFactor, "xrpl::SHAMapNodeID::getChildNodeID : valid branch input");

    // A SHAMap has exactly 65 levels, so nodes must not exceed that
    // depth; if they do, this breaks the invariant of never allowing
    // the construction of a SHAMapNodeID at an invalid depth. We assert
    // to catch this in debug builds.
    //
    // We throw (but never assert) if the node is at level 64, since
    // entries at that depth are leaf nodes and have no children and even
    // constructing a child node from them would break the above invariant.
    XRPL_ASSERT(
        depth_ <= SHAMap::kLeafDepth, "xrpl::SHAMapNodeID::getChildNodeID : maximum leaf depth");

    if (depth_ >= SHAMap::kLeafDepth)
        Throw<std::logic_error>("Request for child node ID of " + to_string(*this));

    if (!isPrefixOf(id_))
        Throw<std::logic_error>("Incorrect mask for " + to_string(*this));

    SHAMapNodeID node{depth_ + 1, id_};
    node.id_.begin()[depth_ / 2] |= ((depth_ & 1) != 0u) ? branch : (branch << 4);
    return node;
}

bool
SHAMapNodeID::isPrefixOf(uint256 const& key) const
{
    return isPrefixOfAtDepth(id_, depth_, key);
}

[[nodiscard]] std::optional<SHAMapNodeID>
deserializeSHAMapNodeID(void const* data, std::size_t size)
{
    std::optional<SHAMapNodeID> ret;

    if (size == 33)
    {
        unsigned int const depth = *(static_cast<unsigned char const*>(data) + 32);
        if (depth <= SHAMap::kLeafDepth)
        {
            // Reject a serialized ID carrying bits below its own depth. Checked before
            // constructing, since the constructor asserts that same property.
            if (auto const id = uint256::fromVoid(data); isPrefixOfAtDepth(id, depth, id))
                ret.emplace(depth, id);
        }
    }

    return ret;
}

[[nodiscard]] unsigned int
selectBranch(SHAMapNodeID const& id, uint256 const& hash)
{
    XRPL_ASSERT(id.getDepth() < SHAMap::kLeafDepth, "xrpl::selectBranch : depth below leaf depth");

    // A depth-64 ID has no nibble left to select. Callers must not ask, but clamp anyway to keep
    // the read below the end of the 32-byte key.
    auto const depth = std::min(id.getDepth(), SHAMap::kLeafDepth - 1u);
    auto branch = static_cast<unsigned int>(*(hash.begin() + (depth / 2)));

    if ((depth & 1) != 0u)
    {
        branch &= 0xf;
    }
    else
    {
        branch >>= 4;
    }

    XRPL_ASSERT(branch < SHAMap::kBranchFactor, "xrpl::selectBranch : maximum result");
    return branch;
}

SHAMapNodeID
SHAMapNodeID::createID(unsigned int depth, uint256 const& key)
{
    // The mask is chosen here, before the constructor runs, so the clamp there cannot cover this
    // call: an out-of-range depth would index depthMask's table while still evaluating this
    // argument. A public factory has to hold its own bound.
    if (depth > SHAMap::kLeafDepth)
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::SHAMapNodeID::createID : depth within tree");
        depth = SHAMap::kLeafDepth;
        // LCOV_EXCL_STOP
    }

    return SHAMapNodeID(depth, maskedToDepth(key, depth));
}

}  // namespace xrpl
