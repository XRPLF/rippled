/**
 * @file tokens.cpp
 * @brief Base58Check encoding and decoding for XRPL identifiers.
 *
 * This file is the single source of truth for XRPL's Base58Check scheme —
 * the algorithm that converts raw binary account IDs, key pairs, and seeds
 * into the human-readable strings users interact with every day
 * (e.g., `rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh`).
 *
 * Every encoded XRPL identifier follows the wire layout:
 * @code
 *   [ type byte (1) ][ raw payload (N) ][ checksum (4) ]
 * @endcode
 * The checksum is the first four bytes of SHA-256(SHA-256(type || payload)),
 * identical to Bitcoin's checksum design.
 *
 * Two internal implementations are provided and selected at compile time:
 * - `b58_ref` — portable O(n²) algorithm adapted from Bitcoin Core.
 * - `b58_fast` — 10-15× faster path using GCC's `unsigned __int128` extension;
 *   unavailable on MSVC, which falls back to `b58_ref`.
 *
 * The fast algorithm stages the conversion as:
 * @code
 *   base 58 → base 58^10 → base 2^64 → base 2^8
 * @endcode
 * `58^10 = 430804206899405824` is the largest power of 58 that fits in a
 * 64-bit register, enabling the first hop to use simple 64-bit arithmetic.
 * The multi-precision second hop then operates on far fewer, larger
 * coefficients, dramatically reducing total work.
 *
 * The base58 encoding & decoding routines in the b58_ref namespace are taken
 * from Bitcoin but have been modified from the original.
 *
 * Copyright (c) 2014 The Bitcoin Core developers
 * Distributed under the MIT software license, see the accompanying
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.
 */

#include <xrpl/protocol/tokens.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/detail/b58_utils.h>
#include <xrpl/protocol/detail/token_errors.h>
#include <xrpl/protocol/digest.h>

#include <boost/container/small_vector.hpp>
#include <boost/endian/conversion.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

/*
Converting between bases is straight forward. First, some background:

Given the coefficients C[m], ... ,C[0] and base B, those coefficients represent
the number C[m]*B^m + ... + C[0]*B^0; The following pseudo-code converts the
coefficients to the (infinite precision) integer N:

```
N = 0;
i = m ;; N.B. m is the index of the largest coefficient
while (i>=0)
    N = N + C[i]*B^i
    i = i - 1
```

For example, in base 10, the number 437 represents the integer 4*10^2 + 3*10^1 +
7*10^0. In base 16, 437 is the same as 4*16^2 + 3*16^1 + 7*16^0.

To find the coefficients that represent the integer N in base B, we start by
computing the lowest order coefficients and work up to the highest order
coefficients. The following pseudo-code converts the (infinite precision)
integer N to the correct coefficients:

```
i = 0
while(N)
    C[i] = N mod B
    N = floor(N/B)
    i = i + 1
```

For example, to find the coefficients of the integer 437 in base 10:

C[0] is 437 mod 10; C[0] = 7;
N is floor(437/10); N = 43;
C[1] is 43 mod 10; C[1] = 3;
N is floor(43/10); N = 4;
C[2] is 4 mod 10; C[2] = 4;
N is floor(4/10); N = 0;
Since N is 0, the algorithm stops.


To convert between a number represented with coefficients from base B1 to that
same number represented with coefficients from base B2, we can use the algorithm
that converts coefficients from base B1 to an integer, and then use the
algorithm that converts a number to coefficients from base B2.

There is a useful shortcut that can be used if one of the bases is a power of
the other base. If B1 == B2^G, then each coefficient from base B1 can be
converted to base B2 independently to create a group of "G" B2 coefficient.
These coefficients can be simply concatenated together. Since 16 == 2^4, this
property is what makes base 16 useful when dealing with binary numbers. For
example consider converting the base 16 number "93" to binary. The base 16
coefficient 9 is represented in base 2 with the coefficients 1,0,0,1. The base
16 coefficient 3 is represented in base 2 with the coefficients 0,0,1,1. To get
the final answer, just concatenate those two independent conversions together.
The base 16 number "93" is the binary number "10010011".

The original (now reference) algorithm to convert from base 58 to a binary
number used the

```
N = 0;
for i in m to 0 inclusive
    N = N + C[i]*B^i
```

algorithm.

However, the algorithm above is pseudo-code. In particular, the variable "N" is
an infinite precision integer in that pseudo-code. Real computers do
computations on registers, and these registers have limited length. Modern
computers use 64-bit general purpose registers, and can multiply two 64 bit
numbers and obtain a 128 bit result (in two registers).

The original algorithm in essence converted from base 58 to base 256 (base
2^8). The new, faster algorithm converts from base 58 to base 58^10 (this is
fast using the shortcut described above), then from base 58^10 to base 2^64
(this is slow, and requires multi-precision arithmetic), and then from base 2^64
to base 2^8 (this is fast, using the shortcut described above). Base 58^10 is
chosen because it is the largest power of 58 that will fit into a 64-bit
register.

While it may seem counter-intuitive that converting from base 58 -> base 58^10
-> base 2^64 -> base 2^8 is faster than directly converting from base 58 -> base
2^8, it is actually 10x-15x faster. The reason for the speed increase is two of
the conversions are trivial (converting between bases where one base is a power
of another base), and doing the multi-precision computations with larger
coefficients sizes greatly speeds up the multi-precision computations.
*/

namespace xrpl {

/** The 58-character XRPL Base58 alphabet.
 *
 *  Deliberately chosen so that an `AccountID` of all-zero bytes encodes to a
 *  string beginning with `'r'`, letting users and validators visually
 *  recognize a classic XRPL account address without decoding.
 */
static constexpr char const* kALPHABET_FORWARD =
    "rpshnaf39wBUDNEGHJKLM4PQRST7VWXYZ2bcdeCg65jkm8oFqi1tuvAxyz";

/** Compile-time reverse lookup table for the XRPL Base58 alphabet.
 *
 *  Maps each ASCII byte value to its 0-based index in `kALPHABET_FORWARD`,
 *  or `-1` if the character is not a valid Base58 digit.  Used by the
 *  decoder to convert encoded characters back to numeric coefficients
 *  without a linear search.
 */
static constexpr std::array<int, 256> const kALPHABET_REVERSE = []() {
    std::array<int, 256> map{};
    for (auto& m : map)
        m = -1;
    for (int i = 0, j = 0; kALPHABET_FORWARD[i] != 0; ++i)
        map[static_cast<unsigned char>(kALPHABET_FORWARD[i])] = j++;
    return map;
}();

/** Hash a raw buffer with the given Hasher.
 *
 *  @tparam Hasher A type satisfying the XRP hasher concept — default-constructible,
 *      callable with `(void const*, size_t)`, and convertible to its `result_type`.
 *  @param data Pointer to the bytes to hash.
 *  @param size Number of bytes to hash.
 *  @return The hash digest.
 */
template <class Hasher>
static typename Hasher::result_type
digest(void const* data, std::size_t size) noexcept
{
    Hasher h;
    h(data, size);
    return static_cast<typename Hasher::result_type>(h);
}

/** Hash a fixed-size byte array with the given Hasher.
 *
 *  Convenience overload for `std::array<T, N>` where `sizeof(T) == 1`.
 *
 *  @tparam Hasher A type satisfying the XRP hasher concept.
 *  @tparam T Byte-sized element type.
 *  @tparam N Array length.
 *  @param v The byte array to hash.
 *  @return The hash digest.
 */
template <class Hasher, class T, std::size_t N, class = std::enable_if_t<sizeof(T) == 1>>
static typename Hasher::result_type
digest(std::array<T, N> const& v)
{
    return digest<Hasher>(v.data(), v.size());
}

/** Compute the hash of a hash (double-digest).
 *
 *  Applies the Hasher twice: first to produce an intermediate digest from
 *  `args`, then hashes that digest again.  The result is the same as
 *  `digest<Hasher>(digest<Hasher>(args...))`.
 *
 *  @tparam Hasher A type satisfying the XRP hasher concept.
 *  @tparam Args  Arguments forwarded to the inner `digest` overload.
 *  @param args   Data arguments passed to the inner digest computation.
 *  @return The double-digest result.
 */
template <class Hasher, class... Args>
static typename Hasher::result_type
digest2(Args const&... args)
{
    return digest<Hasher>(digest<Hasher>(args...));
}

/** Calculate a 4-byte checksum of the data

    The checksum is calculated as the first 4 bytes
    of the SHA256 digest of the message. This is added
    to the base58 encoding of identifiers to detect
    user error in data entry.

    @note This checksum algorithm is part of the client API
*/
static void
checksum(void* out, void const* message, std::size_t size)
{
    auto const h = digest2<sha256_hasher>(message, size);
    std::memcpy(out, h.data(), 4);
}

/** Encode a raw payload as a Base58Check XRPL token string.
 *
 *  Prepends the `type` byte, appends a 4-byte double-SHA256 checksum, then
 *  Base58-encodes the result using the XRPL alphabet.  Dispatches to the
 *  fast (`b58_fast`) implementation on non-MSVC compilers, and to the
 *  portable reference implementation (`b58_ref`) on MSVC.
 *
 *  @param type  The `TokenType` version byte that prefixes the encoded form
 *      (e.g., `TokenType::AccountID = 0` → addresses start with `'r'`).
 *  @param token Pointer to the raw payload bytes to encode.
 *  @param size  Number of payload bytes.
 *  @return The Base58Check-encoded string, or an empty string on error.
 */
[[nodiscard]] std::string
encodeBase58Token(TokenType type, void const* token, std::size_t size)
{
#ifndef _MSC_VER
    return b58_fast::encodeBase58Token(type, token, size);
#else
    return b58_ref::encodeBase58Token(type, token, size);
#endif
}

/** Decode a Base58Check XRPL token string and return the raw payload.
 *
 *  Decodes the Base58 string, then validates:
 *  1. The decoded length is at least 6 bytes (1 type + 1 payload min + 4 checksum).
 *  2. The leading type byte matches `type`.
 *  3. The trailing 4-byte checksum is correct.
 *
 *  Dispatches to the fast (`b58_fast`) implementation on non-MSVC compilers,
 *  and to the portable reference implementation (`b58_ref`) on MSVC.
 *
 *  @param s    The Base58Check-encoded string to decode.
 *  @param type The expected `TokenType` version byte.
 *  @return The raw payload (type byte and checksum stripped), or an empty
 *      string if the input is invalid, the type does not match, or the
 *      checksum fails.
 */
[[nodiscard]] std::string
decodeBase58Token(std::string const& s, TokenType type)
{
#ifndef _MSC_VER
    return b58_fast::decodeBase58Token(s, type);
#else
    return b58_ref::decodeBase58Token(s, type);
#endif
}

namespace b58_ref {

namespace detail {

/** Encode a raw byte buffer as a Base58 string (reference O(n²) algorithm).
 *
 *  Uses the Bitcoin-derived algorithm: for each input byte the working
 *  base-58 buffer is multiplied by 256 and the byte added as carry.
 *  Leading zero bytes map to the first alphabet character (`'r'`) to
 *  preserve bijectivity.
 *
 *  @param message  Pointer to the data to encode.
 *  @param size     Number of bytes in `message`.
 *  @param temp     Caller-supplied scratch buffer; must be at least
 *      `size * 138 / 100 + 1` bytes (derived from log(256)/log(58) ≈ 1.38).
 *  @param tempSize Size of `temp` in bytes.
 *  @return The Base58-encoded string.
 *  @note Exposed in the header for unit-testing comparison against the fast path.
 */
std::string
encodeBase58(void const* message, std::size_t size, void* temp, std::size_t tempSize)
{
    auto pbegin = reinterpret_cast<unsigned char const*>(message);
    auto const pend = pbegin + size;

    // Skip & count leading zeroes.
    int zeroes = 0;
    while (pbegin != pend && *pbegin == 0)
    {
        pbegin++;
        zeroes++;
    }

    auto const b58begin = reinterpret_cast<unsigned char*>(temp);
    auto const b58end = b58begin + tempSize;

    std::fill(b58begin, b58end, 0);

    while (pbegin != pend)
    {
        int carry = *pbegin;
        // Apply "b58 = b58 * 256 + ch".
        for (auto iter = b58end; iter != b58begin; --iter)
        {
            carry += 256 * (iter[-1]);
            iter[-1] = carry % 58;
            carry /= 58;
        }
        XRPL_ASSERT(carry == 0, "xrpl::b58_ref::detail::encodeBase58 : zero carry");
        pbegin++;
    }

    // Skip leading zeroes in base58 result.
    auto iter = b58begin;
    while (iter != b58end && *iter == 0)
        ++iter;

    // Translate the result into a string.
    std::string str;
    str.reserve(zeroes + (b58end - iter));
    str.assign(zeroes, kALPHABET_FORWARD[0]);
    while (iter != b58end)
        str += kALPHABET_FORWARD[*(iter++)];
    return str;
}

/** Decode a Base58 string to raw bytes (reference O(n²) algorithm).
 *
 *  Reverses `encodeBase58`: for each input character the working base-256
 *  buffer is multiplied by 58 and the digit value added as carry.
 *  Leading `'r'` characters (index 0 in the alphabet) map back to leading
 *  zero bytes.
 *
 *  @param s The Base58-encoded string to decode.
 *  @return The decoded byte string, or an empty string if any character is
 *      not in the XRPL alphabet or if the input exceeds 64 characters.
 *  @note Exposed in the header for unit-testing comparison against the fast path.
 */
std::string
decodeBase58(std::string const& s)
{
    auto psz = reinterpret_cast<unsigned char const*>(s.c_str());
    auto remain = s.size();
    // Skip and count leading zeroes
    int zeroes = 0;
    while (remain > 0 && kALPHABET_REVERSE[*psz] == 0)
    {
        ++zeroes;
        ++psz;
        --remain;
    }

    if (remain > 64)
        return {};

    // Allocate enough space in big-endian base256 representation.
    // log(58) / log(256), rounded up.
    std::vector<unsigned char> b256((remain * 733 / 1000) + 1);
    while (remain > 0)
    {
        auto carry = kALPHABET_REVERSE[*psz];
        if (carry == -1)
            return {};
        // Apply "b256 = b256 * 58 + carry".
        for (auto iter = b256.rbegin(); iter != b256.rend(); ++iter)
        {
            carry += 58 * *iter;
            *iter = carry % 256;
            carry /= 256;
        }
        XRPL_ASSERT(carry == 0, "xrpl::b58_ref::detail::decodeBase58 : zero carry");
        ++psz;
        --remain;
    }
    // Skip leading zeroes in b256.
    auto iter = std::ranges::find_if(b256, [](unsigned char c) { return c != 0; });
    std::string result;
    result.reserve(zeroes + (b256.end() - iter));
    result.assign(zeroes, 0x00);
    while (iter != b256.end())
        result.push_back(*(iter++));
    return result;
}

}  // namespace detail

/** Encode a raw payload as a Base58Check XRPL token string (reference implementation).
 *
 *  Constructs the wire layout `[type][payload][checksum]` in a stack-allocated
 *  buffer, then delegates to `detail::encodeBase58`.  The scratch buffer is
 *  sized at `expanded * 3` bytes, which is a safe upper bound derived from
 *  `log(256) / log(58) ≈ 1.38` plus one character of slack.
 *
 *  @param type  The `TokenType` version byte to prepend.
 *  @param token Pointer to the payload bytes.
 *  @param size  Number of payload bytes.
 *  @return The Base58Check-encoded string.
 */
std::string
encodeBase58Token(TokenType type, void const* token, std::size_t size)
{
    // expanded token includes type + 4 byte checksum
    auto const expanded = 1 + size + 4;

    // We need expanded + expanded * (log(256) / log(58)) which is
    // bounded by expanded + expanded * (138 / 100 + 1) which works
    // out to expanded * 3:
    auto const bufsize = expanded * 3;

    boost::container::small_vector<std::uint8_t, 1024> buf(bufsize);

    buf[0] = safeCast<std::underlying_type_t<TokenType>>(type);
    if (size != 0u)
        std::memcpy(buf.data() + 1, token, size);
    checksum(buf.data() + 1 + size, buf.data(), 1 + size);

    return detail::encodeBase58(buf.data(), expanded, buf.data() + expanded, bufsize - expanded);
}

/** Decode a Base58Check XRPL token string and return the raw payload (reference implementation).
 *
 *  Validates decoded length (≥ 6 bytes), the leading type byte, and the
 *  trailing 4-byte checksum.  Returns an empty string on any mismatch.
 *
 *  @param s    The Base58Check-encoded string to decode.
 *  @param type The expected `TokenType` version byte.
 *  @return The raw payload with the type byte and checksum stripped, or an
 *      empty string if validation fails.
 */
std::string
decodeBase58Token(std::string const& s, TokenType type)
{
    std::string const ret = detail::decodeBase58(s);

    // Reject zero length tokens
    if (ret.size() < 6)
        return {};

    // The type must match.
    if (type != safeCast<TokenType>(static_cast<std::uint8_t>(ret[0])))
        return {};

    // And the checksum must as well.
    std::array<char, 4> guard{};
    checksum(guard.data(), ret.data(), ret.size() - guard.size());
    if (!std::equal(guard.rbegin(), guard.rend(), ret.rbegin()))
        return {};

    // Skip the leading type byte and the trailing checksum.
    return ret.substr(1, ret.size() - 1 - guard.size());
}
}  // namespace b58_ref

#ifndef _MSC_VER
// The algorithms use gcc's int128 (fast MS version will have to wait, in the
// meantime MS falls back to the slower reference implementation)
namespace b58_fast {
namespace detail {

/** Encode big-endian binary data as a big-endian Base58 byte sequence (fast path).
 *
 *  Implements the three-hop conversion `base 256 → base 2^64 → base 58^10 → base 58`
 *  using GCC's `unsigned __int128` for carry-free multi-precision arithmetic:
 *
 *  1. The input bytes are loaded into at most 5 `uint64_t` limbs (little-endian
 *     coefficient order), representing the value in base 2^64.
 *  2. The 2^64 limb array is repeatedly divided by `58^10 = 430804206899405824`
 *     via `inplaceBigintDivRem` to extract base-58^10 coefficients.
 *  3. Each base-58^10 coefficient is expanded into 10 base-58 digits via
 *     `b5810ToB58Be` and mapped through `kALPHABET_FORWARD`.
 *
 *  Leading zero bytes in `input` each emit the alphabet's first character
 *  (`'r'`) to preserve the bijective encoding property.
 *
 *  @note Both `input` and `out` are in big-endian byte order.
 *  @note Maximum valid `input` is 38 bytes (33-byte public key + 1 type + 4 checksum).
 *      Larger inputs return `TokenCodecErrc::InputTooLarge`.
 *
 *  @param input The big-endian binary data to encode.
 *  @param out   Caller-supplied output buffer; must be large enough for the
 *      encoded result (≈ ceil(len×log(256)/log(58)) + leading-zero count).
 *      Returns `TokenCodecErrc::OutputTooSmall` if insufficient.
 *  @return On success, a subspan of `out` containing the encoded Base58 bytes.
 *      On failure, an unexpected `TokenCodecErrc` error code.
 */
B58Result<std::span<std::uint8_t>>
b256ToB58Be(std::span<std::uint8_t const> input, std::span<std::uint8_t> out)
{
    // Max valid input is 38 bytes:
    // (33 bytes for nodepublic + 1 byte token + 4 bytes checksum)
    if (input.size() > 38)
    {
        return Unexpected(TokenCodecErrc::InputTooLarge);
    };

    auto countLeadingZeros = [](std::span<std::uint8_t const> const& col) -> std::size_t {
        std::size_t count = 0;
        for (auto const& c : col)
        {
            if (c != 0)
            {
                return count;
            }
            count += 1;
        }
        return count;
    };

    auto const inputZeros = countLeadingZeros(input);
    input = input.subspan(inputZeros);

    // Allocate enough base 2^64 coeff for encoding 38 bytes
    // log(2^(38*8),2^64)) ~= 4.75. So 5 coeff are enough
    std::array<std::uint64_t, 5> base264CoeffBuf{};
    std::span<std::uint64_t> const base264Coeff = [&]() -> std::span<std::uint64_t> {
        // convert input from big endian to native u64, lowest coeff first
        std::size_t numCoeff = 0;
        for (int i = 0; i < base264CoeffBuf.size(); ++i)
        {
            if (i * 8 >= input.size())
            {
                break;
            }
            auto const srcIEnd = input.size() - (i * 8);
            if (srcIEnd >= 8)
            {
                std::memcpy(&base264CoeffBuf[numCoeff], &input[srcIEnd - 8], 8);
                boost::endian::big_to_native_inplace(base264CoeffBuf[numCoeff]);
            }
            else
            {
                std::uint64_t be = 0;
                for (int bi = 0; bi < srcIEnd; ++bi)
                {
                    be <<= 8;
                    be |= input[bi];
                }
                base264CoeffBuf[numCoeff] = be;
            };
            numCoeff += 1;
        }
        return std::span(base264CoeffBuf.data(), numCoeff);
    }();

    // Allocate enough base 58^10 coeff for encoding 38 bytes
    // log(2^(38*8),58^10)) ~= 5.18. So 6 coeff are enough
    std::array<std::uint64_t, 6> base5810Coeff{};
    constexpr std::uint64_t kB5810 = 430804206899405824;  // 58^10;
    std::size_t num5810Coeffs = 0;
    std::size_t cur264End = base264Coeff.size();
    // compute the base 58^10 coeffs
    while (cur264End > 0)
    {
        base5810Coeff[num5810Coeffs] =
            xrpl::b58_fast::detail::inplaceBigintDivRem(base264Coeff.subspan(0, cur264End), kB5810);
        num5810Coeffs += 1;
        if (base264Coeff[cur264End - 1] == 0)
        {
            cur264End -= 1;
        }
    }

    // Translate the result into the alphabet
    // Put all the zeros at the beginning, then all the values from the output
    std::fill(out.begin(), out.begin() + inputZeros, ::xrpl::kALPHABET_FORWARD[0]);

    // iterate through the base 58^10 coeff
    // convert to base 58 big endian then
    // convert to alphabet big endian
    bool skipZeros = true;
    auto outIndex = inputZeros;
    for (int i = num5810Coeffs - 1; i >= 0; --i)
    {
        if (skipZeros && base5810Coeff[i] == 0)
        {
            continue;
        }
        static constexpr std::uint64_t kB5810 = 430804206899405824;  // 58^10;
        if (base5810Coeff[i] >= kB5810)
        {
            return Unexpected(TokenCodecErrc::InputTooLarge);
        }
        std::array<std::uint8_t, 10> const b58Be =
            xrpl::b58_fast::detail::b5810ToB58Be(base5810Coeff[i]);
        std::size_t toSkip = 0;
        std::span<std::uint8_t const> const b58BeS{b58Be.data(), b58Be.size()};
        if (skipZeros)
        {
            toSkip = countLeadingZeros(b58BeS);
            skipZeros = false;
            if (out.size() < ((i + 1) * 10) - toSkip)
            {
                return Unexpected(TokenCodecErrc::OutputTooSmall);
            }
        }
        for (auto b58Coeff : b58BeS.subspan(toSkip))
        {
            out[outIndex] = ::xrpl::kALPHABET_FORWARD[b58Coeff];
            outIndex += 1;
        }
    }

    return out.subspan(0, outIndex);
}

/** Decode a big-endian Base58 string to big-endian binary data (fast path).
 *
 *  Reverses `b256ToB58Be` using the same three-hop strategy in reverse:
 *  `base 58 → base 58^10 → base 2^64 → base 2^8`:
 *
 *  1. The input string is partitioned into chunks of 10 characters
 *     (with a possible shorter partial chunk at the start).  Each chunk is
 *     accumulated into a `uint64_t` base-58^10 coefficient using simple
 *     64-bit arithmetic.
 *  2. The base-58^10 coefficients synthesize a multi-precision value stored
 *     as `uint64_t` limbs via `inplaceBigintMul` (×58^10) and
 *     `inplaceBigintAdd` (+coefficient) for each chunk.
 *  3. The `uint64_t` limbs are written out as big-endian bytes, suppressing
 *     leading zeros from the most-significant limb.
 *
 *  Leading `'r'` characters (alphabet index 0) map back to leading zero bytes.
 *
 *  @note The input string is big-endian (most significant Base58 digit first).
 *  @note Maximum input length is 52 characters (encoding up to 38 bytes);
 *      larger inputs return `TokenCodecErrc::InputTooLarge`.
 *  @note `out` must be at least 8 bytes; returns `TokenCodecErrc::OutputTooSmall`
 *      if the buffer is too small to hold the decoded result.
 *
 *  @param input The Base58-encoded string to decode (big-endian digit order).
 *  @param out   Caller-supplied output buffer for the decoded bytes.
 *  @return On success, a subspan of `out` containing the decoded big-endian bytes.
 *      On failure, an unexpected `TokenCodecErrc` error code (e.g.,
 *      `InvalidEncodingChar` for characters not in the XRPL alphabet).
 */
B58Result<std::span<std::uint8_t>>
b58ToB256Be(std::string_view input, std::span<std::uint8_t> out)
{
    // Convert from b58 to b 58^10

    // Max encoded value is 38 bytes
    // log(2^(38*8),58) ~= 51.9
    if (input.size() > 52)
    {
        return Unexpected(TokenCodecErrc::InputTooLarge);
    };
    if (out.size() < 8)
    {
        return Unexpected(TokenCodecErrc::OutputTooSmall);
    }

    auto countLeadingZeros = [&](auto const& col) -> std::size_t {
        std::size_t count = 0;
        for (auto const& c : col)
        {
            if (c != ::xrpl::kALPHABET_FORWARD[0])
            {
                return count;
            }
            count += 1;
        }
        return count;
    };

    auto const inputZeros = countLeadingZeros(input);

    // Allocate enough base 58^10 coeff for encoding 38 bytes
    // (33 bytes for nodepublic + 1 byte token + 4 bytes checksum)
    // log(2^(38*8),58^10)) ~= 5.18. So 6 coeff are enough
    std::array<std::uint64_t, 6> b5810Coeff{};
    auto [num_full_coeffs, partial_coeff_len] = xrpl::b58_fast::detail::divRem(input.size(), 10);
    auto const numPartialCoeffs = (partial_coeff_len != 0u) ? 1 : 0;
    auto const numB5810Coeffs = num_full_coeffs + numPartialCoeffs;
    XRPL_ASSERT(
        numB5810Coeffs <= b5810Coeff.size(),
        "xrpl::b58_fast::detail::b58_to_b256_be : maximum coeff");
    for (unsigned char const c : input.substr(0, partial_coeff_len))
    {
        auto curVal = ::xrpl::kALPHABET_REVERSE[c];
        if (curVal < 0)
        {
            return Unexpected(TokenCodecErrc::InvalidEncodingChar);
        }
        b5810Coeff[0] *= 58;
        b5810Coeff[0] += curVal;
    }
    for (int i = 0; i < 10; ++i)
    {
        for (int j = 0; j < num_full_coeffs; ++j)
        {
            unsigned char const c = input[partial_coeff_len + (j * 10) + i];
            auto curVal = ::xrpl::kALPHABET_REVERSE[c];
            if (curVal < 0)
            {
                return Unexpected(TokenCodecErrc::InvalidEncodingChar);
            }
            b5810Coeff[numPartialCoeffs + j] *= 58;
            b5810Coeff[numPartialCoeffs + j] += curVal;
        }
    }

    constexpr std::uint64_t kB5810 = 430804206899405824;  // 58^10;

    // log(2^(38*8),2^64) ~= 4.75)
    std::array<std::uint64_t, 5> result{};
    result[0] = b5810Coeff[0];
    std::size_t curResultSize = 1;
    for (int i = 1; i < numB5810Coeffs; ++i)
    {
        std::uint64_t const c = b5810Coeff[i];

        {
            auto code = xrpl::b58_fast::detail::inplaceBigintMul(
                std::span(&result[0], curResultSize + 1), kB5810);
            if (code != TokenCodecErrc::Success)
            {
                return Unexpected(code);
            }
        }
        {
            auto code = xrpl::b58_fast::detail::inplaceBigintAdd(
                std::span(&result[0], curResultSize + 1), c);
            if (code != TokenCodecErrc::Success)
            {
                return Unexpected(code);
            }
        }
        if (result[curResultSize] != 0)
        {
            curResultSize += 1;
        }
    }
    std::fill(out.begin(), out.begin() + inputZeros, 0);
    auto curOutI = inputZeros;
    // Don't write leading zeros to the output for the most significant
    // coeff
    {
        std::uint64_t const c = result[curResultSize - 1];
        auto skipZero = true;
        // start and end of output range
        for (int i = 0; i < 8; ++i)
        {
            std::uint8_t const b = (c >> (8 * (7 - i))) & 0xff;
            if (skipZero)
            {
                if (b == 0)
                {
                    continue;
                }
                skipZero = false;
            }
            out[curOutI] = b;
            curOutI += 1;
        }
    }
    if ((curOutI + (8 * (curResultSize - 1))) > out.size())
    {
        return Unexpected(TokenCodecErrc::OutputTooSmall);
    }

    for (int i = curResultSize - 2; i >= 0; --i)
    {
        auto c = result[i];
        boost::endian::native_to_big_inplace(c);
        memcpy(&out[curOutI], &c, 8);
        curOutI += 8;
    }

    return out.subspan(0, curOutI);
}
}  // namespace detail

/** Encode a raw payload as a Base58Check XRPL token (fast, span-based overload).
 *
 *  Builds the wire layout `[tokenType][input][checksum(4)]` in a 128-byte
 *  stack buffer, then delegates to `detail::b256ToB58Be` to perform the
 *  base conversion.  The output is written directly into the caller-supplied
 *  `out` span, avoiding heap allocation.
 *
 *  @param tokenType The `TokenType` version byte to prepend.
 *  @param input     The raw payload bytes to encode.  Must be non-empty and
 *      at most 123 bytes (128 buffer − 5 bytes for type + checksum).
 *  @param out       Caller-supplied output buffer for the encoded Base58 bytes.
 *  @return On success, a subspan of `out` containing the encoded result.
 *      On failure, an unexpected `TokenCodecErrc` (`InputTooLarge`,
 *      `InputTooSmall`, or `OutputTooSmall`).
 */
B58Result<std::span<std::uint8_t>>
encodeBase58Token(
    TokenType tokenType,
    std::span<std::uint8_t const> input,
    std::span<std::uint8_t> out)
{
    constexpr std::size_t kTMP_BUF_SIZE = 128;
    std::array<std::uint8_t, kTMP_BUF_SIZE> buf{};
    if (input.size() > kTMP_BUF_SIZE - 5)
    {
        return Unexpected(TokenCodecErrc::InputTooLarge);
    }
    if (input.empty())
    {
        return Unexpected(TokenCodecErrc::InputTooSmall);
    }
    buf[0] = static_cast<std::uint8_t>(tokenType);
    memcpy(&buf[1], input.data(), input.size());
    size_t const checksumI = input.size() + 1;
    checksum(buf.data() + checksumI, buf.data(), checksumI);
    std::span<std::uint8_t const> const b58Span(buf.data(), input.size() + 5);
    return detail::b256ToB58Be(b58Span, out);
}

/** Decode a Base58Check XRPL token and return the raw payload (fast, span-based overload).
 *
 *  Converts the Base58 string to binary via `detail::b58ToB256Be`, then
 *  applies three-layer validation:
 *  1. Decoded length ≥ 6 bytes (1 type + at least 1 payload byte + 4 checksum).
 *  2. Leading type byte matches `type`.
 *  3. Trailing 4-byte double-SHA256 checksum is correct.
 *
 *  Only the interior payload (type byte and checksum stripped) is copied into
 *  `outBuf` on success.
 *
 *  @param type   The expected `TokenType` version byte.
 *  @param s      The Base58Check-encoded string to decode (largest digit first).
 *  @param outBuf Caller-supplied buffer for the decoded payload bytes.
 *  @return On success, a subspan of `outBuf` containing the decoded payload.
 *      On failure, an unexpected `TokenCodecErrc` (`InputTooSmall`,
 *      `MismatchedTokenType`, `MismatchedChecksum`, `OutputTooSmall`,
 *      `InvalidEncodingChar`, or `InputTooLarge`).
 */
B58Result<std::span<std::uint8_t>>
decodeBase58Token(TokenType type, std::string_view s, std::span<std::uint8_t> outBuf)
{
    std::array<std::uint8_t, 64> tmpBuf{};
    auto const decodeResult = detail::b58ToB256Be(s, std::span(tmpBuf.data(), tmpBuf.size()));

    if (!decodeResult)
        return decodeResult;

    auto const ret = decodeResult.value();

    // Reject zero length tokens
    if (ret.size() < 6)
        return Unexpected(TokenCodecErrc::InputTooSmall);

    // The type must match.
    if (type != static_cast<TokenType>(static_cast<std::uint8_t>(ret[0])))
        return Unexpected(TokenCodecErrc::MismatchedTokenType);

    // And the checksum must as well.
    std::array<std::uint8_t, 4> guard{};
    checksum(guard.data(), ret.data(), ret.size() - guard.size());
    if (!std::equal(guard.rbegin(), guard.rend(), ret.rbegin()))
    {
        return Unexpected(TokenCodecErrc::MismatchedChecksum);
    }

    std::size_t const outSize = ret.size() - 1 - guard.size();
    if (outBuf.size() < outSize)
        return Unexpected(TokenCodecErrc::OutputTooSmall);
    // Skip the leading type byte and the trailing checksum.
    std::copy(ret.begin() + 1, ret.begin() + outSize + 1, outBuf.begin());
    return outBuf.subspan(0, outSize);
}

/** Encode a raw payload as a Base58Check XRPL token string (fast, legacy string overload).
 *
 *  Bridges the zero-allocation span-based API to the legacy `std::string`
 *  interface expected by callers that pre-date the span overload.  Pre-allocates
 *  128 bytes — well above the theoretical maximum of ≈46 Base58 characters for
 *  a 33-byte public key — to avoid reallocation.
 *
 *  @param type  The `TokenType` version byte to prepend.
 *  @param token Pointer to the raw payload bytes.
 *  @param size  Number of payload bytes.
 *  @return The Base58Check-encoded string, or an empty string on any error.
 */
[[nodiscard]] std::string
encodeBase58Token(TokenType type, void const* token, std::size_t size)
{
    std::string sr;
    sr.resize(128);
    std::span<std::uint8_t> const outSp(reinterpret_cast<std::uint8_t*>(sr.data()), sr.size());
    std::span<std::uint8_t const> const inSp(reinterpret_cast<std::uint8_t const*>(token), size);
    auto r = b58_fast::encodeBase58Token(type, inSp, outSp);
    if (!r)
        return {};
    sr.resize(r.value().size());
    return sr;
}

/** Decode a Base58Check XRPL token string and return the raw payload (fast, legacy string overload).
 *
 *  Bridges the zero-allocation span-based API to the legacy `std::string`
 *  interface.  Pre-allocates 64 bytes — sufficient for any valid XRPL token
 *  payload — to avoid reallocation.
 *
 *  @param s    The Base58Check-encoded string to decode.
 *  @param type The expected `TokenType` version byte.
 *  @return The raw payload with type byte and checksum stripped, or an empty
 *      string on any error (invalid character, type mismatch, checksum failure, etc.).
 */
[[nodiscard]] std::string
decodeBase58Token(std::string const& s, TokenType type)
{
    std::string sr;
    sr.resize(64);
    std::span<std::uint8_t> const outSp(reinterpret_cast<std::uint8_t*>(sr.data()), sr.size());
    auto r = b58_fast::decodeBase58Token(type, s, outSp);
    if (!r)
        return {};
    sr.resize(r.value().size());
    return sr;
}

}  // namespace b58_fast
#endif  // _MSC_VER
}  // namespace xrpl
