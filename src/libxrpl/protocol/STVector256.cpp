/** @file
 *  Implements STVector256, the serialized type for ordered lists of uint256 values.
 *
 *  On the wire the array is encoded as a single VL-prefixed blob of concatenated
 *  32-byte hashes (type identifier STI_VECTOR256, code 19). Common ledger fields
 *  that use this type include sfAmendments, sfIndexes, and sfHashes.
 */

#include <xrpl/protocol/STVector256.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace xrpl {

/** Deserialize an STVector256 from a wire-format stream.
 *
 *  Reads a single VL-prefixed blob from @p sit and partitions it into
 *  consecutive 32-byte chunks, each becoming one `uint256` entry. The
 *  resulting vector retains the original wire order.
 *
 *  @param sit  Forward-only iterator positioned at the VL length prefix of
 *      the field. Consumed by exactly one `getVLDataLength` + `getSlice` pair.
 *  @param name The `SField` that identifies this field within its parent
 *      `STObject`.
 *  @throws std::runtime_error if the decoded blob length is not an exact
 *      multiple of 32 bytes, indicating corrupt or truncated data. Thrown
 *      (not asserted) because this path processes untrusted network data.
 */
STVector256::STVector256(SerialIter& sit, SField const& name) : STBase(name)
{
    auto const slice = sit.getSlice(sit.getVLDataLength());

    if (slice.size() % uint256::size() != 0)
    {
        Throw<std::runtime_error>(
            "Bad serialization for STVector256: " + std::to_string(slice.size()));
    }

    auto const cnt = slice.size() / uint256::size();

    value_.reserve(cnt);

    for (std::size_t i = 0; i != cnt; ++i)
        value_.emplace_back(slice.substr(i * uint256::size(), uint256::size()));
}

/** Copy this object into a caller-supplied buffer using the STVar placement protocol.
 *
 *  Delegates to `STBase::emplace`, which constructs in-place when @p buf is
 *  large enough, or heap-allocates otherwise.
 *
 *  @param n    Size of the buffer at @p buf, in bytes.
 *  @param buf  Destination buffer for placement construction.
 *  @return Pointer to the newly constructed `STVector256`.
 */
STBase*
STVector256::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

/** Move this object into a caller-supplied buffer using the STVar placement protocol.
 *
 *  Delegates to `STBase::emplace`, which constructs in-place when @p buf is
 *  large enough, or heap-allocates otherwise. The source object is left in a
 *  valid but unspecified state after the move.
 *
 *  @param n    Size of the buffer at @p buf, in bytes.
 *  @param buf  Destination buffer for placement construction.
 *  @return Pointer to the newly constructed `STVector256`.
 */
STBase*
STVector256::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

/** Return the serialized type identifier for this field.
 *
 *  @return `STI_VECTOR256` (code 19).
 */
SerializedTypeID
STVector256::getSType() const
{
    return STI_VECTOR256;
}

/** Return whether this object holds no entries.
 *
 *  An empty `STVector256` is the canonical default value. Per XRPL
 *  serialization rules, default-valued fields are omitted from the wire
 *  encoding and contribute nothing to a transaction or ledger hash.
 *
 *  @return `true` if the internal vector is empty.
 */
bool
STVector256::isDefault() const
{
    return value_.empty();
}

/** Serialize this array into @p s as a VL-prefixed blob.
 *
 *  Writes a length prefix followed by the concatenated raw bytes of each
 *  `uint256` entry. The two assertions guard against programmer errors such
 *  as assigning an `STVector256` value to a field of the wrong type before
 *  serialization reaches the wire.
 *
 *  @param s  Accumulator to append the encoded field into.
 *  @note Asserts (debug builds only) that the associated `SField` is marked
 *      binary and carries type `STI_VECTOR256`.
 */
void
STVector256::add(Serializer& s) const
{
    XRPL_ASSERT(getFName().isBinary(), "xrpl::STVector256::add : field is binary");
    XRPL_ASSERT(getFName().fieldType == STI_VECTOR256, "xrpl::STVector256::add : valid field type");
    s.addVL(value_.begin(), value_.end(), value_.size() * (256 / 8));
}

/** Test deep equality with another `STBase` instance.
 *
 *  Two `STVector256` objects are equivalent when they contain the same
 *  sequence of `uint256` values in the same order.
 *
 *  @param t  The object to compare against. Must be dynamically of type
 *      `STVector256`; otherwise the result is `false`.
 *  @return `true` if @p t is an `STVector256` with identical contents.
 */
bool
STVector256::isEquivalent(STBase const& t) const
{
    STVector256 const* v = dynamic_cast<STVector256 const*>(&t);
    return (v != nullptr) && (value_ == v->value_);
}

/** Produce a JSON array where each element is the hex string of a `uint256`.
 *
 *  The `JsonOptions` parameter is accepted for interface conformance but is
 *  not consumed; `STVector256` has no output variants that depend on API
 *  version or format flags.
 *
 *  @return A `json::arrayValue` containing one lowercase hex string per entry,
 *      as produced by `to_string(uint256)`. This is the representation seen
 *      in RPC responses for fields such as `sfAmendments`.
 */
json::Value
STVector256::getJson(JsonOptions) const
{
    json::Value ret(json::ValueType::Array);

    for (auto const& vEntry : value_)
        ret.append(to_string(vEntry));

    return ret;
}

}  // namespace xrpl
