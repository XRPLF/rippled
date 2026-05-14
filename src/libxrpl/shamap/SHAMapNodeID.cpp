#include <xrpl/shamap/SHAMapNodeID.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/shamap/SHAMap.h>

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>

namespace xrpl {

/** Return the path-prefix bitmask for a given tree depth.
 *
 *  The SHAMap tree consumes one 4-bit nibble of a 256-bit key per level,
 *  so the meaningful prefix at depth @p depth is exactly the top `4 * depth`
 *  bits of a `uint256`.  This function returns a precomputed mask with those
 *  bits set to 1 and all lower bits set to 0, indexed by depth [0, 64].
 *
 *  The table is built once at program start inside a function-local `static`:
 *  - depth 0 (root): all-zero mask — the root carries no path prefix.
 *  - depth 1: high nibble of byte 0 set (`0xF0______...`).
 *  - depth 2: full first byte set (`0xFF______...`).
 *  - Each subsequent pair of depths adds one nibble more.
 *  - depth 64 (leaf): all 32 bytes fully set.
 *
 *  @param depth Tree depth in [0, 64].  Caller is responsible for range
 *      validity; out-of-range access is undefined behaviour.
 *  @return Reference to the precomputed mask for @p depth.
 *  @note Thread-safe: the `static` local is initialised exactly once by the
 *      C++11 guarantee; no explicit synchronisation is required.
 */
static uint256 const&
depthMask(unsigned int depth)
{
    static constexpr auto kMASK_SIZE = 65;

    struct MasksT
    {
        uint256 entry[kMASK_SIZE];

        MasksT()
        {
            uint256 selector;
            for (int i = 0; i < kMASK_SIZE - 1; i += 2)
            {
                entry[i] = selector;
                *(selector.begin() + (i / 2)) = 0xF0;
                entry[i + 1] = selector;
                *(selector.begin() + (i / 2)) = 0xFF;
            }
            entry[kMASK_SIZE - 1] = selector;
        }
    };

    static MasksT const kMASKS;
    return kMASKS.entry[depth];
}

SHAMapNodeID::SHAMapNodeID(unsigned int depth, uint256 const& hash) : id_(hash), depth_(depth)
{
    XRPL_ASSERT(
        depth <= SHAMap::kLEAF_DEPTH, "xrpl::SHAMapNodeID::SHAMapNodeID : maximum depth input");
    XRPL_ASSERT(
        id_ == (id_ & depthMask(depth)),
        "xrpl::SHAMapNodeID::SHAMapNodeID : hash and depth inputs do match");
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
SHAMapNodeID::getChildNodeID(unsigned int m) const
{
    XRPL_ASSERT(
        m < SHAMap::kBRANCH_FACTOR, "xrpl::SHAMapNodeID::getChildNodeID : valid branch input");
    XRPL_ASSERT(
        depth_ <= SHAMap::kLEAF_DEPTH, "xrpl::SHAMapNodeID::getChildNodeID : maximum leaf depth");

    if (depth_ >= SHAMap::kLEAF_DEPTH)
        Throw<std::logic_error>("Request for child node ID of " + to_string(*this));

    if (id_ != (id_ & depthMask(depth_)))
        Throw<std::logic_error>("Incorrect mask for " + to_string(*this));

    SHAMapNodeID node{depth_ + 1, id_};
    node.id_.begin()[depth_ / 2] |= ((depth_ & 1) != 0u) ? m : (m << 4);
    return node;
}

[[nodiscard]] std::optional<SHAMapNodeID>
deserializeSHAMapNodeID(void const* data, std::size_t size)
{
    std::optional<SHAMapNodeID> ret;

    if (size == 33)
    {
        unsigned int const depth = *(static_cast<unsigned char const*>(data) + 32);
        if (depth <= SHAMap::kLEAF_DEPTH)
        {
            auto const id = uint256::fromVoid(data);

            if (id == (id & depthMask(depth)))
                ret.emplace(depth, id);
        }
    }

    return ret;
}

[[nodiscard]] unsigned int
selectBranch(SHAMapNodeID const& id, uint256 const& hash)
{
    auto const depth = id.getDepth();
    auto branch = static_cast<unsigned int>(*(hash.begin() + (depth / 2)));

    if ((depth & 1) != 0u)
    {
        branch &= 0xf;
    }
    else
    {
        branch >>= 4;
    }

    XRPL_ASSERT(branch < SHAMap::kBRANCH_FACTOR, "xrpl::selectBranch : maximum result");
    return branch;
}

SHAMapNodeID
SHAMapNodeID::createID(int depth, uint256 const& key)
{
    XRPL_ASSERT((depth >= 0) && (depth < 65), "xrpl::SHAMapNodeID::createID : valid branch input");
    return SHAMapNodeID(depth, key & depthMask(depth));
}

}  // namespace xrpl
