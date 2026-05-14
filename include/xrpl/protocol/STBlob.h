#pragma once

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/STBase.h>

#include <cstring>

namespace xrpl {

/** Serialized-type representation of a variable-length binary field.
 *
 *  `STBlob` backs any ledger or transaction field whose wire type is
 *  `STI_VL` (variable-length) **or** `STI_ACCOUNT` (20-byte account ID).
 *  Both types share identical VL-prefixed binary encoding on the wire;
 *  the semantic distinction is carried by the `SField` descriptor rather
 *  than this class.
 *
 *  Storage is an owned `Buffer` (heap-backed `unique_ptr<uint8_t[]>`).
 *  Read access is always through a non-owning `Slice`, keeping the
 *  ownership model explicit: holding a `Slice` confers no ownership claim.
 *
 *  @note Do not store `STBlob` (or any `STBase`-derived type) in
 *      `std::vector` or other standard containers — `STBase::operator=`
 *      intentionally does not copy field names, which breaks slide-down
 *      semantics. Use `detail::STVar` (via `STObject`) instead.
 */
class STBlob : public STBase, public CountedObject<STBlob>
{
    Buffer value_;

public:
    /** Non-owning view type used for all read access to the payload. */
    using value_type = Slice;

    STBlob() = default;

    /** Copy-construct, duplicating the owned byte buffer. */
    STBlob(STBlob const& rhs);

    /** Construct by copying @p size bytes from @p data into an owned buffer.
     *
     *  @param f    The `SField` descriptor identifying this field.
     *  @param data Pointer to the source bytes (must not be null if
     *      @p size is non-zero).
     *  @param size Number of bytes to copy.
     */
    STBlob(SField const& f, void const* data, std::size_t size);

    /** Construct by taking ownership of an existing buffer.
     *
     *  @param f The `SField` descriptor identifying this field.
     *  @param b The buffer to move from; left empty after this call.
     */
    STBlob(SField const& f, Buffer&& b);

    /** Construct an empty (default) blob associated with field @p n.
     *
     *  `isDefault()` returns `true` until the payload is set.
     *
     *  @param n The `SField` descriptor identifying this field.
     */
    STBlob(SField const& n);

    /** Deserialize a variable-length blob from a byte stream.
     *
     *  Reads the VL-prefixed byte sequence from @p st via
     *  `SerialIter::getVLBuffer()`.
     *
     *  @param st   Forward-only cursor over the serialized byte stream;
     *      advanced past the VL prefix and payload bytes on return.
     *  @param name The `SField` descriptor identifying this field.
     */
    STBlob(SerialIter& st, SField const& name = kSF_GENERIC);

    /** Return the number of bytes in the payload. */
    [[nodiscard]] std::size_t
    size() const;

    /** Return a pointer to the first byte of the payload, or null if empty. */
    [[nodiscard]] std::uint8_t const*
    data() const;

    /** Return `STI_VL`, the wire type tag for variable-length fields.
     *
     *  @note Returns `STI_VL` even when the associated `SField` has type
     *      `STI_ACCOUNT`. Both types share identical VL-prefixed encoding;
     *      the field-ID byte written by the enclosing `STObject` carries the
     *      semantic distinction.
     */
    [[nodiscard]] SerializedTypeID
    getSType() const override;

    /** Return the payload as an uppercase hex string for logging and JSON output. */
    [[nodiscard]] std::string
    getText() const override;

    /** Serialize the payload into @p s with a VL length prefix.
     *
     *  Writes the byte count followed by the raw bytes via
     *  `Serializer::addVL`, the exact inverse of the deserialization path.
     *
     *  @param s The `Serializer` accumulator to append to.
     *  @note Asserts that the associated `SField` is a binary field and that
     *      its `fieldType` is `STI_VL` or `STI_ACCOUNT`. A field of any other
     *      type indicates a construction-time programming error and would
     *      produce a malformed wire encoding.
     */
    void
    add(Serializer& s) const override;

    /** Return `true` if @p t is an `STBlob` with byte-identical content.
     *
     *  Field names are not compared — only the raw payload bytes.
     *
     *  @param t The other serialized-type object to compare against.
     */
    [[nodiscard]] bool
    isEquivalent(STBase const& t) const override;

    /** Return `true` if this blob holds no bytes (empty buffer).
     *
     *  `STObject` uses this to omit optional fields whose payload has not
     *  been set, keeping wire representations compact.
     */
    [[nodiscard]] bool
    isDefault() const override;

    /** Replace the payload with a copy of @p slice.
     *
     *  Allocates a fresh `Buffer` and copies the bytes from @p slice.
     *  Use `operator=(Buffer&&)` or `setValue(Buffer&&)` to transfer
     *  ownership without a copy.
     *
     *  @param slice Non-owning view of the source bytes.
     *  @return `*this`
     */
    STBlob&
    operator=(Slice const& slice);

    /** Return a non-owning view of the payload.
     *
     *  The returned `Slice` is valid only for the lifetime of this `STBlob`
     *  and is invalidated by any mutation (`operator=`, `setValue`).
     */
    [[nodiscard]] value_type
    value() const noexcept;

    /** Transfer ownership of @p buffer to this blob in O(1).
     *
     *  @p buffer is left empty after this call.
     *
     *  @param buffer The buffer whose ownership is transferred.
     *  @return `*this`
     */
    STBlob&
    operator=(Buffer&& buffer);

    /** Transfer ownership of @p b to this blob in O(1).
     *
     *  Named alternative to `operator=(Buffer&&)` for call sites where an
     *  explicit setter reads more clearly than an assignment expression.
     *  @p b is left empty after this call.
     *
     *  @param b The buffer whose ownership is transferred.
     */
    void
    setValue(Buffer&& b);

private:
    /** Place-construct a copy into an `STVar` inline buffer or heap.
     *
     *  Called exclusively by `detail::STVar`. Delegates to
     *  `STBase::emplace(n, buf, *this)`.
     */
    STBase*
    copy(std::size_t n, void* buf) const override;

    /** Place-construct a moved instance into an `STVar` inline buffer or heap.
     *
     *  Called exclusively by `detail::STVar`. Delegates to
     *  `STBase::emplace(n, buf, std::move(*this))`, transferring `Buffer`
     *  ownership without copying the payload bytes.
     */
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

inline STBlob::STBlob(STBlob const& rhs)
    : STBase(rhs), CountedObject<STBlob>(rhs), value_(rhs.data(), rhs.size())
{
}

inline STBlob::STBlob(SField const& f, void const* data, std::size_t size)
    : STBase(f), value_(data, size)
{
}

inline STBlob::STBlob(SField const& f, Buffer&& b) : STBase(f), value_(std::move(b))
{
}

inline STBlob::STBlob(SField const& n) : STBase(n)
{
}

inline std::size_t
STBlob::size() const
{
    return value_.size();
}

inline std::uint8_t const*
STBlob::data() const
{
    return value_.data();
}

inline STBlob&
STBlob::operator=(Slice const& slice)
{
    value_ = Buffer(slice.data(), slice.size());
    return *this;
}

inline STBlob::value_type
STBlob::value() const noexcept
{
    return value_;
}

inline STBlob&
STBlob::operator=(Buffer&& buffer)
{
    value_ = std::move(buffer);
    return *this;
}

inline void
STBlob::setValue(Buffer&& b)
{
    value_ = std::move(b);
}

}  // namespace xrpl
