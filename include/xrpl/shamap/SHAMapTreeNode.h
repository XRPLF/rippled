/** @file
 *  Base class for all nodes in the SHAMap authenticated Merkle radix tree,
 *  plus the wire-protocol type tags and the in-memory node-type enumeration.
 *
 *  Every node stored in a `SHAMap` — branching inner node or data-carrying
 *  leaf — derives from `SHAMapTreeNode`. This file is therefore the single
 *  authoritative location for the tree's on-disk and on-wire node format.
 */

#pragma once

#include <xrpl/basics/IntrusivePointer.h>
#include <xrpl/basics/IntrusiveRefCounts.h>
#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapNodeID.h>

#include <cstdint>
#include <string>

namespace xrpl {

/** Wire-protocol type tag for a bare transaction node (no metadata).
 *
 *  Appended as the final byte of a serialized node payload during peer-to-peer
 *  sync. Part of the XRPL wire protocol — do not change.
 */
static constexpr unsigned char const kWIRE_TYPE_TRANSACTION = 0;

/** Wire-protocol type tag for an account-state (ledger object) node.
 *
 *  Part of the XRPL wire protocol — do not change.
 */
static constexpr unsigned char const kWIRE_TYPE_ACCOUNT_STATE = 1;

/** Wire-protocol type tag for a full inner node (all 16 hashes emitted).
 *
 *  Part of the XRPL wire protocol — do not change.
 */
static constexpr unsigned char const kWIRE_TYPE_INNER = 2;

/** Wire-protocol type tag for a compressed inner node (sparse hash encoding).
 *
 *  Part of the XRPL wire protocol — do not change.
 */
static constexpr unsigned char const kWIRE_TYPE_COMPRESSED_INNER = 3;

/** Wire-protocol type tag for a transaction node that includes metadata.
 *
 *  Part of the XRPL wire protocol — do not change.
 */
static constexpr unsigned char const kWIRE_TYPE_TRANSACTION_WITH_META = 4;

/** In-memory classification of a SHAMap tree node.
 *
 *  Used for runtime dispatch and to select the correct hash prefix and wire
 *  format. Note that these values are distinct from the `kWIRE_TYPE_*`
 *  constants: the wire-type byte is appended to the serialized payload,
 *  while `SHAMapNodeType` lives only in memory.
 */
enum class SHAMapNodeType {
    TnInner = 1,
    TnTransactionNm = 2,  /**< Transaction leaf without metadata. */
    TnTransactionMd = 3,  /**< Transaction leaf with metadata. */
    TnAccountState = 4    /**< Account-state (ledger object) leaf. */
};

/** Polymorphic base for all nodes in the SHAMap authenticated Merkle radix tree.
 *
 *  Concrete subtypes are `SHAMapInnerNode` (the 16-way branching node) and the
 *  three leaf types: `SHAMapTxLeafNode`, `SHAMapTxPlusMetaLeafNode`, and
 *  `SHAMapAccountStateLeafNode`. All share the copy-on-write ownership model
 *  encoded in `cowid_` and the two serialization contracts defined here.
 *
 *  Nodes can be shared across multiple `SHAMap` instances simultaneously when
 *  `cowid_ == 0`. A map that needs to mutate a shared node must first call
 *  `clone()` to produce a private copy, then mutate only the clone.
 *
 *  Copy and move are deleted: duplication must always go through `clone()`.
 *
 *  @note This class inherits from `IntrusiveRefCounts`, which packs 16-bit
 *      strong count, 14-bit weak count, and two lifecycle-state bits into a
 *      single 32-bit atomic. Per-node overhead is intentionally minimal because
 *      a full ledger tree can contain millions of nodes.
 *  @see SHAMapInnerNode for the branching node implementation.
 *  @see SHAMapLeafNode for the abstract leaf base.
 */
class SHAMapTreeNode : public IntrusiveRefCounts
{
protected:
    /** Cached Merkle hash of this node.
     *
     *  Zero when the node is dirty (after a mutation, before `updateHash()` is
     *  called). `serializeWithPrefix` feeds this value into the parent's hash
     *  computation.
     */
    SHAMapHash hash_;

    /** Identifies which `SHAMap` instance exclusively owns this node.
     *
     *  A zero value means the node is clean and eligible for sharing across
     *  multiple `SHAMap` instances simultaneously. A non-zero value is the
     *  `cowid` of the one map that owns and may mutate this node.
     *
     *  `unshare()` resets this to zero, making the node shareable again (called
     *  during flush, after which the node is effectively immutable).
     */
    std::uint32_t cowid_;

    /** @{ */
    /** Construct a node with the given CoW owner and an unset hash.
     *
     *  @param cowid Copy-on-write identifier of the owning `SHAMap`; pass 0
     *      for a shareable (read-only) node.
     */
    explicit SHAMapTreeNode(std::uint32_t cowid) noexcept : cowid_(cowid)
    {
    }

    /** Construct a node with the given CoW owner and a pre-computed hash.
     *
     *  Used when deserializing from storage where the hash is already known,
     *  avoiding a redundant `updateHash()` call.
     *
     *  @param cowid Copy-on-write identifier of the owning `SHAMap`.
     *  @param hash  Pre-validated Merkle hash for this node.
     */
    explicit SHAMapTreeNode(std::uint32_t cowid, SHAMapHash const& hash) noexcept
        : hash_(hash), cowid_(cowid)
    {
    }
    /** @} */

public:
    ~SHAMapTreeNode() noexcept override = default;

    SHAMapTreeNode(SHAMapTreeNode const&) = delete;
    SHAMapTreeNode&
    operator=(SHAMapTreeNode const&) = delete;

    /** Release expensive sub-resources while weak references still exist.
     *
     *  Called by the `IntrusiveRefCounts` infrastructure when the strong
     *  reference count reaches zero but at least one weak reference remains.
     *  The default implementation is a no-op; `SHAMapInnerNode` overrides it
     *  to release all 16 child `SharedPtr`s promptly, avoiding a cascade of
     *  memory retention through the child tree while weak pointers keep this
     *  node's storage alive.
     *
     *  @note Callers must invoke `partialDestructorFinished()` after this
     *      returns, per the `IntrusiveRefCounts` contract.
     */
    virtual void
    partialDestructor() {};

    /** Return the copy-on-write identifier of the owning `SHAMap`.
     *
     *  A return value of 0 means the node is unowned and eligible for sharing
     *  across multiple `SHAMap` instances. A non-zero value identifies the one
     *  map that may mutate this node.
     *
     *  @return The owning map's CoW ID, or 0 if the node is shareable.
     */
    std::uint32_t
    cowid() const
    {
        return cowid_;
    }

    /** Mark this node as shareable by clearing its CoW ownership.
     *
     *  Sets `cowid_` to 0, making the node eligible for inclusion in multiple
     *  `SHAMap` snapshots simultaneously. Called during flush, after which the
     *  node is written to backing storage and must not be further mutated.
     *
     *  @note Only call this on nodes that are clean (do not need to be flushed)
     *      or have already been persisted.
     */
    void
    unshare()
    {
        cowid_ = 0;
    }

    /** Produce a deep copy of this node assigned to a new CoW epoch.
     *
     *  The returned node carries `cowid` as its owner, allowing the caller's
     *  `SHAMap` to mutate it independently of any other maps that still hold
     *  references to the original. The original node is left intact.
     *
     *  @param cowid Copy-on-write identifier of the map that will own the clone.
     *  @return A `SharedPtr` to the new, independently-owned copy.
     */
    virtual intr_ptr::SharedPtr<SHAMapTreeNode>
    clone(std::uint32_t cowid) const = 0;

    /** Recompute `hash_` from the node's current contents.
     *
     *  Each concrete subclass feeds the appropriate `HashPrefix` constant and
     *  payload into SHA-512/2. Must be called after any mutation before the
     *  node's hash is read by its parent.
     */
    virtual void
    updateHash() = 0;

    /** Return the cached Merkle hash of this node.
     *
     *  The hash is zero if the node is dirty (mutated since the last
     *  `updateHash()` call).
     *
     *  @return The current `SHAMapHash` for this node.
     */
    SHAMapHash const&
    getHash() const
    {
        return hash_;
    }

    /** Return the in-memory node type used for runtime dispatch.
     *
     *  @return One of `TnInner`, `TnTransactionNm`, `TnTransactionMd`, or
     *      `TnAccountState`.
     */
    virtual SHAMapNodeType
    getType() const = 0;

    /** Return whether this node is a leaf (data-carrying) node.
     *
     *  @return `true` for all three concrete leaf types; `false` for inner
     *      nodes.
     */
    virtual bool
    isLeaf() const = 0;

    /** Return whether this node is an inner (branching) node.
     *
     *  @return `true` for `SHAMapInnerNode`; `false` for all leaf types.
     */
    virtual bool
    isInner() const = 0;

    /** Serialize this node for peer-to-peer wire transmission.
     *
     *  Appends the node payload followed by a single `kWIRE_TYPE_*` type byte
     *  at the end of the buffer. The receiver reads the last byte to determine
     *  the node type before parsing the preceding payload.
     *
     *  @param s Serializer to append to.
     *  @see serializeWithPrefix for the hashing/database format.
     */
    virtual void
    serializeForWire(Serializer& s) const = 0;

    /** Serialize this node in canonical hash-input form.
     *
     *  Prepends a 4-byte `HashPrefix` constant before the node data, allowing
     *  the node type to be identified from the first four bytes. This format is
     *  used for Merkle hash computation and for database storage.
     *
     *  @param s Serializer to append to.
     *  @see serializeForWire for the wire-transmission format.
     */
    virtual void
    serializeWithPrefix(Serializer& s) const = 0;

    /** Return a human-readable description of this node for debugging.
     *
     *  @param id The tree address of this node.
     *  @return A string representation including the node's position.
     */
    virtual std::string
    getString(SHAMapNodeID const& id) const;

    /** Verify structural invariants in debug builds.
     *
     *  Each concrete subclass asserts its own preconditions (non-zero hash,
     *  consistent child counts, etc.). The `isRoot` parameter relaxes
     *  constraints that do not apply at the tree root — e.g., a root inner
     *  node with a single child is valid for a one-item tree.
     *
     *  @param isRoot Pass `true` when checking the tree root; pass `false`
     *      (the default) for all other nodes.
     */
    virtual void
    invariants(bool isRoot = false) const = 0;

    /** Deserialize a node from the hash-prefixed database/storage format.
     *
     *  Reads the leading 4-byte `HashPrefix` to identify the node type, strips
     *  it, then dispatches to the appropriate private factory. The supplied
     *  `hash` is passed directly to the concrete node constructor (skipping
     *  `updateHash()`), so the caller is asserting that `hash` is correct.
     *
     *  @param rawNode Serialized node bytes including the leading `HashPrefix`.
     *  @param hash    Pre-validated Merkle hash for this node.
     *  @return A `SharedPtr<SHAMapTreeNode>` to the newly-constructed node.
     *  @throws std::runtime_error if `rawNode` is fewer than 4 bytes or its
     *      prefix does not correspond to a known node type.
     */
    static intr_ptr::SharedPtr<SHAMapTreeNode>
    makeFromPrefix(Slice rawNode, SHAMapHash const& hash);

    /** Deserialize a node from the wire-transmission format.
     *
     *  Reads the trailing type byte to identify the node type, strips it, then
     *  dispatches to the appropriate factory. Because the hash is not supplied,
     *  leaf nodes call `updateHash()` internally to compute it from the payload.
     *
     *  @param rawNode Serialized node bytes including the trailing `kWIRE_TYPE_*`
     *      byte.
     *  @return A `SharedPtr<SHAMapTreeNode>` to the newly-constructed node, or
     *      an empty pointer if `rawNode` is empty.
     *  @throws std::runtime_error if the trailing type byte is unrecognized.
     */
    static intr_ptr::SharedPtr<SHAMapTreeNode>
    makeFromWire(Slice rawNode);

private:
    /** Construct a `SHAMapTxLeafNode` from raw transaction bytes.
     *
     *  The item key is computed as `sha512Half(HashPrefix::TransactionId, data)`.
     *  If `hashValid` is true, `hash` is assigned directly to the new node;
     *  otherwise `updateHash()` is deferred to the concrete constructor.
     *
     *  @param data      Raw transaction payload.
     *  @param hash      Pre-validated node hash (used only when `hashValid`).
     *  @param hashValid Whether `hash` may be trusted without recomputation.
     *  @return A `SharedPtr<SHAMapTreeNode>` to the new leaf node.
     */
    static intr_ptr::SharedPtr<SHAMapTreeNode>
    makeTransaction(Slice data, SHAMapHash const& hash, bool hashValid);

    /** Construct a `SHAMapAccountStateLeafNode` from raw account-state bytes.
     *
     *  The item key is read from the trailing 32 bytes of `data`, then chopped.
     *  Throws if `data` is too short or the extracted key is zero.
     *
     *  @param data      Raw account-state payload with 32-byte key appended.
     *  @param hash      Pre-validated node hash (used only when `hashValid`).
     *  @param hashValid Whether `hash` may be trusted without recomputation.
     *  @return A `SharedPtr<SHAMapTreeNode>` to the new leaf node.
     *  @throws std::runtime_error if `data` is shorter than 32 bytes or the
     *      extracted key is zero.
     */
    static intr_ptr::SharedPtr<SHAMapTreeNode>
    makeAccountState(Slice data, SHAMapHash const& hash, bool hashValid);

    /** Construct a `SHAMapTxPlusMetaLeafNode` from raw transaction+metadata bytes.
     *
     *  The item key is read from the trailing 32 bytes of `data`, then chopped.
     *  Throws if `data` is too short.
     *
     *  @param data      Raw tx+metadata payload with 32-byte key appended.
     *  @param hash      Pre-validated node hash (used only when `hashValid`).
     *  @param hashValid Whether `hash` may be trusted without recomputation.
     *  @return A `SharedPtr<SHAMapTreeNode>` to the new leaf node.
     *  @throws std::runtime_error if `data` is shorter than 32 bytes.
     */
    static intr_ptr::SharedPtr<SHAMapTreeNode>
    makeTransactionWithMeta(Slice data, SHAMapHash const& hash, bool hashValid);
};

}  // namespace xrpl
