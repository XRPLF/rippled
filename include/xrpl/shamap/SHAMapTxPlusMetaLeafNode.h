#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapLeafNode.h>

namespace xrpl {

/** SHAMap leaf node for a transaction paired with its execution metadata.
 *
 *  Represents the canonical form of a transaction entry in a validated
 *  (closed) ledger's transaction map. Each node stores the transaction blob
 *  and the `TxMeta` blob describing exactly what the transaction did to the
 *  ledger state. Open-ledger transaction maps use the metadata-free sibling
 *  `SHAMapTxLeafNode` instead; the two types are structurally incompatible
 *  because their hash formulas differ, making Merkle roots from the two
 *  contexts intrinsically distinct.
 *
 *  Sits at the bottom of the inheritance chain:
 *  `SHAMapTreeNode` → `SHAMapLeafNode` → `SHAMapTxPlusMetaLeafNode`.
 *  This class is a stateless policy layer: all mutable state (`hash_`,
 *  `cowid_`, `item_`) lives in the base classes. The only behavior provided
 *  here is the type-specific hash formula, wire format, clone factory, and
 *  type tag.
 *
 *  Also inherits `CountedObject<SHAMapTxPlusMetaLeafNode>`, which wires the
 *  type into the global object telemetry system for diagnosing live-instance
 *  counts under memory pressure.
 *
 *  @see SHAMapLeafNode
 *  @see SHAMapTxLeafNode
 *  @see SHAMapAccountStateLeafNode
 */
class SHAMapTxPlusMetaLeafNode final : public SHAMapLeafNode,
                                       public CountedObject<SHAMapTxPlusMetaLeafNode>
{
public:
    /** Construct a new transaction-plus-metadata leaf and compute its hash.
     *
     *  Use this constructor when creating a node from a freshly produced item.
     *  `updateHash()` is called immediately so the node is valid for insertion
     *  into the trie.
     *
     *  @param item  The serialized transaction-plus-metadata payload; must be
     *      non-null and at least 12 bytes.
     *  @param cowid Copy-on-write owner ID of the creating `SHAMap` instance.
     */
    SHAMapTxPlusMetaLeafNode(boost::intrusive_ptr<SHAMapItem const> item, std::uint32_t cowid)
        : SHAMapLeafNode(std::move(item), cowid)
    {
        updateHash();
    }

    /** Construct a transaction-plus-metadata leaf with a pre-computed hash.
     *
     *  Used by `clone()` and deserialization paths where the hash is already
     *  known, avoiding a redundant SHA-512 computation. The caller must ensure
     *  `hash` is consistent with `item`'s content or the Merkle tree will be
     *  silently corrupted.
     *
     *  @param item  The serialized transaction-plus-metadata payload; must be
     *      non-null and at least 12 bytes.
     *  @param cowid Copy-on-write owner ID for the new node.
     *  @param hash  Pre-computed hash of the leaf; passed straight through to
     *      `SHAMapLeafNode` without recomputation.
     */
    SHAMapTxPlusMetaLeafNode(
        boost::intrusive_ptr<SHAMapItem const> item,
        std::uint32_t cowid,
        SHAMapHash const& hash)
        : SHAMapLeafNode(std::move(item), cowid, hash)
    {
    }

    /** Produce an exclusively owned copy of this node for copy-on-write mutation.
     *
     *  Shares the existing `item_` pointer (no deep copy) and forwards the
     *  current `hash_` to avoid recomputation. The caller supplies the new
     *  `cowid` so the clone is immediately owned by the mutating `SHAMap`.
     *
     *  @param cowid Copy-on-write owner ID for the cloned node.
     *  @return A freshly allocated `SHAMapTxPlusMetaLeafNode` with the same
     *      item and hash, exclusively owned by `cowid`.
     */
    intr_ptr::SharedPtr<SHAMapTreeNode>
    clone(std::uint32_t cowid) const override
    {
        return intr_ptr::makeShared<SHAMapTxPlusMetaLeafNode>(item_, cowid, hash_);
    }

    /** Return the node type tag for transaction-plus-metadata leaves.
     *
     *  @return `SHAMapNodeType::TnTransactionMd`
     */
    SHAMapNodeType
    getType() const override
    {
        return SHAMapNodeType::TnTransactionMd;
    }

    /** Recompute and store this node's Merkle hash.
     *
     *  Hashes the `HashPrefix::TxNode` domain separator (`'S'`,`'N'`,`'D'`)
     *  followed by the raw payload slice and then the 32-byte item key, via
     *  `sha512Half`. Including the key is essential: unlike a bare transaction
     *  (whose ID is derived from the payload), a tx+meta node's key is an
     *  externally assigned identifier not present in the blob — omitting it
     *  would allow two distinct objects with identical payloads to collide.
     *
     *  The `HashPrefix::TxNode` separator is distinct from the
     *  `HashPrefix::TransactionId` used by `SHAMapTxLeafNode` and
     *  `HashPrefix::LeafNode` used by `SHAMapAccountStateLeafNode`, ensuring
     *  cross-context hash collisions are structurally impossible.
     *
     *  @note Marked `final` to signal that the hashing algorithm for this
     *      node type is fixed. `serializeWithPrefix()` must stay in sync with
     *      this formula; any drift silently corrupts hash verification.
     */
    void
    updateHash() final
    {
        hash_ = SHAMapHash{sha512Half(HashPrefix::TxNode, item_->slice(), item_->key())};
    }

    /** Serialize this node for peer-to-peer sync (wire format).
     *
     *  Writes the raw payload slice, then the 32-byte item key via
     *  `addBitString`, then the single-byte wire-type tag
     *  `kWIRE_TYPE_TRANSACTION_WITH_META` (`4`). The trailing tag allows
     *  `SHAMapTreeNode::makeFromWire()` to reconstruct the correct concrete
     *  leaf type on the receiving peer.
     *
     *  The key must be included on the wire because it does not appear in the
     *  payload — contrast with `SHAMapTxLeafNode::serializeForWire()`, which
     *  omits the key entirely and uses wire-type `0`.
     *
     *  @param s Serializer to append to.
     */
    void
    serializeForWire(Serializer& s) const final
    {
        s.addRaw(item_->slice());
        s.addBitString(item_->key());
        s.add8(kWIRE_TYPE_TRANSACTION_WITH_META);
    }

    /** Serialize this node in the canonical hashing format.
     *
     *  Writes the 4-byte `HashPrefix::TxNode` domain separator followed by
     *  the raw payload slice and the 32-byte item key. This matches exactly
     *  the input fed to `sha512Half` in `updateHash()` and is used during
     *  Merkle proof verification to reconstruct a hash from raw data without
     *  instantiating a full node object. No wire-type tag is appended because
     *  the hash prefix already encodes the node type.
     *
     *  @param s Serializer to append to.
     */
    void
    serializeWithPrefix(Serializer& s) const final
    {
        s.add32(HashPrefix::TxNode);
        s.addRaw(item_->slice());
        s.addBitString(item_->key());
    }
};

}  // namespace xrpl
