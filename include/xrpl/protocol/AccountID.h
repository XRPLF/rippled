/** @file
 *  Defines the AccountID type, serialization helpers, sentinel constants,
 *  and the optional base58 encoding cache for XRP Ledger account identities.
 */

#pragma once

#include <xrpl/protocol/tokens.h>
// VFALCO Uncomment when the header issues are resolved
// #include <xrpl/protocol/PublicKey.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/json_get_or_throw.h>

#include <cstddef>
#include <optional>
#include <string>

namespace xrpl {

namespace detail {

/** Phantom tag type that makes AccountID a distinct strong type.
 *
 *  Passed as the second template argument to `BaseUInt<160, Tag>` so that
 *  a 160-bit account hash cannot be silently used where a raw hash or node ID
 *  is expected, and vice versa.  The class has no data members or behaviour.
 */
class AccountIDTag
{
public:
    explicit AccountIDTag() = default;
};

}  // namespace detail

/** A 160-bit identifier that uniquely addresses an XRP Ledger account.
 *
 *  Stored as five `uint32_t` values in big-endian byte order — a layout
 *  that is part of the binary serialization protocol and cannot be changed.
 *  Derived from a public key via SHA-256 + RIPEMD-160 (`calcAccountID()`).
 *
 *  The phantom tag `detail::AccountIDTag` makes this a distinct C++ type,
 *  preventing accidental mixing with other 160-bit quantities at compile time.
 *
 *  @see calcAccountID(), toBase58(), parseBase58<AccountID>()
 */
using AccountID = BaseUInt<160, detail::AccountIDTag>;

/** Encode an AccountID as a Base58Check string.
 *
 *  Prepends `TokenType::AccountID` (value 0) before encoding.  When the
 *  global cache has been initialised via `initAccountIdCache()`, the result
 *  is served from the cache to avoid repeated SHA-256 checksum computation.
 *
 *  @param v The account identifier to encode.
 *  @return The Base58Check-encoded string (always 25–34 printable characters).
 *  @see initAccountIdCache(), parseBase58<AccountID>()
 */
std::string
toBase58(AccountID const& v);

/** Decode a Base58Check string into an AccountID.
 *
 *  Validates the `TokenType::AccountID` prefix and requires the decoded
 *  payload to be exactly 20 bytes.  Input that fails either check returns
 *  `std::nullopt` rather than throwing, because external input is frequently
 *  untrusted.
 *
 *  @param s The Base58Check-encoded account string to parse.
 *  @return The decoded AccountID, or `std::nullopt` on any parse failure.
 *  @see toBase58()
 */
template <>
std::optional<AccountID>
parseBase58(std::string const& s);

/** Compute the AccountID for a public key using SHA-256 + RIPEMD-160.
 *
 *  Applies `RipeshaHasher` to the raw public-key bytes (no version byte).
 *  The double-hash matches Bitcoin's derivation: SHA-256 prevents
 *  length-extension attacks, and RIPEMD-160 is considered safe at 160 bits.
 *  XRPL adopted the scheme to avoid any claim of weaker security relative
 *  to Bitcoin.
 *
 *  @note Declaration lives in `PublicKey.h`; the implementation is in
 *      `AccountID.cpp`.
 */
// VFALCO In PublicKey.h for now
// AccountID
// calcAccountID (PublicKey const& pk);

/** Return the canonical XRP issuer sentinel: the all-zero AccountID.
 *
 *  Used as the issuer field in XRP `STAmount` values.  Code that needs to
 *  test whether an amount is native XRP should prefer checking the native
 *  flag or the currency directly rather than comparing the issuer against
 *  this value — see the deprecated `isXRP(AccountID)` overload.
 *
 *  @return A function-local static `AccountID` equal to `beast::kZERO`.
 *      Returned by `const&` to avoid copies; lifetime is the process lifetime.
 */
AccountID const&
xrpAccount();

/** Return the "no account" sentinel: `AccountID(1)`.
 *
 *  Used as a placeholder in offer and trust-line fields that have no
 *  meaningful account value (e.g., an uninitialized or absent counterparty).
 *  Distinct from `xrpAccount()` (all zeros) so the two sentinels cannot
 *  be confused.
 *
 *  @return A function-local static `AccountID` with value 1.
 *      Returned by `const&` to avoid copies; lifetime is the process lifetime.
 */
AccountID const&
noAccount();

/** Parse a hex or Base58Check string into an AccountID.
 *
 *  Tries hex first (`parseHex`), then falls back to Base58Check.  Used
 *  in legacy configuration parsing where the encoding is not guaranteed.
 *
 *  @param issuer Output: receives the parsed AccountID on success.
 *  @param s The hex (40 chars) or Base58Check string to parse.
 *  @return `true` if parsing succeeded and `issuer` was written.
 *  @deprecated Prefer `parseBase58<AccountID>()` for user-facing input.
 */
// DEPRECATED
bool
toIssuer(AccountID&, std::string const&);

/** Test whether an AccountID equals the XRP issuer sentinel (all zeros).
 *
 *  @param c The account identifier to test.
 *  @return `true` if `c` equals `beast::kZERO` (i.e., equals `xrpAccount()`).
 *  @deprecated Check the currency field or the native/integral flag instead;
 *      relying on the zero-account-as-issuer convention is a leaky abstraction.
 */
// DEPRECATED Should be checking the currency or native flag
inline bool
isXRP(AccountID const& c)
{
    return c == beast::kZERO;
}

/** Convert an AccountID to its Base58Check string representation.
 *
 *  @param account The account identifier to convert.
 *  @return The Base58Check-encoded string.
 *  @deprecated Use `toBase58()` directly.
 */
// DEPRECATED
inline std::string
to_string(AccountID const& account)
{
    return toBase58(account);
}

/** Write the Base58Check encoding of an AccountID to an output stream.
 *
 *  @param os The stream to write to.
 *  @param x The account identifier to encode.
 *  @return `os`, to allow chaining.
 *  @deprecated Prefer explicit `toBase58()` calls; stream output silently
 *      invokes Base58 encoding and can be surprising in logging contexts.
 */
// DEPRECATED
inline std::ostream&
operator<<(std::ostream& os, AccountID const& x)
{
    os << to_string(x);
    return os;
}

/** Initialize the global AccountID → Base58Check encoding cache.
 *
 *  Base58Check encoding requires a SHA-256 checksum on every call, which is
 *  expensive at transaction-processing throughput.  The cache uses a
 *  direct-mapped open-addressing table with 64 spinlocks packed into a single
 *  `atomic<uint64_t>` (via `PackedSpinlock`) to allow concurrent access with
 *  minimal memory overhead.  The index hash is `hardened_hash<>` (DoS-
 *  resistant seeded hash) to prevent crafted workloads from degrading lookups.
 *
 *  The cache is strictly optional: if never initialised, `toBase58()` falls
 *  through to `encodeBase58Token` on every call.
 *
 *  @param count The number of cache slots to allocate.  Pass 0 to leave the
 *      cache disabled (no-op if already disabled).
 *  @note This function initialises the cache at most once.  Subsequent calls
 *      with any `count` value are silently ignored.
 */
void
initAccountIdCache(std::size_t count);

}  // namespace xrpl

//------------------------------------------------------------------------------
namespace json {

/** Extract and parse an AccountID from a JSON object field.
 *
 *  Reads `field` from `v` as a string, then decodes it as a Base58Check
 *  account address.  Throws `JsonTypeMismatchError` if the field is absent,
 *  not a string, or not a valid AccountID encoding — the same error type
 *  raised for any other JSON type mismatch, enabling uniform error handling
 *  in RPC and transaction-parsing code.
 *
 *  @param v The JSON object to read from.
 *  @param field The SField identifying the key to look up.
 *  @return The decoded AccountID.
 *  @throws JsonTypeMismatchError if the field is missing, not a string, or
 *      cannot be decoded as a valid Base58Check AccountID.
 */
template <>
inline xrpl::AccountID
getOrThrow(json::Value const& v, xrpl::SField const& field)
{
    using namespace xrpl;

    std::string const b58 = getOrThrow<std::string>(v, field);
    if (auto const r = parseBase58<AccountID>(b58))
        return *r;
    Throw<JsonTypeMismatchError>(field.getJsonName(), "AccountID");
}
}  // namespace json

//------------------------------------------------------------------------------

namespace std {

/** `std::hash` specialization for AccountID, delegating to `hardened_hash<>`.
 *
 *  Maintains compatibility with standard-library unordered containers that
 *  key on `AccountID`.  The underlying hasher uses a random seed (DoS-
 *  resistant), so hash values differ across process restarts.
 *
 *  @deprecated Prefer `beast::uhash` or XRPL's hardened unordered containers
 *      (`UnorderedMap`, `UnorderedSet`) for new code.
 */
// DEPRECATED
// VFALCO Use beast::uhash or a hardened container
template <>
struct hash<xrpl::AccountID> : xrpl::AccountID::hasher
{
    hash() = default;
};

}  // namespace std
