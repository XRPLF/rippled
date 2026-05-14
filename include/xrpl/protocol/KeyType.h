/** @file
 *  Discriminant enum and conversion utilities for XRPL's two signature schemes.
 *
 *  Every key-management function in the protocol (generation, signing,
 *  verification) accepts a `KeyType` to select between the secp256k1 and
 *  ed25519 algorithms. This header is included by virtually every
 *  cryptographic interface in the protocol layer.
 */

#pragma once

#include <optional>
#include <string>

namespace xrpl {

/** Selects the cryptographic signature algorithm for a key pair.
 *
 *  The XRPL supports two independent signature schemes: the Bitcoin-lineage
 *  secp256k1 elliptic curve and the modern ed25519 Edwards curve.  The
 *  explicit integer values provide stable identifiers for internal storage and
 *  configuration that maps an integer to a key type, even though `KeyType`
 *  itself is not serialized directly on the wire.
 *
 *  The choice of algorithm is self-describing in serialized key material:
 *  secp256k1 public keys begin with a compressed-point prefix byte (`0x02` or
 *  `0x03`), while ed25519 public keys carry a sentinel byte `0xED`.
 *
 *  @see publicKeyType for recovering the algorithm from a serialized public key.
 */
enum class KeyType {
    Secp256k1 = 0, /**< Bitcoin-lineage elliptic curve; XRPL uses a custom
                        seed-to-key-pair derivation path. */
    Ed25519 = 1,   /**< Modern Edwards curve; uses a direct derivation from the
                        seed with no intermediate generator step. */
};

/** Parse a canonical string name into a `KeyType`.
 *
 *  Recognises exactly `"secp256k1"` and `"ed25519"` (lower-case).  Returns an
 *  empty optional for any other input rather than throwing, so callers at
 *  configuration-parse or RPC-request boundaries can compose the result with
 *  their own error-reporting logic without requiring exception handling.
 *
 *  @param s The string to parse.
 *  @return The corresponding `KeyType`, or `std::nullopt` if `s` is not a
 *      recognised algorithm name.
 */
inline std::optional<KeyType>
keyTypeFromString(std::string const& s)
{
    if (s == "secp256k1")
        return KeyType::Secp256k1;

    if (s == "ed25519")
        return KeyType::Ed25519;

    return {};
}

/** Return the canonical lower-case string name for a `KeyType`.
 *
 *  Returns `"INVALID"` — rather than `nullptr` or undefined behaviour — for
 *  any value that matches neither known enumerator.  This defensive path is
 *  reachable because C++ permits arbitrary integers to be cast to an
 *  `enum class`, so a corrupt or adversarially crafted value must not cause
 *  unsafe access in logging or diagnostic paths.
 *
 *  @param type The key type to convert.
 *  @return `"secp256k1"`, `"ed25519"`, or `"INVALID"`.
 */
inline char const*
to_string(KeyType type)
{
    if (type == KeyType::Secp256k1)
        return "secp256k1";

    if (type == KeyType::Ed25519)
        return "ed25519";

    return "INVALID";
}

/** Write a `KeyType` to a stream as its canonical string name.
 *
 *  Templated on `Stream` rather than fixed to `std::ostream` so the operator
 *  works with Beast logging streams, test-harness formatters, and any other
 *  stream-like type without coupling this header to a concrete stream
 *  hierarchy.
 *
 *  @tparam Stream Any type that supports `operator<<(char const*)`.
 *  @param s    The destination stream.
 *  @param type The key type to write.
 *  @return `s`, to allow chaining.
 */
template <class Stream>
inline Stream&
operator<<(Stream& s, KeyType type)
{
    return s << to_string(type);
}

}  // namespace xrpl
