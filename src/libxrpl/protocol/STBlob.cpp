/** @file
 *  Implements STBlob, the serialized-type class for variable-length binary
 *  fields (`STI_VL`) and account-identifier fields (`STI_ACCOUNT`).
 *
 *  Both wire types share identical VL-prefixed binary encoding; the
 *  `fieldType` tag on the `SField` carries the semantic distinction used
 *  by higher layers (JSON formatting, validation) while this file handles
 *  the common binary I/O path.
 */

#include <xrpl/protocol/STBlob.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>

#include <cstddef>
#include <string>
#include <utility>

namespace xrpl {

/** Deserialize a variable-length blob from a byte stream.
 *
 *  Reads a VL-prefixed byte sequence from @p st via `getVLBuffer()` and
 *  stores the resulting owned `Buffer`. The length prefix has already been
 *  consumed by `SerialIter` before this constructor runs.
 *
 *  @param st   Forward-only cursor over the serialized byte stream.
 *  @param name The `SField` descriptor that identifies this field.
 */
STBlob::STBlob(SerialIter& st, SField const& name) : STBase(name), value_(st.getVLBuffer())
{
}

/** Place-construct a copy of this blob into an STVar buffer.
 *
 *  Used exclusively by `detail::STVar` to clone a blob field without
 *  knowing its concrete type. If `sizeof(STBlob) <= n`, the copy is
 *  constructed in-place at @p buf via `STBase::emplace`; otherwise it
 *  falls back to heap allocation.
 *
 *  @param n   Size of the inline buffer offered by `STVar`.
 *  @param buf Pointer to the inline buffer (valid for at least @p n bytes).
 *  @return Pointer to the newly constructed `STBlob` (may be @p buf or heap).
 */
STBase*
STBlob::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

/** Place-construct a moved instance of this blob into an STVar buffer.
 *
 *  Like `copy()`, but transfers ownership of the internal `Buffer` via
 *  move, avoiding a heap allocation for the payload bytes. Used by
 *  `detail::STVar` when relocating an existing blob field.
 *
 *  @param n   Size of the inline buffer offered by `STVar`.
 *  @param buf Pointer to the inline buffer (valid for at least @p n bytes).
 *  @return Pointer to the newly constructed `STBlob` (may be @p buf or heap).
 */
STBase*
STBlob::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

/** Return the wire type code for this field.
 *
 *  Always returns `STI_VL` regardless of whether the `SField` is
 *  semantically `STI_ACCOUNT`. Both types share the same VL-prefixed
 *  binary encoding; the field-ID byte written by the containing
 *  `STObject` will embed `STI_VL` for both.
 *
 *  @return `STI_VL`
 */
SerializedTypeID
STBlob::getSType() const
{
    return STI_VL;
}

/** Return the blob contents as an uppercase hex string.
 *
 *  Used by `STBase::getFullText()` and surfaces in JSON output and log
 *  messages.
 *
 *  @return Uppercase hex encoding of the raw bytes.
 */
std::string
STBlob::getText() const
{
    return strHex(value_);
}

/** Serialize this blob into @p s with a VL length prefix.
 *
 *  Writes the byte count followed by the raw payload via `Serializer::addVL`,
 *  the exact inverse of the `getVLBuffer()` deserialization path.
 *
 *  @param s Serializer accumulator to append to.
 *  @note Asserts that the associated `SField` is a binary field
 *      (`fieldValue < 256`) and that its `fieldType` is either `STI_VL`
 *      or `STI_ACCOUNT`. Violations indicate an `SField` of the wrong type
 *      was used to construct this `STBlob`, which would produce a malformed
 *      wire encoding.
 */
void
STBlob::add(Serializer& s) const
{
    XRPL_ASSERT(getFName().isBinary(), "xrpl::STBlob::add : field is binary");
    XRPL_ASSERT(
        (getFName().fieldType == STI_VL) || (getFName().fieldType == STI_ACCOUNT),
        "xrpl::STBlob::add : valid field type");
    s.addVL(value_.data(), value_.size());
}

/** Compare this blob to another serialized type by byte value.
 *
 *  Returns `true` only when @p t is also an `STBlob` and its payload is
 *  byte-for-byte identical. The `dynamic_cast` guard ensures a field of a
 *  different ST type never compares equal even if their raw bytes happened
 *  to match. Used by `STBase::operator==` for transaction de-duplication
 *  and ledger comparison.
 *
 *  @param t The other serialized type to compare against.
 *  @return `true` if @p t is an `STBlob` with identical contents.
 */
bool
STBlob::isEquivalent(STBase const& t) const
{
    STBlob const* v = dynamic_cast<STBlob const*>(&t);
    return (v != nullptr) && (value_ == v->value_);
}

/** Return whether this blob holds no data.
 *
 *  `STObject` serialization uses this to omit optional fields that have
 *  not been populated, keeping the wire representation compact.
 *
 *  @return `true` if the internal buffer is empty.
 */
bool
STBlob::isDefault() const
{
    return value_.empty();
}

}  // namespace xrpl
