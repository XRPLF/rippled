/** @file
 *  Implements the shared behavior of all SHAMap leaf node types.
 *
 *  `SHAMapLeafNode` is the abstract middle tier of a three-level hierarchy:
 *  `SHAMapTreeNode` (CoW identity, hash, refcounts) → `SHAMapLeafNode` (item
 *  ownership, mutation, debug formatting, invariant checking) → the three
 *  concrete types (`SHAMapTxLeafNode`, `SHAMapTxPlusMetaLeafNode`,
 *  `SHAMapAccountStateLeafNode`). Type-specific logic — hash prefix, wire
 *  format, and node-type identity — lives exclusively in those subclasses.
 */

#include <xrpl/shamap/SHAMapLeafNode.h>

#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapNodeID.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <boost/smart_ptr/intrusive_ptr.hpp>

#include <cstdint>
#include <string>
#include <utility>

namespace xrpl {

/** Construct a new leaf whose hash must be computed by the subclass.
 *
 *  Used when creating a leaf for the first time. The concrete subclass is
 *  expected to call `updateHash()` immediately after delegating to this
 *  constructor so that `hash_` reflects the stored item.
 *
 *  @param item  The ledger object to store; must carry at least 12 bytes of
 *      payload (protocol-level minimum for any meaningful serialized object).
 *  @param cowid Copy-on-Write owner ID. Non-zero indicates exclusive ownership
 *      by a particular `SHAMap` instance; zero means the node is shared and
 *      must not be mutated.
 */
SHAMapLeafNode::SHAMapLeafNode(boost::intrusive_ptr<SHAMapItem const> item, std::uint32_t cowid)
    : SHAMapTreeNode(cowid), item_(std::move(item))
{
    XRPL_ASSERT(
        item_->size() >= 12,
        "xrpl::SHAMapLeafNode::SHAMapLeafNode(boost::intrusive_ptr<"
        "SHAMapItem const>, std::uint32_t) : minimum input size");
}

/** Construct a leaf with a pre-computed hash, skipping hash recomputation.
 *
 *  Used during deserialization (`makeFromWire`, `makeFromPrefix`) and during
 *  `clone()` operations where the hash is already known. Passing the hash
 *  directly avoids a SHA-512 half computation.
 *
 *  @param item  The ledger object to store; must carry at least 12 bytes of
 *      payload.
 *  @param cowid Copy-on-Write owner ID (see the two-argument overload).
 *  @param hash  The pre-computed hash for this leaf. The caller is responsible
 *      for ensuring this matches `item`; no verification is performed here.
 */
SHAMapLeafNode::SHAMapLeafNode(
    boost::intrusive_ptr<SHAMapItem const> item,
    std::uint32_t cowid,
    SHAMapHash const& hash)
    : SHAMapTreeNode(cowid, hash), item_(std::move(item))
{
    XRPL_ASSERT(
        item_->size() >= 12,
        "xrpl::SHAMapLeafNode::SHAMapLeafNode(boost::intrusive_ptr<"
        "SHAMapItem const>, std::uint32_t, SHAMapHash const&) : minimum input "
        "size");
}

boost::intrusive_ptr<SHAMapItem const> const&
SHAMapLeafNode::peekItem() const
{
    return item_;
}

bool
SHAMapLeafNode::setItem(boost::intrusive_ptr<SHAMapItem const> item)
{
    XRPL_ASSERT(cowid_, "xrpl::SHAMapLeafNode::setItem : nonzero cowid");
    item_ = std::move(item);

    auto const oldHash = hash_;

    updateHash();

    return (oldHash != hash_);
}

/** Produce a human-readable diagnostic string for this leaf node.
 *
 *  Prepends position context from `SHAMapTreeNode::getString(id)`, then
 *  appends the concrete node type (resolved via the virtual `getType()`),
 *  the item's 256-bit key, the node hash, and the item payload size in bytes.
 *
 *  @param id  The tree position (depth + masked path prefix) of this node.
 *  @return    A multi-line string suitable for logging and debug output.
 */
std::string
SHAMapLeafNode::getString(SHAMapNodeID const& id) const
{
    std::string ret = SHAMapTreeNode::getString(id);

    auto const type = getType();

    if (type == SHAMapNodeType::TnTransactionNm)
    {
        ret += ",txn\n";
    }
    else if (type == SHAMapNodeType::TnTransactionMd)
    {
        ret += ",txn+md\n";
    }
    else if (type == SHAMapNodeType::TnAccountState)
    {
        ret += ",as\n";
    }
    else
    {
        ret += ",leaf\n";
    }

    ret += "  Tag=";
    ret += to_string(item_->key());
    ret += "\n  Hash=";
    ret += to_string(hash_);
    ret += "/";
    ret += std::to_string(item_->size());
    return ret;
}

/** Assert that this leaf is in a valid, fully-initialized state.
 *
 *  Checks that `hash_` is non-zero and `item_` is non-null. Sealed `final`
 *  here so that no concrete subclass can inadvertently weaken or bypass
 *  these universal leaf invariants.
 */
void
SHAMapLeafNode::invariants(bool) const
{
    XRPL_ASSERT(hash_.isNonZero(), "xrpl::SHAMapLeafNode::invariants : nonzero hash");
    XRPL_ASSERT(item_, "xrpl::SHAMapLeafNode::invariants : non-null item");
}

}  // namespace xrpl
