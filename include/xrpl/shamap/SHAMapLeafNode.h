#pragma once

#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <cstdint>

namespace xrpl {

/** Abstract base class for all SHAMap leaf nodes.
 *
 *  Provides shared item storage, copy-on-write mutation, and identity queries
 *  for the three concrete leaf types: `SHAMapAccountStateLeafNode`,
 *  `SHAMapTxLeafNode`, and `SHAMapTxPlusMetaLeafNode`. Each concrete subclass
 *  supplies its own `updateHash()`, `serializeForWire()`, and
 *  `serializeWithPrefix()` implementations reflecting the distinct hash
 *  formulas and wire formats of account state, bare transactions, and
 *  transaction-plus-metadata respectively.
 *
 *  Copy construction and copy assignment are deleted; duplication is always
 *  explicit via the virtual `clone(cowid)` method, which produces an owned
 *  copy ready for mutation under copy-on-write semantics.
 *
 *  @note `item_` is `protected` rather than `private` so that concrete
 *      subclasses can access it directly in their inline `updateHash()` and
 *      `serialize*()` implementations, avoiding virtual-dispatch overhead in
 *      the hot hash-recomputation path.
 *
 *  @see SHAMapAccountStateLeafNode
 *  @see SHAMapTxLeafNode
 *  @see SHAMapTxPlusMetaLeafNode
 */
class SHAMapLeafNode : public SHAMapTreeNode
{
protected:
    /** The keyed payload carried by this leaf.
     *
     *  Deliberately `const`-qualified through the pointer: the item itself is
     *  immutable once created and may be shared safely across CoW snapshots.
     *  `setItem()` replaces this pointer; it never mutates the referent.
     */
    boost::intrusive_ptr<SHAMapItem const> item_;

    /** Construct a leaf and defer hash computation to the concrete subclass.
     *
     *  Each concrete subclass calls `updateHash()` immediately after invoking
     *  this constructor so the node is valid for insertion into the trie.
     *  Asserts `item->size() >= 12`; any well-formed XRPL serialized object is
     *  at least 12 bytes — shorter data indicates corruption or a caller error.
     *
     *  @param item  Non-null payload for the new leaf.
     *  @param cowid Copy-on-write owner ID of the creating `SHAMap` instance.
     */
    SHAMapLeafNode(boost::intrusive_ptr<SHAMapItem const> item, std::uint32_t cowid);

    /** Construct a leaf with a pre-computed hash.
     *
     *  Used by `clone()` and deserialization paths where the hash is already
     *  known, avoiding a redundant SHA-512 computation. The caller must ensure
     *  `hash` is consistent with `item`'s current content or the Merkle tree
     *  will be silently corrupted.
     *  Asserts `item->size() >= 12`.
     *
     *  @param item  Non-null payload for the new leaf.
     *  @param cowid Copy-on-write owner ID for the new node.
     *  @param hash  Pre-computed hash of the leaf; passed straight through to
     *      `SHAMapTreeNode` without recomputation.
     */
    SHAMapLeafNode(
        boost::intrusive_ptr<SHAMapItem const> item,
        std::uint32_t cowid,
        SHAMapHash const& hash);

public:
    SHAMapLeafNode(SHAMapLeafNode const&) = delete;
    SHAMapLeafNode&
    operator=(SHAMapLeafNode const&) = delete;

    /** Returns `true`; sealed here so all concrete leaf subclasses inherit it. */
    bool
    isLeaf() const final
    {
        return true;
    }

    /** Returns `false`; sealed here so all concrete leaf subclasses inherit it. */
    bool
    isInner() const final
    {
        return false;
    }

    /** Assert the node's fundamental invariants.
     *
     *  Checks that `hash_` is non-zero and `item_` is non-null. The `isRoot`
     *  argument is meaningful only for inner nodes and is silently ignored here.
     *
     *  @param isRoot Ignored for leaf nodes.
     */
    void
    invariants(bool isRoot = false) const final;

public:
    /** Return a non-owning reference to the stored item pointer.
     *
     *  Gives callers access to the item's key and payload without transferring
     *  ownership or bumping the reference count. The "peek" convention signals
     *  zero-cost, non-owning access throughout the XRPL codebase.
     *
     *  @return A `const` reference to the `intrusive_ptr` holding the leaf's
     *      item. The reference is valid for the lifetime of this node.
     */
    boost::intrusive_ptr<SHAMapItem const> const&
    peekItem() const;

    /** Replace the stored item and recompute this node's hash.
     *
     *  Asserts that `cowid_` is non-zero — the node must be exclusively owned
     *  by a `SHAMap` instance (i.e. previously unshared via `clone()`) before
     *  it may be mutated. After swapping the item, `updateHash()` is called;
     *  `dirtyUp()` in the caller is only necessary when this returns `true`.
     *
     *  @param i  The replacement item; must share the same key as the current
     *      item (cross-key swaps corrupt the trie position).
     *  @return `true` if the hash changed (the effective content of the leaf
     *      was modified); `false` if the new item produces the same hash as the
     *      old one, indicating a no-op update.
     */
    bool
    setItem(boost::intrusive_ptr<SHAMapItem const> i);

    /** Format this leaf's identity and payload summary as a human-readable string.
     *
     *  Appends the node type tag (txn, txn+md, or as), the item key, the hash,
     *  and the item size to the base class string produced by
     *  `SHAMapTreeNode::getString()`. Intended for debugging and logging only.
     *
     *  @param id The `SHAMapNodeID` identifying this node's trie position.
     *  @return A multi-line string describing the node.
     */
    std::string
    getString(SHAMapNodeID const&) const final;
};

}  // namespace xrpl
