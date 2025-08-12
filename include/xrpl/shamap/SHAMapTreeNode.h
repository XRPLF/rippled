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

#ifndef RIPPLE_SHAMAP_SHAMAPTREENODE_H_INCLUDED
#define RIPPLE_SHAMAP_SHAMAPTREENODE_H_INCLUDED

#include <xrpl/basics/IntrusivePointer.h>
#include <xrpl/basics/IntrusiveRefCounts.h>
#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapNodeID.h>

#include <cstdint>
#include <string>

namespace ripple {

// These are wire-protocol identifiers used during serialization to encode the
// type of a node. They should not be arbitrarily be changed.
static constexpr unsigned char const wireTypeTransaction = 0;
static constexpr unsigned char const wireTypeAccountState = 1;
static constexpr unsigned char const wireTypeInner = 2;
static constexpr unsigned char const wireTypeCompressedInner = 3;
static constexpr unsigned char const wireTypeTransactionWithMeta = 4;

enum class SHAMapNodeType {
    tnINNER = 1,
    tnTRANSACTION_NM = 2,  // transaction, no metadata
    tnTRANSACTION_MD = 3,  // transaction, with metadata
    tnACCOUNT_STATE = 4
};

class SHAMapTreeNode : public IntrusiveRefCounts
{
protected:
    SHAMapHash hash_;

    /** Determines the owning SHAMap, if any. Used for copy-on-write semantics.

        If this value is 0, the node is not dirty and does not need to be
        flushed. It is eligible for sharing and may be included multiple
        SHAMap instances.
     */
    std::uint32_t cowid_;

protected:
    SHAMapTreeNode(SHAMapTreeNode const&) = delete;
    SHAMapTreeNode&
    operator=(SHAMapTreeNode const&) = delete;

    /** Construct a node

        @param cowid The identifier of a SHAMap. For more, see #cowid_
        @param hash The hash associated with this node, if any.
     */
    /** @{ */
    explicit SHAMapTreeNode(std::uint32_t cowid) noexcept : cowid_(cowid)
    {
    }

    explicit SHAMapTreeNode(
        std::uint32_t cowid,
        SHAMapHash const& hash) noexcept
        : hash_(hash), cowid_(cowid)
    {
    }
    /** @} */

public:
    virtual ~SHAMapTreeNode() noexcept = default;

    // Needed to support weak intrusive pointers
    virtual void
    partialDestructor() {};

    /** \defgroup SHAMap Copy-on-Write Support

        By nature, a node may appear in multiple SHAMap instances. Rather
       than actually duplicating these nodes, SHAMap opts to be memory
       efficient and uses copy-on-write semantics for nodes.

        Only nodes that are not modified and don't need to be flushed back
       can be shared. Once a node needs to be changed, it must first be
       copied and the copy must marked as not shareable.

        Note that just because a node may not be *owned* by a given SHAMap
        instance does not mean that the node is NOT a part of any SHAMap. It
        only means that the node is not owned exclusively by any one SHAMap.

        For more on copy-on-write, check out:
            https://en.wikipedia.org/wiki/Copy-on-write
     */
    /** @{ */
    /** Returns the SHAMap that owns this node.

        @return the ID of the SHAMap that owns this node, or 0 if the
       node is not owned by any SHAMap and is a candidate for sharing.
     */
    std::uint32_t
    cowid() const
    {
        return cowid_;
    }

    /** If this node is shared with another map, mark it as no longer shared.

        Only nodes that are not modified and do not need to be flushed back
        should be marked as unshared.
     */
    void
    unshare()
    {
        cowid_ = 0;
    }

    /** Make a copy of this node, setting the owner. */
    virtual intr_ptr::SharedPtr<SHAMapTreeNode>
    clone(std::uint32_t cowid) const = 0;
    /** @} */

    /** Recalculate the hash of this node. */
    virtual void
    updateHash() = 0;

    /** Return the hash of this node. */
    SHAMapHash const&
    getHash() const
    {
        return hash_;
    }

    /** Determines the type of node. */
    virtual SHAMapNodeType
    getType() const = 0;

    /** Determines if this is a leaf node. */
    virtual bool
    isLeaf() const = 0;

    /** Determines if this is an inner node. */
    virtual bool
    isInner() const = 0;

    /** Serialize the node in a format appropriate for sending over the wire */
    virtual void
    serializeForWire(Serializer&) const = 0;

    /** Serialize the node in a format appropriate for hashing */
    virtual void
    serializeWithPrefix(Serializer&) const = 0;

    virtual std::string
    getString(SHAMapNodeID const&) const;

    virtual void
    invariants(bool is_root = false) const = 0;

    static intr_ptr::SharedPtr<SHAMapTreeNode>
    makeFromPrefix(Slice rawNode, SHAMapHash const& hash);

    static intr_ptr::SharedPtr<SHAMapTreeNode>
    makeFromWire(Slice rawNode);

private:
    static intr_ptr::SharedPtr<SHAMapTreeNode>
    makeTransaction(Slice data, SHAMapHash const& hash, bool hashValid);

    static intr_ptr::SharedPtr<SHAMapTreeNode>
    makeAccountState(Slice data, SHAMapHash const& hash, bool hashValid);

    static intr_ptr::SharedPtr<SHAMapTreeNode>
    makeTransactionWithMeta(Slice data, SHAMapHash const& hash, bool hashValid);
};

}  // namespace ripple

#endif
