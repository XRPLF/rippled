#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/crypto/secure_erase.h>

#include <boost/endian/conversion.hpp>

#include <array>

namespace xrpl {

/** @file
 *  Cryptographic digest primitives for the XRPL protocol layer.
 *
 *  Defines all hasher structs used to compute ledger object identifiers,
 *  transaction IDs, account addresses, and signing payloads. Every type
 *  satisfies the `Hasher` concept from N3980 ("Types Don't Know #"), enabling
 *  `beast::hash_append` to drive any of them generically.
 *
 *  @see https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2014/n3980.html
 */

//------------------------------------------------------------------------------

/** RIPEMD-160 hasher backed by the OpenSSL `RIPEMD160_CTX` implementation.
 *
 *  Satisfies the `Hasher` concept: feed data via `operator()`, then extract the
 *  20-byte digest with `explicit operator result_type()`.
 *
 *  The OpenSSL context is stored in an opaque `char ctx_[96]` buffer so that
 *  this header never needs to include any OpenSSL headers. A `static_assert` in
 *  the constructor verifies the buffer size matches `sizeof(RIPEMD160_CTX)` at
 *  compile time; if an OpenSSL upgrade changes the struct size the build fails
 *  rather than silently corrupting memory.
 *
 *  @note Prefer the `ripemd160_hasher` alias over this name at call sites.
 */
struct OpensslRipemd160Hasher
{
public:
    static constexpr auto const kENDIAN = boost::endian::order::native;

    using result_type = std::array<std::uint8_t, 20>;

    OpensslRipemd160Hasher();

    /** Feed bytes into the running digest.
     *
     *  @param data Pointer to the input bytes.
     *  @param size Number of bytes to consume.
     */
    void
    operator()(void const* data, std::size_t size) noexcept;

    /** Finalize and return the 20-byte RIPEMD-160 digest. */
    explicit
    operator result_type() noexcept;

private:
    char ctx_[96]{};
};

/** SHA-512 hasher backed by the OpenSSL `SHA512_CTX` implementation.
 *
 *  Satisfies the `Hasher` concept: feed data via `operator()`, then extract the
 *  64-byte digest with `explicit operator result_type()`.
 *
 *  Like `OpensslRipemd160Hasher`, the context is stored in an opaque
 *  `char ctx_[216]` buffer to avoid exposing OpenSSL headers, with a
 *  compile-time size check in the constructor.
 *
 *  @note Prefer the `sha512_hasher` alias over this name at call sites. Most
 *      callers should use `sha512_half_hasher` / `sha512Half()` instead, which
 *      truncate the output to the 256-bit XRPL canonical form.
 */
struct OpensslSha512Hasher
{
public:
    static constexpr auto const kENDIAN = boost::endian::order::native;

    using result_type = std::array<std::uint8_t, 64>;

    OpensslSha512Hasher();

    /** Feed bytes into the running digest.
     *
     *  @param data Pointer to the input bytes.
     *  @param size Number of bytes to consume.
     */
    void
    operator()(void const* data, std::size_t size) noexcept;

    /** Finalize and return the 64-byte SHA-512 digest. */
    explicit
    operator result_type() noexcept;

private:
    char ctx_[216]{};
};

/** SHA-256 hasher backed by the OpenSSL `SHA256_CTX` implementation.
 *
 *  Satisfies the `Hasher` concept: feed data via `operator()`, then extract the
 *  32-byte digest with `explicit operator result_type()`.
 *
 *  Like `OpensslRipemd160Hasher`, the context is stored in an opaque
 *  `char ctx_[112]` buffer to avoid exposing OpenSSL headers, with a
 *  compile-time size check in the constructor.
 *
 *  @note Prefer the `sha256_hasher` alias over this name at call sites.
 */
struct OpensslSha256Hasher
{
public:
    static constexpr auto const kENDIAN = boost::endian::order::native;

    using result_type = std::array<std::uint8_t, 32>;

    OpensslSha256Hasher();

    /** Feed bytes into the running digest.
     *
     *  @param data Pointer to the input bytes.
     *  @param size Number of bytes to consume.
     */
    void
    operator()(void const* data, std::size_t size) noexcept;

    /** Finalize and return the 32-byte SHA-256 digest. */
    explicit
    operator result_type() noexcept;

private:
    char ctx_[112]{};
};

//------------------------------------------------------------------------------

/** Implementation-neutral alias for the RIPEMD-160 hasher. */
using ripemd160_hasher = OpensslRipemd160Hasher;

/** Implementation-neutral alias for the SHA-256 hasher. */
using sha256_hasher = OpensslSha256Hasher;

/** Implementation-neutral alias for the SHA-512 hasher. */
using sha512_hasher = OpensslSha512Hasher;

//------------------------------------------------------------------------------

/** Hasher that computes RIPEMD-160(SHA-256(msg)) — the XRPL account ID formula.
 *
 *  This is the Bitcoin-lineage two-pass construction used throughout XRPL to
 *  derive a 160-bit `AccountID` from a public key. Data is accumulated into a
 *  `sha256_hasher`; on conversion, SHA-256 is finalized and its 32-byte output
 *  is immediately fed into a fresh `ripemd160_hasher` — no intermediate buffer
 *  escapes the function.
 *
 *  The formula is deliberately key-type-agnostic: both secp256k1 and Ed25519
 *  public keys are hashed the same way, decoupling account addresses from the
 *  underlying cryptographic scheme. Future key types can be added without
 *  changing the address derivation.
 *
 *  Satisfies the `Hasher` concept (N3980 `hash_append` interface).
 *
 *  @see calcAccountID() in AccountID.h
 */
struct RipeshaHasher
{
private:
    sha256_hasher h_;

public:
    static constexpr auto const kENDIAN = boost::endian::order::native;

    using result_type = std::array<std::uint8_t, 20>;

    /** Feed bytes into the running SHA-256 accumulator.
     *
     *  @param data Pointer to the input bytes.
     *  @param size Number of bytes to consume.
     */
    void
    operator()(void const* data, std::size_t size) noexcept
    {
        h_(data, size);
    }

    /** Finalize SHA-256, hash its output through RIPEMD-160, and return the
     *  20-byte account ID digest.
     */
    explicit
    operator result_type() noexcept
    {
        auto const d0 = sha256_hasher::result_type(h_);
        ripemd160_hasher rh;
        rh(d0.data(), d0.size());
        return ripemd160_hasher::result_type(rh);
    }
};

//------------------------------------------------------------------------------

namespace detail {

/** SHA-512-Half hasher: computes SHA-512 and returns the first 256 bits as a
 *  `uint256`.
 *
 *  SHA-512 is used (rather than SHA-256) because it is faster on 64-bit
 *  hardware due to wider register operations, while truncating to 256 bits
 *  still provides strong security. This is the dominant hash construction
 *  across XRPL's protocol layer — transaction IDs, ledger node hashes, signing
 *  payloads, and manifest digests all use it.
 *
 *  `kENDIAN = big` ensures that when `beast::hash_append` serializes multi-byte
 *  integers before feeding them to this hasher, the bytes are in network
 *  (big-endian) order — matching the XRPL wire format.
 *
 *  @tparam Secure When `true`, the destructor calls `secure_erase()` on the
 *      internal SHA-512 context to prevent sensitive key material from
 *      lingering in memory. When `false`, the destructor is a no-op with zero
 *      overhead. Use `sha512_half_hasher_s` (Secure=true) when hashing
 *      private keys or seed material; use `sha512_half_hasher` (Secure=false)
 *      for all other ledger computations.
 */
template <bool Secure>
struct BasicSha512HalfHasher
{
private:
    sha512_hasher h_;

public:
    static constexpr auto const kENDIAN = boost::endian::order::big;

    using result_type = uint256;

    ~BasicSha512HalfHasher()
    {
        erase(std::integral_constant<bool, Secure>{});
    }

    /** Feed bytes into the running SHA-512 accumulator.
     *
     *  @param data Pointer to the input bytes.
     *  @param size Number of bytes to consume.
     */
    void
    operator()(void const* data, std::size_t size) noexcept
    {
        h_(data, size);
    }

    /** Finalize SHA-512 and return the first 256 bits as a `uint256`. */
    explicit
    operator result_type() noexcept
    {
        auto const digest = sha512_hasher::result_type(h_);
        return result_type::fromVoid(digest.data());
    }

private:
    void
    erase(std::false_type)
    {
    }

    void
    erase(std::true_type)
    {
        secureErase(&h_, sizeof(h_));
    }
};

}  // namespace detail

/** Standard SHA-512-Half hasher for ledger computations.
 *
 *  The destructor is a no-op. Use this for transaction IDs, ledger node
 *  hashes, and any non-sensitive protocol payload. Use `sha512_half_hasher_s`
 *  when hashing key or seed material.
 */
using sha512_half_hasher = detail::BasicSha512HalfHasher<false>;

/** Secure SHA-512-Half hasher that zeroes internal state on destruction.
 *
 *  Identical to `sha512_half_hasher` except the destructor calls
 *  `secure_erase()` on the embedded SHA-512 context, preventing sensitive
 *  material from remaining in memory after the hasher goes out of scope.
 */
using sha512_half_hasher_s = detail::BasicSha512HalfHasher<true>;

//------------------------------------------------------------------------------

/** Compute the SHA-512-Half of one or more objects and return a `uint256`.
 *
 *  Constructs a `sha512_half_hasher`, drives it with `beast::hash_append` over
 *  all arguments in order, and returns the 256-bit result. Because
 *  `hash_append` is overloaded for all XRPL protocol types — including
 *  `HashPrefix`, `STObject`, `Serializer`, and primitive integers — a single
 *  call can serialize and hash an entire transaction or ledger node.
 *
 *  Always prepend a `HashPrefix` constant as the first argument to enforce
 *  domain separation and prevent cross-context hash collisions.
 *
 *  @param args One or more `hash_append`-compatible values to hash in order.
 *  @return The first 256 bits of the SHA-512 digest of the serialized input.
 *
 *  @see sha512HalfS() for the secure variant that zeroes internal state.
 *  @see HashPrefix.h for the domain-separation prefix constants.
 */
template <class... Args>
sha512_half_hasher::result_type
sha512Half(Args const&... args)
{
    sha512_half_hasher h;
    using beast::hash_append;
    hash_append(h, args...);
    return static_cast<typename sha512_half_hasher::result_type>(h);
}

/** Compute the SHA-512-Half of one or more objects, zeroing internal state
 *  after extraction.
 *
 *  Identical to `sha512Half()` except the internal `sha512_half_hasher_s`
 *  context is erased via `secure_erase()` on destruction, preventing
 *  sensitive material (e.g., a private key or seed) from lingering on the
 *  stack after the call returns.
 *
 *  @param args One or more `hash_append`-compatible values to hash in order.
 *  @return The first 256 bits of the SHA-512 digest of the serialized input.
 *
 *  @see sha512Half() for the non-secure, lower-overhead variant.
 */
template <class... Args>
sha512_half_hasher_s::result_type
sha512HalfS(Args const&... args)
{
    sha512_half_hasher_s h;
    using beast::hash_append;
    hash_append(h, args...);
    return static_cast<typename sha512_half_hasher_s::result_type>(h);
}

}  // namespace xrpl
