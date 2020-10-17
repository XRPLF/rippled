//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_SHAMAP_SHAMAPNODEID_H_INCLUDED
#define RIPPLE_SHAMAP_SHAMAPNODEID_H_INCLUDED

#include <ripple/basics/CountedObject.h>
#include <ripple/basics/base_uint.h>
#include <optional>
#include <ostream>
#include <string>
#include <tuple>

namespace ripple {

/** Identifies a node inside a SHAMap */
class SHAMapNodeID : public CountedObject<SHAMapNodeID>
{
private:
    uint256 id_;
    unsigned int depth_ = 0;

public:
    SHAMapNodeID() = default;
    SHAMapNodeID(SHAMapNodeID const& other) = default;
    SHAMapNodeID(unsigned int depth, uint256 const& hash);

    SHAMapNodeID& operator=(SHAMapNodeID const& other) = default;

    bool
    isRoot() const
    {
        return depth_ == 0;
    }

    // Get the wire format (256-bit nodeID, 1-byte depth)
    std::string
    getRawString() const;

    unsigned int
    getDepth() const
    {
        return depth_;
    }

    uint256 const&
    getNodeID() const
    {
        return id_;
    }

    SHAMapNodeID
    getChildNodeID(unsigned int m) const;

    // FIXME-C++20: use spaceship and operator synthesis
    /** Comparison operators */
    bool
    operator<(SHAMapNodeID const& n) const
    {
        return std::tie(depth_, id_) < std::tie(n.depth_, n.id_);
    }

    bool
    operator>(SHAMapNodeID const& n) const
    {
        return n < *this;
    }

    bool
    operator<=(SHAMapNodeID const& n) const
    {
        return !(n < *this);
    }

    bool
    operator>=(SHAMapNodeID const& n) const
    {
        return !(*this < n);
    }

    bool
    operator==(SHAMapNodeID const& n) const
    {
        return (depth_ == n.depth_) && (id_ == n.id_);
    }

    bool
    operator!=(SHAMapNodeID const& n) const
    {
        return !(*this == n);
    }
};

inline std::string
to_string(SHAMapNodeID const& node)
{
    if (node.isRoot())
        return "NodeID(root)";

    return "NodeID(" + std::to_string(node.getDepth()) + "," +
        to_string(node.getNodeID()) + ")";
}

inline std::ostream&
operator<<(std::ostream& out, SHAMapNodeID const& node)
{
    return out << to_string(node);
}

/** Return an object representing a serialized SHAMap Node ID
 *
 * @param s A string of bytes
 * @param data a non-null pointer to a buffer of @param size bytes.
 * @param size the size, in bytes, of the buffer pointed to by @param data.
 * @return A seated optional if the buffer contained a serialized SHAMap
 *         node ID and an unseated optional otherwise.
 */
/** @{ */
[[nodiscard]] std::optional<SHAMapNodeID>
deserializeSHAMapNodeID(void const* data, std::size_t size);

[[nodiscard]] inline std::optional<SHAMapNodeID>
deserializeSHAMapNodeID(std::string const& s)
{
    return deserializeSHAMapNodeID(s.data(), s.size());
}
/** @} */

/** Returns the branch that would contain the given hash */
[[nodiscard]] unsigned int
selectBranch(SHAMapNodeID const& id, uint256 const& hash);

}  // namespace ripple

#endif
