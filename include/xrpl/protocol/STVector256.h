/** @file
 *  Declares STVector256, the serialized type for ordered lists of uint256 values.
 *
 *  On the wire the array is encoded as a single VL-prefixed blob of concatenated
 *  32-byte hashes (type identifier STI_VECTOR256, code 19). Common ledger fields
 *  that use this type include sfAmendments, sfIndexes, and sfHashes.
 */

#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/STInteger.h>

namespace xrpl {

/** Serialized type for an ordered list of 256-bit hash values.
 *
 *  Wraps a `std::vector<uint256>` with the `STBase` contract so that hash
 *  collections can be stored as named, typed fields inside `STObject` —
 *  giving them a wire-format identity (`STI_VECTOR256`, code 19), a
 *  canonical binary encoding (VL-prefixed blob of packed 32-byte values),
 *  and a JSON representation (array of hex strings). Typical ledger uses
 *  include `sfAmendments` (active amendments in a validator vote),
 *  `sfIndexes` (keys in a `DirectoryNode` page), and `sfHashes`.
 *
 *  An empty `STVector256` is the canonical default state and is omitted from
 *  the wire encoding when the field is declared optional.
 *
 *  The `CountedObject<STVector256>` mixin adds lock-free instance counting
 *  for diagnostic purposes, with no overhead in the fast path.
 */
class STVector256 : public STBase, public CountedObject<STVector256>
{
    std::vector<uint256> value_;

public:
    /** Reference type used when this value is passed as a read-only handle. */
    using value_type = std::vector<uint256> const&;

    /** Construct an empty, unnamed STVector256. */
    STVector256() = default;

    /** Construct an empty STVector256 bound to the given field name.
     *
     *  @param n The SField that identifies this field in its parent STObject.
     */
    explicit STVector256(SField const& n);

    /** Construct an unnamed STVector256 pre-populated with @p vector.
     *
     *  @param vector Initial contents; copied into the internal store.
     */
    explicit STVector256(std::vector<uint256> const& vector);

    /** Construct an STVector256 bound to @p n and pre-populated with @p vector.
     *
     *  @param n      The SField that identifies this field in its parent STObject.
     *  @param vector Initial contents; copied into the internal store.
     */
    STVector256(SField const& n, std::vector<uint256> const& vector);

    /** Deserialize an STVector256 from a wire-format stream.
     *
     *  Reads a single VL-prefixed blob from @p sit and partitions it into
     *  consecutive 32-byte chunks, each becoming one `uint256` entry. The
     *  resulting vector retains the original wire order.
     *
     *  @param sit  Forward-only iterator positioned at the VL length prefix of
     *      the field. Consumed by exactly one VL-length + slice read pair.
     *  @param name The SField that identifies this field within its parent STObject.
     *  @throws std::runtime_error if the decoded blob length is not an exact
     *      multiple of 32 bytes, indicating corrupt or truncated data.
     */
    STVector256(SerialIter& sit, SField const& name);

    /** Return the serialized type identifier for this field.
     *
     *  @return `STI_VECTOR256` (code 19).
     */
    [[nodiscard]] SerializedTypeID
    getSType() const override;

    /** Serialize this array into @p s as a VL-prefixed blob.
     *
     *  Writes a length prefix followed by the concatenated raw bytes of each
     *  `uint256` entry (32 bytes per element, no padding or separators).
     *
     *  @param s Accumulator to append the encoded field into.
     *  @note Asserts (debug builds only) that the associated SField is marked
     *      binary and carries type `STI_VECTOR256`. These guards catch accidental
     *      field-type mismatches before data reaches the wire.
     */
    void
    add(Serializer& s) const override;

    /** Produce a JSON array of hex-encoded hash strings.
     *
     *  Each `uint256` entry is rendered as a lowercase hex string via
     *  `to_string()`. The @p options parameter is accepted for interface
     *  conformance but is unused; the representation is identical across
     *  all API versions.
     *
     *  @return A `json::arrayValue` with one hex string per entry.
     */
    [[nodiscard]] json::Value getJson(JsonOptions) const override;

    /** Test deep equality with another STBase instance.
     *
     *  Two `STVector256` objects are equivalent when they contain the same
     *  sequence of `uint256` values in the same order.
     *
     *  @param t The object to compare against.
     *  @return `true` if @p t is an `STVector256` with identical contents;
     *      `false` if the types differ or the sequences do not match.
     */
    [[nodiscard]] bool
    isEquivalent(STBase const& t) const override;

    /** Return whether this object holds no entries.
     *
     *  An empty `STVector256` is the canonical default value. Per XRPL
     *  serialization rules, default-valued optional fields are omitted from
     *  the wire encoding and contribute nothing to a transaction or ledger hash.
     *
     *  @return `true` if the internal vector is empty.
     */
    [[nodiscard]] bool
    isDefault() const override;

    /** Replace the contents with a copy of @p v.
     *
     *  @param v Source vector; copied into the internal store.
     *  @return Reference to this object.
     */
    STVector256&
    operator=(std::vector<uint256> const& v);

    /** Replace the contents by moving @p v into the internal store.
     *
     *  @param v Source vector; left in a valid but unspecified state after the call.
     *  @return Reference to this object.
     */
    STVector256&
    operator=(std::vector<uint256>&& v);

    /** Copy the inner vector from @p v, leaving the SField name unchanged.
     *
     *  Unlike `operator=`, this copies only the payload (`mValue`), not the
     *  field binding. Use this when you need to transfer values between two
     *  fields that have different SField identities.
     *
     *  @param v Source object whose contents are copied.
     */
    void
    setValue(STVector256 const& v);

    /** Return a copy of the internal vector.
     *
     *  Marked `explicit` to prevent accidental implicit copies in generic
     *  contexts; prefer `value()` for read-only access.
     */
    explicit
    operator std::vector<uint256>() const;

    /** Return the number of entries in the vector.
     *
     *  @return Entry count; 0 for an empty (default) object.
     */
    [[nodiscard]] std::size_t
    size() const;

    /** Resize the internal vector to @p n entries.
     *
     *  New entries (if any) are value-initialized to the zero `uint256`.
     *
     *  @param n Target size.
     */
    void
    resize(std::size_t n);

    /** Return whether the vector contains no entries.
     *
     *  @return `true` if `size() == 0`.
     */
    [[nodiscard]] bool
    empty() const;

    /** Return a mutable reference to the entry at index @p n.
     *
     *  @param n Zero-based index; behavior is undefined if out of range.
     */
    std::vector<uint256>::reference
    operator[](std::vector<uint256>::size_type n);

    /** Return a read-only reference to the entry at index @p n.
     *
     *  @param n Zero-based index; behavior is undefined if out of range.
     */
    std::vector<uint256>::const_reference
    operator[](std::vector<uint256>::size_type n) const;

    /** Return a read-only reference to the internal vector.
     *
     *  Prefer this over the explicit conversion operator for non-owning access.
     *
     *  @return Const reference to the underlying `std::vector<uint256>`.
     */
    [[nodiscard]] std::vector<uint256> const&
    value() const;

    /** Insert @p value before @p pos.
     *
     *  @param pos   Iterator before which the new element is inserted.
     *  @param value Hash to insert.
     *  @return Iterator to the inserted element.
     */
    std::vector<uint256>::iterator
    insert(std::vector<uint256>::const_iterator pos, uint256 const& value);

    /** Append @p v to the end of the vector.
     *
     *  @param v Hash to append.
     */
    void
    pushBack(uint256 const& v);

    /** Return a mutable iterator to the first element. */
    std::vector<uint256>::iterator
    begin();

    /** Return a read-only iterator to the first element. */
    [[nodiscard]] std::vector<uint256>::const_iterator
    begin() const;

    /** Return a mutable past-the-end iterator. */
    std::vector<uint256>::iterator
    end();

    /** Return a read-only past-the-end iterator. */
    [[nodiscard]] std::vector<uint256>::const_iterator
    end() const;

    /** Remove the element at @p position.
     *
     *  @param position Iterator to the element to remove.
     *  @return Iterator to the element following the removed one.
     */
    std::vector<uint256>::iterator
    erase(std::vector<uint256>::iterator position);

    /** Remove all entries, leaving the vector empty. */
    void
    clear() noexcept;

private:
    /** Copy this object into a caller-supplied buffer via the STVar placement protocol.
     *
     *  Called only by `detail::STVar`. Delegates to `STBase::emplace`, which
     *  constructs in-place when @p buf is large enough, or heap-allocates otherwise.
     *
     *  @param n   Size of the buffer at @p buf, in bytes.
     *  @param buf Destination buffer for placement construction.
     *  @return Pointer to the newly constructed `STVector256`.
     */
    STBase*
    copy(std::size_t n, void* buf) const override;

    /** Move this object into a caller-supplied buffer via the STVar placement protocol.
     *
     *  Called only by `detail::STVar`. Delegates to `STBase::emplace`, which
     *  constructs in-place when @p buf is large enough, or heap-allocates otherwise.
     *  The source is left in a valid but unspecified state.
     *
     *  @param n   Size of the buffer at @p buf, in bytes.
     *  @param buf Destination buffer for placement construction.
     *  @return Pointer to the newly constructed `STVector256`.
     */
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

inline STVector256::STVector256(SField const& n) : STBase(n)
{
}

inline STVector256::STVector256(std::vector<uint256> const& vector) : value_(vector)
{
}

inline STVector256::STVector256(SField const& n, std::vector<uint256> const& vector)
    : STBase(n), value_(vector)
{
}

inline STVector256&
STVector256::operator=(std::vector<uint256> const& v)
{
    value_ = v;
    return *this;
}

inline STVector256&
STVector256::operator=(std::vector<uint256>&& v)
{
    value_ = std::move(v);
    return *this;
}

inline void
STVector256::setValue(STVector256 const& v)
{
    value_ = v.value_;
}

inline STVector256::
operator std::vector<uint256>() const
{
    return value_;
}

inline std::size_t
STVector256::size() const
{
    return value_.size();
}

inline void
STVector256::resize(std::size_t n)
{
    value_.resize(n);
}

inline bool
STVector256::empty() const
{
    return value_.empty();
}

inline std::vector<uint256>::reference
STVector256::operator[](std::vector<uint256>::size_type n)
{
    return value_[n];
}

inline std::vector<uint256>::const_reference
STVector256::operator[](std::vector<uint256>::size_type n) const
{
    return value_[n];
}

inline std::vector<uint256> const&
STVector256::value() const
{
    return value_;
}

inline std::vector<uint256>::iterator
STVector256::insert(std::vector<uint256>::const_iterator pos, uint256 const& value)
{
    return value_.insert(pos, value);
}

inline void
STVector256::pushBack(uint256 const& v)
{
    value_.push_back(v);
}

inline std::vector<uint256>::iterator
STVector256::begin()
{
    return value_.begin();
}

inline std::vector<uint256>::const_iterator
STVector256::begin() const
{
    return value_.begin();
}

inline std::vector<uint256>::iterator
STVector256::end()
{
    return value_.end();
}

inline std::vector<uint256>::const_iterator
STVector256::end() const
{
    return value_.end();
}

inline std::vector<uint256>::iterator
STVector256::erase(std::vector<uint256>::iterator position)
{
    return value_.erase(position);
}

inline void
STVector256::clear() noexcept
{
    value_.clear();
}

}  // namespace xrpl
