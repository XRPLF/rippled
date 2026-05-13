#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapLeafNode.h>

namespace xrpl {

/** SHAMap leaf node holding a serialized ledger state object.
 *
 *  Represents account roots, offers, trust lines, and other ledger objects
 *  in the SHAMap state trie. One of three concrete leaf types alongside
 *  `SHAMapTxLeafNode` and `SHAMapTxPlusMetaLeafNode`; each encodes
 *  type-specific hashing and serialization rules statically via virtual
 *  dispatch, eliminating runtime branching in the hot path.
 *
 *  Unlike transaction leaves, the Merkle hash commits to both the payload
 *  and the item key (see `updateHash()`). This is required because state
 *  object keys (e.g. account address, offer index) are externally assigned
 *  and may not appear verbatim in the serialized blob — omitting the key
 *  would allow two distinct objects with identical payloads to collide.
 *
 *  All mutable state lives in the base classes. This class is a stateless
 *  policy layer: it supplies only the hash formula, clone factory, type
 *  tag, and wire format for account-state leaves.
 *
 *  @see SHAMapLeafNode
 *  @see SHAMapTxLeafNode
 *  @see SHAMapTxPlusMetaLeafNode
 */
class SHAMapAccountStateLeafNode final : public SHAMapLeafNode,
                                         public CountedObject<SHAMapAccountStateLeafNode>
{
public:
    /** Construct a new account-state leaf and compute its hash.
     *
     *  Use this constructor when creating a brand-new node from a freshly
     *  produced item. `updateHash()` is called immediately so the node is
     *  valid for insertion into the trie.
     *
     *  @param item  The ledger state object payload; must be non-null.
     *  @param cowid Copy-on-write owner ID of the creating SHAMap instance.
     */
    SHAMapAccountStateLeafNode(boost::intrusive_ptr<SHAMapItem const> item, std::uint32_t cowid)
        : SHAMapLeafNode(std::move(item), cowid)
    {
        updateHash();
    }

    /** Construct an account-state leaf with a pre-computed hash.
     *
     *  Used by `clone()` when the underlying item has not changed: forwarding
     *  the existing hash avoids a redundant SHA-512 computation.
     *
     *  @param item  The ledger state object payload; must be non-null.
     *  @param cowid Copy-on-write owner ID for the new node.
     *  @param hash  Known hash of `item`; must be consistent with the item's
     *      current content or the Merkle tree will be corrupted.
     */
    SHAMapAccountStateLeafNode(
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
     *  @return A freshly allocated `SHAMapAccountStateLeafNode` with the same
     *      item and hash, owned exclusively by `cowid`.
     */
    intr_ptr::SharedPtr<SHAMapTreeNode>
    clone(std::uint32_t cowid) const final
    {
        return intr_ptr::makeShared<SHAMapAccountStateLeafNode>(item_, cowid, hash_);
    }

    /** Return the node type tag for account-state leaves.
     *
     *  @return `SHAMapNodeType::TnAccountState`
     */
    SHAMapNodeType
    getType() const final
    {
        return SHAMapNodeType::TnAccountState;
    }

    /** Recompute and store this node's Merkle hash.
     *
     *  Hashes the `HashPrefix::LeafNode` domain separator (`'M'`,`'L'`,`'N'`),
     *  the raw item payload, and the item key together via `sha512Half`. The key
     *  is included because state object keys are externally assigned identifiers
     *  that bind the object to its trie position; without the key, two objects
     *  with identical serialized data would produce the same hash regardless of
     *  their position in the ledger.
     *
     *  @note The hash formula differs from `SHAMapTxLeafNode::updateHash()`,
     *      which omits the key because a transaction's ID is already derived
     *      from `sha512Half(prefix, blob)`.
     */
    void
    updateHash() final
    {
        hash_ = SHAMapHash{sha512Half(HashPrefix::LeafNode, item_->slice(), item_->key())};
    }

    /** Serialize this node for peer-to-peer sync (wire format).
     *
     *  Writes the raw item payload, then the item key as a fixed-width bit
     *  string, then the single-byte wire-type tag `kWIRE_TYPE_ACCOUNT_STATE`
     *  (`1`). The tag at the end allows `SHAMapTreeNode::makeFromWire()` to
     *  reconstruct the correct concrete leaf type on the receiving peer.
     *
     *  @param s Serializer to append to.
     */
    void
    serializeForWire(Serializer& s) const final
    {
        s.addRaw(item_->slice());
        s.addBitString(item_->key());
        s.add8(kWIRE_TYPE_ACCOUNT_STATE);
    }

    /** Serialize this node in the canonical hashing format.
     *
     *  Writes the 4-byte `HashPrefix::LeafNode` domain separator, the raw item
     *  payload, and the item key. This matches the input fed to `sha512Half` in
     *  `updateHash()` and is used for Merkle proof verification where the hash
     *  prefix already encodes the node type (no wire-type tag is appended).
     *
     *  @param s Serializer to append to.
     */
    void
    serializeWithPrefix(Serializer& s) const final
    {
        s.add32(HashPrefix::LeafNode);
        s.addRaw(item_->slice());
        s.addBitString(item_->key());
    }
};

}  // namespace xrpl
