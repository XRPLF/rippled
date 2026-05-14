#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapLeafNode.h>

namespace xrpl {

/** SHAMap leaf node for a bare transaction without metadata.
 *
 *  Used when building the transaction tree of an open or proposed ledger,
 *  before execution metadata is available. One of three concrete leaf types
 *  alongside `SHAMapTxPlusMetaLeafNode` (transaction + metadata) and
 *  `SHAMapAccountStateLeafNode` (ledger state); each encodes type-specific
 *  hashing and serialization rules statically via virtual dispatch,
 *  eliminating runtime branching in the hot path.
 *
 *  Critically, the Merkle hash does **not** include the item key. A bare
 *  transaction's ID is itself derived from `sha512Half(prefix, blob)`, so
 *  the key is redundant — the hash fully characterises the content without
 *  it. This contrasts with `SHAMapTxPlusMetaLeafNode` and
 *  `SHAMapAccountStateLeafNode`, which must include the key because their
 *  keys are externally assigned identifiers that do not appear in the blob.
 *
 *  All mutable state lives in the base classes. This class is a stateless
 *  policy layer: it supplies only the hash formula, clone factory, type
 *  tag, and wire format for no-metadata transaction leaves.
 *
 *  @see SHAMapLeafNode
 *  @see SHAMapTxPlusMetaLeafNode
 *  @see SHAMapAccountStateLeafNode
 */
class SHAMapTxLeafNode final : public SHAMapLeafNode, public CountedObject<SHAMapTxLeafNode>
{
public:
    /** Construct a new bare-transaction leaf and compute its hash.
     *
     *  Use this constructor when creating a brand-new node from a freshly
     *  produced item. `updateHash()` is called immediately so the node is
     *  valid for insertion into the trie.
     *
     *  @param item  The raw transaction payload; must be non-null.
     *  @param cowid Copy-on-write owner ID of the creating SHAMap instance.
     */
    SHAMapTxLeafNode(boost::intrusive_ptr<SHAMapItem const> item, std::uint32_t cowid)
        : SHAMapLeafNode(std::move(item), cowid)
    {
        updateHash();
    }

    /** Construct a bare-transaction leaf with a pre-computed hash.
     *
     *  Used by `clone()` when the underlying item has not changed: forwarding
     *  the existing hash avoids a redundant SHA-512 computation.
     *
     *  @param item  The raw transaction payload; must be non-null.
     *  @param cowid Copy-on-write owner ID for the new node.
     *  @param hash  Known hash of `item`; must be consistent with the item's
     *      current content or the Merkle tree will be corrupted.
     */
    SHAMapTxLeafNode(
        boost::intrusive_ptr<SHAMapItem const> item,
        std::uint32_t cowid,
        SHAMapHash const& hash)
        : SHAMapLeafNode(std::move(item), cowid, hash)
    {
    }

    /** Produce an exclusively owned copy of this node for copy-on-write mutation.
     *
     *  The new node shares the same item and hash as the original — no
     *  recomputation occurs. The caller supplies the new `cowid` so the clone
     *  is immediately owned by the mutating SHAMap.
     *
     *  @param cowid Copy-on-write owner ID for the cloned node.
     *  @return A freshly allocated `SHAMapTxLeafNode` with the same item and
     *      hash, owned exclusively by `cowid`.
     */
    intr_ptr::SharedPtr<SHAMapTreeNode>
    clone(std::uint32_t cowid) const final
    {
        return intr_ptr::makeShared<SHAMapTxLeafNode>(item_, cowid, hash_);
    }

    /** Return the node type tag for bare-transaction leaves.
     *
     *  @return `SHAMapNodeType::TnTransactionNm`
     */
    SHAMapNodeType
    getType() const final
    {
        return SHAMapNodeType::TnTransactionNm;
    }

    /** Recompute and store this node's Merkle hash.
     *
     *  Hashes the `HashPrefix::TransactionId` domain separator (`'T'`,`'X'`,`'N'`)
     *  followed by the raw transaction bytes via `sha512Half`. The item key is
     *  deliberately omitted: for a bare transaction, the transaction ID is
     *  itself `sha512Half(prefix, blob)`, so the key carries no information
     *  beyond what the payload already provides.
     *
     *  @note The hash formula differs from `SHAMapTxPlusMetaLeafNode::updateHash()`
     *      and `SHAMapAccountStateLeafNode::updateHash()`, which both include
     *      `item_->key()` because their keys are externally assigned and not
     *      derivable from the payload alone.
     */
    void
    updateHash() final
    {
        hash_ = SHAMapHash{sha512Half(HashPrefix::TransactionId, item_->slice())};
    }

    /** Serialize this node for peer-to-peer sync (wire format).
     *
     *  Writes the raw transaction bytes followed by the single-byte wire-type
     *  tag `kWIRE_TYPE_TRANSACTION` (`0`). The item key is not written because
     *  it is fully determined by the payload. The trailing tag allows
     *  `SHAMapTreeNode::makeFromWire()` to reconstruct the correct concrete
     *  leaf type on the receiving peer.
     *
     *  @param s Serializer to append to.
     */
    void
    serializeForWire(Serializer& s) const final
    {
        s.addRaw(item_->slice());
        s.add8(kWIRE_TYPE_TRANSACTION);
    }

    /** Serialize this node in the canonical hashing format.
     *
     *  Writes the 4-byte `HashPrefix::TransactionId` domain separator followed
     *  by the raw transaction bytes. This matches the input fed to `sha512Half`
     *  in `updateHash()` and is used for Merkle proof verification where the
     *  hash prefix already encodes the node type (no wire-type tag is appended).
     *  The item key is omitted for the same reason as in `updateHash()`.
     *
     *  @param s Serializer to append to.
     */
    void
    serializeWithPrefix(Serializer& s) const final
    {
        s.add32(HashPrefix::TransactionId);
        s.addRaw(item_->slice());
    }
};

}  // namespace xrpl
