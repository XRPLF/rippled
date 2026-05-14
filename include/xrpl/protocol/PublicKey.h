#pragma once

#include <xrpl/basics/Slice.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/STExchange.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/json_get_or_throw.h>
#include <xrpl/protocol/tokens.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <ostream>

namespace xrpl {

/** Immutable 33-byte value type holding an XRPL public key.
 *
 *  Supports both secp256k1 and Ed25519 cryptosystems. The lead byte acts as
 *  a self-describing type tag — `0x02`/`0x03` for secp256k1 compressed keys,
 *  `0xED` for Ed25519 keys (an XRPL-specific prefix that pads the native
 *  32-byte Ed25519 key to the common 33-byte size). This uniform encoding
 *  allows `publicKeyType()` to identify the algorithm in O(1) from the raw
 *  bytes alone, with no external metadata.
 *
 *  The default constructor is deleted; the only construction path is from a
 *  `Slice`. If the slice does not represent a recognized key format,
 *  construction calls `LogicError` (process termination) rather than
 *  throwing — an invalid key at this point indicates a programming error,
 *  not a recoverable runtime condition. Any live `PublicKey` object is
 *  therefore always well-formed and algorithm-identified.
 *
 *  The implicit conversion to `Slice` is intentional: it lets `PublicKey`
 *  flow into serialization and hashing APIs without explicit casting.
 */
class PublicKey
{
protected:
    /** Uniform storage size in bytes for all supported key types. */
    static constexpr std::size_t kSIZE = 33;
    std::uint8_t buf_[kSIZE]{};

public:
    using const_iterator = std::uint8_t const*;

public:
    PublicKey() = delete;

    PublicKey(PublicKey const& other);
    PublicKey&
    operator=(PublicKey const& other);

    /** Construct from a raw byte slice.
     *
     *  Copies exactly 33 bytes from `slice` after verifying that the bytes
     *  represent a recognized key format (secp256k1 or Ed25519). Calls
     *  `LogicError` — terminating the process — if the slice is undersized
     *  or does not pass `publicKeyType()`.
     *
     *  @param slice Raw bytes to construct from; must satisfy
     *      `publicKeyType(slice) != std::nullopt`.
     *  @note Use `publicKeyType()` to validate untrusted input before
     *      constructing; `parseBase58<PublicKey>` does this automatically
     *      for Base58-encoded keys.
     */
    explicit PublicKey(Slice const& slice);

    /** Return a pointer to the raw 33-byte key buffer. */
    [[nodiscard]] std::uint8_t const*
    data() const noexcept
    {
        return buf_;
    }

    /** Return the fixed size of all `PublicKey` objects (always 33). */
    static std::size_t
    size() noexcept
    {
        return kSIZE;
    }

    /** Return an iterator to the first byte of the key buffer. */
    [[nodiscard]] const_iterator
    begin() const noexcept
    {
        return buf_;
    }

    /** Return a const iterator to the first byte of the key buffer. */
    [[nodiscard]] const_iterator
    cbegin() const noexcept
    {
        return buf_;
    }

    /** Return an iterator past the last byte of the key buffer. */
    [[nodiscard]] const_iterator
    end() const noexcept
    {
        return buf_ + kSIZE;
    }

    /** Return a const iterator past the last byte of the key buffer. */
    [[nodiscard]] const_iterator
    cend() const noexcept
    {
        return buf_ + kSIZE;
    }

    /** Return a `Slice` view over the 33-byte key buffer. */
    [[nodiscard]] Slice
    slice() const noexcept
    {
        return {buf_, kSIZE};
    }

    /** Implicit conversion to `Slice` for use with serialization APIs. */
    operator Slice() const noexcept
    {
        return slice();
    }
};

/** Write the public key as a hex string to a stream. */
std::ostream&
operator<<(std::ostream& os, PublicKey const& pk);

/** Return `true` if both keys hold identical 33-byte representations. */
inline bool
operator==(PublicKey const& lhs, PublicKey const& rhs)
{
    return std::memcmp(lhs.data(), rhs.data(), rhs.size()) == 0;
}

/** Return `true` if `lhs` is lexicographically less than `rhs`. */
inline bool
operator<(PublicKey const& lhs, PublicKey const& rhs)
{
    return std::lexicographical_compare(
        lhs.data(), lhs.data() + lhs.size(), rhs.data(), rhs.data() + rhs.size());
}

/** Feed the raw 33-byte key into a hash algorithm.
 *
 *  Enables `PublicKey` to be used as a key in unordered containers via
 *  `boost::hash` or any other `hash_append`-compatible hasher.
 *
 *  @tparam Hasher A `hash_append`-compatible hasher type.
 *  @param h The hasher to feed bytes into.
 *  @param pk The key whose bytes are appended.
 */
template <class Hasher>
void
hash_append(Hasher& h, PublicKey const& pk)
{
    h(pk.data(), pk.size());
}

/** Serialization bridge between `STBlob` fields and `PublicKey` values.
 *
 *  This specialization plugs `PublicKey` into XRPL's typed serialization
 *  framework. It allows `get<PublicKey>` and `set<PublicKey>` on `STBlob`
 *  fields in serialized ledger objects and transactions without any
 *  conversion boilerplate at call sites.
 */
template <>
struct STExchange<STBlob, PublicKey>
{
    explicit STExchange() = default;

    using value_type = PublicKey;

    /** Read a `PublicKey` from an `STBlob` field into `t`. */
    static void
    get(std::optional<value_type>& t, STBlob const& u)
    {
        t.emplace(Slice(u.data(), u.size()));
    }

    /** Write a `PublicKey` into a new `STBlob` for the given field. */
    static std::unique_ptr<STBlob>
    set(SField const& f, PublicKey const& t)
    {
        return std::make_unique<STBlob>(f, t.data(), t.size());
    }
};

//------------------------------------------------------------------------------

/** Encode a public key as a Base58Check string with a token-type prefix.
 *
 *  @param type The `TokenType` prefix to use (e.g. `TokenType::NodePublic`
 *      for validator keys, `TokenType::AccountPublic` for signing keys).
 *  @param pk The key to encode.
 *  @return The Base58Check-encoded string.
 */
inline std::string
toBase58(TokenType type, PublicKey const& pk)
{
    return encodeBase58Token(type, pk.data(), pk.size());
}

/** Decode a Base58Check-encoded public key.
 *
 *  Validates the token-type prefix and that the decoded bytes represent a
 *  recognized key format. Safe to call on untrusted input.
 *
 *  @param type The expected `TokenType` prefix.
 *  @param s The Base58Check-encoded string to decode.
 *  @return A `PublicKey` on success, or `std::nullopt` if the string is
 *      malformed, uses the wrong token type, or the decoded bytes are not
 *      a valid secp256k1 or Ed25519 key.
 */
template <>
std::optional<PublicKey>
parseBase58(TokenType type, std::string const& s);

/** Canonicality level of a DER-encoded secp256k1 ECDSA signature.
 *
 *  For any signed message, both `(R, S)` and `(R, G-S)` are mathematically
 *  valid ECDSA signatures (where G is the secp256k1 curve order). Accepting
 *  both enables transaction malleability attacks. XRPL prevents this by
 *  requiring *fully canonical* signatures — where `S ≤ G-S` — for new
 *  transactions.
 */
enum class ECDSACanonicality {
    /** Both R and S are in `[1, G)` with no redundant zero padding, but
     *  `S > G/2`. Structurally valid; may be accepted in legacy contexts. */
    Canonical,
    /** Both R and S are in `[1, G)` and `S ≤ G-S`, making the signature
     *  unique and immune to the malleability flip. Required for new XRPL
     *  transactions. */
    FullyCanonical
};

/** Determine the canonicality of a DER-encoded secp256k1 ECDSA signature.
 *
 *  Validates the DER structure (`0x30 <len> 0x02 <R> 0x02 <S>`), checks
 *  that R and S are properly encoded integers (no negative encoding, no
 *  redundant zero padding), and compares them against the secp256k1 curve
 *  order G. Returns `FullyCanonical` when `S ≤ G-S`, `Canonical` when
 *  `S > G-S` but the signature is otherwise structurally sound.
 *
 *  @param sig DER-encoded ECDSA signature to examine.
 *  @return `ECDSACanonicality::FullyCanonical` if `S ≤ G-S`,
 *      `ECDSACanonicality::Canonical` if `S > G-S` but structurally valid,
 *      or `std::nullopt` if the encoding is malformed (wrong header bytes,
 *      invalid integer components, R or S outside the curve order, or
 *      trailing bytes present).
 *  @note Only the structure and canonicality of the encoding are checked;
 *      no cryptographic verification is performed.
 */
std::optional<ECDSACanonicality>
ecdsaCanonicality(Slice const& sig);

/** Determine the algorithm encoded in a public key.
 *
 *  Uses the lead byte as a self-describing type tag: `0xED` → Ed25519;
 *  `0x02`/`0x03` → secp256k1 compressed. Any other lead byte, or a slice
 *  that is not exactly 33 bytes, is unrecognized.
 *
 *  @return The detected `KeyType`, or `std::nullopt` if the bytes do not
 *      match a known key format.
 */
/** @{ */
[[nodiscard]] std::optional<KeyType>
publicKeyType(Slice const& slice);

/** @copydoc publicKeyType(Slice const&) */
[[nodiscard]] inline std::optional<KeyType>
publicKeyType(PublicKey const& publicKey)
{
    return publicKeyType(publicKey.slice());
}
/** @} */

/** Verify a secp256k1 ECDSA signature against a pre-computed digest.
 *
 *  Validates DER structure and canonicality before calling libsecp256k1.
 *  When `mustBeFullyCanonical` is `false` and the signature is merely
 *  canonical (S > G/2), the S component is normalized to its low form via
 *  `secp256k1_ecdsa_signature_normalize` before verification — preserving
 *  backward compatibility without accepting truly malformed encodings.
 *
 *  @param publicKey A secp256k1 public key. Passing an Ed25519 key calls
 *      `LogicError` (programming error).
 *  @param digest The 256-bit digest over which the signature was produced.
 *  @param sig DER-encoded ECDSA signature.
 *  @param mustBeFullyCanonical If `true` (default), reject signatures where
 *      `S > G/2`. If `false`, accept them after S normalization.
 *  @return `true` if the signature is cryptographically valid for the given
 *      key and digest; `false` for any structural, canonicality, or
 *      cryptographic failure.
 */
[[nodiscard]] bool
verifyDigest(
    PublicKey const& publicKey,
    uint256 const& digest,
    Slice const& sig,
    bool mustBeFullyCanonical = true) noexcept;

/** Verify a signature over a raw message for either supported key type.
 *
 *  Dispatches on the cryptosystem detected from `publicKey`:
 *  - **secp256k1**: hashes `m` with SHA512-Half (256-bit digest) and
 *    delegates to `verifyDigest` with `mustBeFullyCanonical = true`.
 *  - **Ed25519**: checks that the signature scalar S is below the Ed25519
 *    subgroup order, then calls the underlying `ed25519_sign_open` library
 *    after stripping the XRPL-specific `0xED` prefix byte that the library
 *    does not understand.
 *
 *  @param publicKey The public key to verify against.
 *  @param m The message that was signed (raw bytes, not pre-hashed).
 *  @param sig The signature to verify.
 *  @return `true` if the signature is valid; `false` for any failure
 *      including unrecognized key type, non-canonical signature, or
 *      cryptographic mismatch.
 */
[[nodiscard]] bool
verify(PublicKey const& publicKey, Slice const& m, Slice const& sig) noexcept;

/** Derive the 160-bit node identity from a public key.
 *
 *  Applies RIPEMD-160(SHA-256(pubkey)) to produce the `NodeID` used in the
 *  peer-to-peer layer for validator routing and consensus tracking.
 *
 *  @param pk The validator's public key (secp256k1 or Ed25519).
 *  @return The 160-bit `NodeID` identifying the validator on the network.
 */
NodeID
calcNodeID(PublicKey const&);

/** Derive the 160-bit on-ledger account address from a public key.
 *
 *  Applies RIPEMD-160(SHA-256(pubkey)) — the same algorithm used in
 *  Bitcoin — to produce the `AccountID` that identifies the account on the
 *  XRP Ledger.
 *
 *  @param pk The account's public key.
 *  @return The `AccountID` corresponding to `pk`.
 *  @note The implementation lives in `AccountID.cpp` rather than
 *      `PublicKey.cpp` due to header dependency ordering constraints.
 */
// VFALCO This belongs in AccountID.h but
//        is here because of header issues
AccountID
calcAccountID(PublicKey const& pk);

/** Format a human-readable peer fingerprint for diagnostic logging.
 *
 *  Produces a string of the form
 *  `"IP Address: <addr>[, Public Key: <NodePublic>][, Id: <id>]"` suitable
 *  for audit and connection-lifecycle log messages.
 *
 *  @param address The peer's IP endpoint (always included).
 *  @param publicKey The peer's node public key, encoded as `NodePublic`
 *      Base58; omitted if not yet known (e.g., before the handshake).
 *  @param id An optional session identifier string; omitted if absent.
 *  @return A formatted fingerprint string.
 */
inline std::string
getFingerprint(
    beast::IP::Endpoint const& address,
    std::optional<PublicKey> const& publicKey = std::nullopt,
    std::optional<std::string> const& id = std::nullopt)
{
    std::stringstream ss;
    ss << "IP Address: " << address;
    if (publicKey.has_value())
    {
        ss << ", Public Key: " << toBase58(TokenType::NodePublic, *publicKey);
    }
    if (id.has_value())
    {
        ss << ", Id: " << id.value();
    }
    return ss.str();
}
}  // namespace xrpl

//------------------------------------------------------------------------------

/** Deserialize a `PublicKey` from a JSON field value.
 *
 *  Accepts three formats in order:
 *  1. Lowercase hex string of the raw 33-byte key.
 *  2. `NodePublic` Base58Check encoding (validator keys).
 *  3. `AccountPublic` Base58Check encoding (signing keys).
 *
 *  This covers the variety of formats that appear in RPC requests and
 *  configuration files.
 *
 *  @param v The JSON object to read from.
 *  @param field The field whose value is decoded.
 *  @return The decoded `PublicKey`.
 *  @throws `JsonTypeMismatchError` if the field value does not match any
 *      recognized format.
 */
template <>
inline xrpl::PublicKey
getOrThrow(json::Value const& v, xrpl::SField const& field)
{
    using namespace xrpl;
    std::string const b58 = getOrThrow<std::string>(v, field);
    if (auto pubKeyBlob = strUnHex(b58);
        pubKeyBlob.has_value() && publicKeyType(makeSlice(*pubKeyBlob)))
    {
        return PublicKey{makeSlice(*pubKeyBlob)};
    }
    for (auto const tokenType : {TokenType::NodePublic, TokenType::AccountPublic})
    {
        if (auto const pk = parseBase58<PublicKey>(tokenType, b58))
            return *pk;
    }
    Throw<JsonTypeMismatchError>(field.getJsonName(), "PublicKey");
}
}  // namespace json
