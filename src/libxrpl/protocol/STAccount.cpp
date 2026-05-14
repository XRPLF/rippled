/** @file
 *  Implements STAccount — the serialized-type container for XRPL account
 *  identifiers in ledger objects and transactions.
 *
 *  Key design note: although `AccountID` is a fixed 160-bit value, the wire
 *  format intentionally preserves the variable-length (VL) blob encoding used
 *  by the original `STBlob`-based implementation so that existing ledger data
 *  remains byte-for-byte compatible. The `default_` flag tracks whether the
 *  field has been explicitly assigned; a default field serializes as a
 *  zero-length VL blob rather than 20 zero bytes to distinguish "unset" from
 *  "explicitly set to the zero account."
 */
#include <xrpl/protocol/STAccount.h>

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>

#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace xrpl {

/** Construct an anonymous, unset account field.
 *
 *  Sets `value_` to `beast::kZERO` and marks `default_ = true`. An unset
 *  field serializes as a zero-length VL blob and returns an empty string
 *  from `getText()`.
 */
STAccount::STAccount() : value_(beast::kZERO), default_(true)
{
}

/** Construct a named but unset account field.
 *
 *  Binds the field to descriptor `n`, sets `value_` to zero, and marks
 *  `default_ = true`. Typical use: building a new ledger object before
 *  individual fields are populated.
 *
 *  @param n The `SField` descriptor identifying this field (e.g. `sfAccount`).
 */
STAccount::STAccount(SField const& n) : STBase(n), value_(beast::kZERO), default_(true)
{
}

/** Construct from a raw byte buffer, as produced by VL-blob deserialization.
 *
 *  An empty buffer is the round-trip representation of a default (unset)
 *  field and leaves the object in the default state. A non-empty buffer must
 *  be exactly `uint160::kBYTES` (20) bytes; any other size throws. This is
 *  the path taken by `STAccount(SerialIter&, SField const&)`.
 *
 *  @param n The `SField` descriptor for this field.
 *  @param v Raw bytes read from a VL-blob. Must be empty or exactly 20 bytes.
 *  @throws std::runtime_error if `v` is non-empty and not exactly 20 bytes.
 *  @note Throwing from a constructor is safe here because the only direct
 *      caller, `STVar::STVar(SerialIter&, SField const&)`, already propagates
 *      exceptions.
 */
STAccount::STAccount(SField const& n, Buffer const& v) : STAccount(n)
{
    if (v.empty())
        return;  // Zero is a valid size for a defaulted STAccount.

    if (v.size() != uint160::kBYTES)
        Throw<std::runtime_error>("Invalid STAccount size");

    default_ = false;
    memcpy(value_.begin(), v.data(), uint160::kBYTES);
}

/** Deserialize an account field from a wire-format byte stream.
 *
 *  Extracts the next VL-prefixed blob from `sit` and delegates to the
 *  `Buffer` constructor for size validation and value assignment.
 *
 *  @param sit  Forward cursor over the serialized byte stream; advanced past
 *      the VL blob on return.
 *  @param name The `SField` descriptor for this field.
 *  @throws std::runtime_error if the extracted blob is not empty or 20 bytes.
 */
STAccount::STAccount(SerialIter& sit, SField const& name) : STAccount(name, sit.getVLBuffer())
{
}

/** Construct from a typed `AccountID` value.
 *
 *  The field is marked non-default regardless of whether `v` is the zero
 *  account. This is the standard path in application code when the account
 *  address is already known.
 *
 *  @param n The `SField` descriptor for this field.
 *  @param v The 160-bit account identifier to store.
 */
STAccount::STAccount(SField const& n, AccountID const& v) : STBase(n), value_(v), default_(false)
{
}

/** Place a copy of this object into the supplied buffer or heap-allocate.
 *
 *  Delegates to `STBase::emplace()` for the small-object optimization used
 *  by `detail::STVar`.
 */
STBase*
STAccount::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

/** Place a moved instance into the supplied buffer or heap-allocate.
 *
 *  Delegates to `STBase::emplace()` for the small-object optimization used
 *  by `detail::STVar`.
 */
STBase*
STAccount::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

SerializedTypeID
STAccount::getSType() const
{
    return STI_ACCOUNT;
}

/** Append this field to a serializer in VL-blob wire format.
 *
 *  Preserves byte-for-byte compatibility with the legacy `STBlob`-based
 *  encoding: a default (unset) field serializes as a zero-length VL blob
 *  (one `0x00` byte on the wire), while a non-default field serializes as
 *  a 20-byte VL blob. This distinction is what separates "field not set"
 *  from "field explicitly set to the zero account."
 *
 *  @param s The serializer to append to.
 *  @note Asserts (debug builds only) that the associated `SField` is a binary
 *      field of type `STI_ACCOUNT`.
 */
void
STAccount::add(Serializer& s) const
{
    XRPL_ASSERT(getFName().isBinary(), "xrpl::STAccount::add : field is binary");
    XRPL_ASSERT(getFName().fieldType == STI_ACCOUNT, "xrpl::STAccount::add : valid field type");

    int const size = isDefault() ? 0 : uint160::kBYTES;
    s.addVL(value_.data(), size);
}

/** Check semantic equivalence with another serialized field.
 *
 *  Two `STAccount` fields are equivalent only when both their `default_`
 *  flags and their 160-bit `value_` agree. The `SField` name is intentionally
 *  ignored — equivalence is purely about the stored account state, not which
 *  field slot it occupies.
 *
 *  @param t The field to compare against.
 *  @return `true` if `t` is an `STAccount` with the same default flag and
 *      value; `false` if `t` is a different type or either attribute differs.
 *  @note This is the polymorphic path used by `STObject` for structural
 *      comparison. Callers that want to compare only the address (ignoring
 *      default state) should use `operator==` on the `value()` accessors.
 */
bool
STAccount::isEquivalent(STBase const& t) const
{
    auto const* const tPtr = dynamic_cast<STAccount const*>(&t);
    return (tPtr != nullptr) && (default_ == tPtr->default_) && (value_ == tPtr->value_);
}

bool
STAccount::isDefault() const
{
    return default_;
}

/** Return the account address as a Base58Check string, or empty if unset.
 *
 *  A default (unset) field returns an empty string rather than the Base58
 *  encoding of the all-zeros pseudo-account, preserving the distinction
 *  between an unset field and a field explicitly set to the XRP issuer
 *  sentinel (`rrrrrrrrrrrrrrrrrrrrrhoLvTp`).
 *
 *  @return Base58Check-encoded address, or `""` when `isDefault()` is true.
 */
std::string
STAccount::getText() const
{
    if (isDefault())
        return "";
    return toBase58(value());
}

}  // namespace xrpl
