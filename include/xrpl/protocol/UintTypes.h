/** @file
 *  Defines the strongly-typed fixed-width integer identifiers used throughout
 *  the XRPL protocol layer.
 *
 *  Rather than passing bare `uint160` or `uint256` values, this header creates
 *  a family of mutually-incompatible types via the tag-parameter mechanism of
 *  `BaseUInt<Bits, Tag>`.  A `Currency`, a `NodeID`, and an `AccountID` are
 *  all 160-bit values at the hardware level but are distinct C++ types, so the
 *  compiler rejects accidental substitutions at compile time.  The tag classes
 *  themselves contribute no data members and no runtime overhead.
 *
 *  Also declares the three sentinel `Currency` values (`xrpCurrency`,
 *  `noCurrency`, `badCurrency`) and the string ↔ `Currency` conversion
 *  functions that implement the XRPL wire-format currency encoding.
 */

#pragma once

#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/AccountID.h>

namespace xrpl {
namespace detail {

/** Phantom tag type that makes `Currency` a distinct strong type.
 *
 *  Passed as the second template argument to `BaseUInt<160, Tag>` so that
 *  a 160-bit currency value cannot be silently used where a `NodeID` or raw
 *  hash is expected, and vice versa.  The class has no data members or
 *  behaviour; it exists only to produce a unique template instantiation.
 */
class CurrencyTag
{
public:
    explicit CurrencyTag() = default;
};

/** Phantom tag type that makes `Directory` a distinct strong type.
 *
 *  Passed as the second template argument to `BaseUInt<256, Tag>`.  Without
 *  this tag, a `Directory` and an untagged `uint256` would share the same
 *  type and be freely interchangeable.  The class has no data members or
 *  behaviour.
 */
class DirectoryTag
{
public:
    explicit DirectoryTag() = default;
};

/** Phantom tag type that makes `NodeID` a distinct strong type.
 *
 *  Passed as the second template argument to `BaseUInt<160, Tag>` so that
 *  a 160-bit validator node ID cannot be silently used where a `Currency` or
 *  `AccountID` is expected.  The class has no data members or behaviour.
 */
class NodeIDTag
{
public:
    explicit NodeIDTag() = default;
};

}  // namespace detail

/** A 256-bit index into the DEX offer-book directory.
 *
 *  The last 64 bits of this value encode the order-book quality (in big-endian
 *  byte order), so that the natural `SHAMap` sort order over directory keys
 *  matches price-ascending order.  The `detail::DirectoryTag` phantom type
 *  makes this a distinct C++ type from untagged `uint256`.
 *
 *  @see keylet::quality(), Quality
 */
using Directory = BaseUInt<256, detail::DirectoryTag>;

/** A 160-bit identifier for an XRP Ledger currency.
 *
 *  Distinct from `NodeID` and `AccountID` at the C++ type level, even though
 *  all three are 160-bit values.  The canonical encoding of the native asset
 *  (XRP) is the all-zero value; use `xrpCurrency()` and `isXRP()` rather than
 *  constructing zero directly.  ISO-style 3-character codes occupy bytes 12–14
 *  of the 20-byte wire representation, with all other bytes zero.
 *
 *  @see xrpCurrency(), noCurrency(), badCurrency(), isXRP(), to_string(),
 *      toCurrency()
 */
using Currency = BaseUInt<160, detail::CurrencyTag>;

/** A 160-bit identifier for a validator node.
 *
 *  Distinct from `Currency` and `AccountID` at the C++ type level via the
 *  `detail::NodeIDTag` phantom parameter.  Derived from the node's public key
 *  via SHA-256 + RIPEMD-160 (the same derivation as `AccountID`).
 */
using NodeID = BaseUInt<160, detail::NodeIDTag>;

/** A 192-bit identifier for an MPT (Multi-Purpose Token) issuance.
 *
 *  Composed of a 32-bit sequence number (big-endian) in the high bits followed
 *  by the 160-bit issuer `AccountID`.  No tag parameter is used because no
 *  other 192-bit type exists in the protocol; the added friction of an empty
 *  tag class is not justified when there is nothing to confuse it with.
 */
using MPTID = BaseUInt<192>;

/** A 256-bit generic domain hash.
 *
 *  No tag parameter is used because there is no other untagged 256-bit type it
 *  could be confused with in the same contexts; see `Directory` for the tagged
 *  256-bit counterpart.
 */
using Domain = BaseUInt<256>;

/** Return the canonical encoding of XRP, the ledger's native asset.
 *
 *  The all-zero 160-bit value (`beast::kZERO`) is the protocol-defined
 *  representation of XRP.  `isXRP(Currency const&)` tests equality against
 *  this value.  The returned reference points to a function-local static and
 *  is valid for the lifetime of the process.
 *
 *  @return A `const` reference to the singleton zero `Currency`.
 *  @see isXRP(Currency const&), noCurrency(), badCurrency()
 */
Currency const&
xrpCurrency();

/** Return a placeholder sentinel used when no currency is meaningful.
 *
 *  Holds the value `1`.  Data structures that require a `Currency` slot but
 *  have no actual currency to store use this sentinel rather than zero (which
 *  would be mistaken for XRP).  The returned reference is to a function-local
 *  static valid for the process lifetime.
 *
 *  @return A `const` reference to the singleton `Currency{1}`.
 *  @see xrpCurrency(), badCurrency()
 */
Currency const&
noCurrency();

/** Return the poisoned sentinel for the "XRP as ISO code" misuse.
 *
 *  Early adopters sometimes encoded XRP using the three-letter ISO code
 *  `"XRP"` (byte value `0x5852500000000000` placed at bytes 12–14), rather
 *  than the correct all-zero form.  This sentinel lets code paths detect and
 *  reject that mistake explicitly.  `to_string()` on this value produces a
 *  40-character hex string, not `"XRP"`, making the anomaly visible.  The
 *  returned reference is to a function-local static valid for the process
 *  lifetime.
 *
 *  @return A `const` reference to the singleton `Currency{0x5852500000000000}`.
 *  @see xrpCurrency(), noCurrency(), toCurrency()
 */
Currency const&
badCurrency();

/** Test whether a `Currency` represents the native XRP asset.
 *
 *  @param c The currency to test.
 *  @return `true` if `c` equals `beast::kZERO` (i.e., equals `xrpCurrency()`).
 */
inline bool
isXRP(Currency const& c)
{
    return c == beast::kZERO;
}

/** Convert a `Currency` to its canonical string representation.
 *
 *  Applies a priority-ordered decision tree:
 *  - Zero value → `"XRP"` (native asset).
 *  - `noCurrency()` → `"1"`.
 *  - All non-ISO bytes zero and the 3-character code in the allowed character
 *    set (alphanumeric plus `<>(){}[]|?!@#$%^&*`) and not equal to `"XRP"` →
 *    the three-character string.
 *  - Everything else → a 40-character uppercase hex string.
 *
 *  @param c The currency value to convert.
 *  @return A string in one of the four forms described above.
 *  @note A currency whose ISO bytes spell `"XRP"` but whose surrounding bytes
 *      are non-zero is returned as hex, making the anomaly visible rather than
 *      silently aliasing to the native currency.
 */
std::string
to_string(Currency const& c);

/** Parse a string into a `Currency`, reporting success via the return value.
 *
 *  Accepted forms:
 *  - Empty string or `"XRP"` → sets `currency` to `xrpCurrency()` (all zero).
 *  - Exactly 3 characters from the XRPL ISO character set → zero-pads and
 *    writes the code at byte offset 12 of `currency`.
 *  - 40-character hex string → parsed directly into `currency`.
 *  - Anything else → returns `false` and leaves `currency` unmodified.
 *
 *  @param currency Output: receives the parsed `Currency` on success.
 *  @param code The string to parse.
 *  @return `true` if parsing succeeded; `false` otherwise.
 *  @note Returns `true` even when the result equals `badCurrency()`.  This
 *      legacy behaviour is preserved to avoid breaking existing callers;
 *      validate the output value separately if `badCurrency()` must be
 *      excluded.
 */
bool
toCurrency(Currency&, std::string const&);

/** Parse a string into a `Currency`, returning `noCurrency()` on failure.
 *
 *  Convenience wrapper around `toCurrency(Currency&, std::string const&)`.
 *  Prefer the two-argument overload when the caller must distinguish a parse
 *  failure from a legitimately absent currency.
 *
 *  @param code The string to parse.
 *  @return The parsed `Currency`, or `noCurrency()` if the string is invalid.
 *  @note Can return `badCurrency()` for hex input that encodes that sentinel.
 *      This legacy behaviour is preserved; callers should validate the result
 *      if they need to exclude `badCurrency()`.
 */
Currency
toCurrency(std::string const&);

/** Write the string representation of a `Currency` to an output stream.
 *
 *  Delegates to `to_string(Currency const&)`.
 *
 *  @param os The stream to write to.
 *  @param x The currency to format.
 *  @return `os`, to allow chaining.
 */
inline std::ostream&
operator<<(std::ostream& os, Currency const& x)
{
    os << to_string(x);
    return os;
}

}  // namespace xrpl

namespace std {

/** `std::hash` specialization for `xrpl::Currency`.
 *
 *  Inherits the `hasher` inner type provided by `BaseUInt`, enabling
 *  `Currency` to be used directly as a key in `std::unordered_map` and
 *  `std::unordered_set`.
 */
template <>
struct hash<xrpl::Currency> : xrpl::Currency::hasher
{
    hash() = default;
};

/** `std::hash` specialization for `xrpl::NodeID`.
 *
 *  Inherits the `hasher` inner type provided by `BaseUInt`, enabling
 *  `NodeID` to be used directly as a key in `std::unordered_map` and
 *  `std::unordered_set`.
 */
template <>
struct hash<xrpl::NodeID> : xrpl::NodeID::hasher
{
    hash() = default;
};

/** `std::hash` specialization for `xrpl::Directory`.
 *
 *  Inherits the `hasher` inner type provided by `BaseUInt`, enabling
 *  `Directory` to be used directly as a key in `std::unordered_map` and
 *  `std::unordered_set`.
 */
template <>
struct hash<xrpl::Directory> : xrpl::Directory::hasher
{
    hash() = default;
};

/** `std::hash` specialization for `xrpl::uint256`.
 *
 *  Inherits the `hasher` inner type provided by `BaseUInt`.  Included here
 *  because `uint256` is used in the same contexts as `Currency`, `NodeID`,
 *  and `Directory`, even though it is not exclusively defined in this file.
 */
template <>
struct hash<xrpl::uint256> : xrpl::uint256::hasher
{
    hash() = default;
};

}  // namespace std
