/** @file
 *  Defines `NodeObject`, the atomic storage unit of the XRPL node store.
 *
 *  Every piece of ledger state — account tree nodes, transaction tree nodes,
 *  and ledger headers — is stored and retrieved as a `NodeObject`. The class
 *  is a pure value type: a type tag, a 256-bit hash key, and a raw binary
 *  blob.  Higher layers (SHAMap, ledger, serialization) are responsible for
 *  interpreting the blob's contents.
 *
 *  `NodeObject` lives in the `xrpl` namespace rather than `xrpl::NodeStore`
 *  so that the SHAMap layer, ledger subsystem, and serialization paths can
 *  consume it without pulling in the full nodestore backend API.
 */
#pragma once

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/base_uint.h>

namespace xrpl {

/** Identifies the kind of data stored in a `NodeObject`.
 *
 *  The integer values are part of the on-disk format (written by
 *  `EncodedBlob` and read by `DecodedBlob`), so they must not be changed.
 *  Value 2 is a historical gap left by a removed type and must remain
 *  unused. `Dummy` (512) is deliberately outside the contiguous valid range
 *  so it cannot be confused with a legitimate type by accident or by
 *  off-by-one arithmetic; it is used as a cache sentinel meaning "confirmed
 *  missing".
 */
enum class NodeObjectType : std::uint32_t {
    Unknown = 0,         /**< Type not yet determined or not applicable. */
    Ledger = 1,          /**< Serialized ledger header. */
    // Value 2 intentionally absent — historical removal; do not reuse.
    AccountNode = 3,     /**< SHAMap node from an account-state tree. */
    TransactionNode = 4, /**< SHAMap node from a transaction tree. */
    Dummy = 512          /**< Sentinel for a confirmed-missing cache entry; not a real object. */
};

/** Immutable storage unit carrying a type tag, a 256-bit hash key, and a
 *  raw binary payload.
 *
 *  `NodeObject` is the payload type at every level of the nodestore stack:
 *  `Backend::fetch()` produces instances; `Backend::store()` and
 *  `Backend::storeBatch()` consume them; `Database` caches shared pointers
 *  to them.  All three data members are `const` — once constructed the
 *  object never changes, which is correct for content-addressed storage.
 *
 *  Instances must be created exclusively through `createObject()`. Direct
 *  construction is blocked via the `PrivateAccess` tag idiom (see below).
 *  All shared references are `std::shared_ptr<NodeObject>`; ownership is
 *  always shared, never transferred.
 *
 *  Inherits `CountedObject<NodeObject>` to maintain a global atomic
 *  live-instance count that feeds the `get_counts` diagnostic RPC.
 *
 *  @note The hash is accepted on trust — no verification that it matches
 *      the payload is performed here. Correctness is enforced at higher
 *      layers (SHAMap traversal, ledger validation).
 *  @see SHAMap
 */
class NodeObject : public CountedObject<NodeObject>
{
public:
    /** Size in bytes of the hash key used to identify a `NodeObject`. */
    static constexpr std::size_t kKEY_BYTES = 32;

private:
    /** Tag type that makes the public constructor effectively private.
     *
     *  `std::make_shared` requires the constructor it calls to be
     *  accessible, so the constructor cannot be `private`. Instead, it
     *  takes a `PrivateAccess` argument. Because `PrivateAccess` itself is
     *  a private nested type, only code inside `NodeObject` (i.e.,
     *  `createObject`) can construct one — achieving the same effect.
     */
    struct PrivateAccess
    {
        explicit PrivateAccess() = default;
    };

public:
    /** Constructs a `NodeObject`; use `createObject()` instead.
     *
     *  The `PrivateAccess` parameter is intentionally inaccessible to
     *  external callers; it exists solely to satisfy `std::make_shared`.
     */
    NodeObject(NodeObjectType type, Blob&& data, uint256 const& hash, PrivateAccess);

    /** Create a `NodeObject`, transferring ownership of the payload buffer.
     *
     *  The caller's `data` buffer is moved into the new object; after this
     *  call `data` is in a valid but unspecified state. No copy of the
     *  payload is made.
     *
     *  @param type  The kind of ledger data the payload represents.
     *  @param data  Raw serialized payload; ownership is transferred to the
     *      returned object.
     *  @param hash  256-bit hash that uniquely identifies this object in the
     *      node store. Must be the correct hash of `data` — no verification
     *      is performed.
     *  @return A `shared_ptr` to the newly created, immutable `NodeObject`.
     */
    static std::shared_ptr<NodeObject>
    createObject(NodeObjectType type, Blob&& data, uint256 const& hash);

    /** Returns the type tag indicating what kind of ledger data this object
     *  holds.
     */
    [[nodiscard]] NodeObjectType
    getType() const;

    /** Returns the 256-bit hash that identifies this object in the node
     *  store.
     *
     *  @note The hash is not verified against the payload at construction
     *      time; callers must ensure consistency at higher layers.
     */
    [[nodiscard]] uint256 const&
    getHash() const;

    /** Returns the raw serialized payload stored in this object. */
    [[nodiscard]] Blob const&
    getData() const;

private:
    NodeObjectType const type_;
    uint256 const hash_;
    Blob const data_;
};

}  // namespace xrpl
