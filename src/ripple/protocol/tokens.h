//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_PROTOCOL_TOKENS_H_INCLUDED
#define RIPPLE_PROTOCOL_TOKENS_H_INCLUDED

#include <ripple/basics/Expected.h>
#include <ripple/basics/contract.h>
#include <ripple/protocol/impl/token_errors.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace ripple {

template <class T>
using B58Result = Expected<T, std::error_code>;

enum class TokenType : std::uint8_t {
    None = 1,  // unused
    NodePublic = 28,
    NodePrivate = 32,
    AccountID = 0,
    AccountPublic = 35,
    AccountSecret = 34,
    FamilyGenerator = 41,  // unused
    FamilySeed = 33
};

template <class T>
[[nodiscard]] std::optional<T>
parseBase58(std::string const& s);

template <class T>
[[nodiscard]] std::optional<T>
parseBase58(TokenType type, std::string const& s);

/** Encode data in Base58Check format using XRPL alphabet

    For details on the format see
    https://xrpl.org/base58-encodings.html#base58-encodings

    @param type The type of token to encode.
    @param token Pointer to the data to encode.
    @param size The size of the data to encode.

    @return the encoded token.
*/
[[nodiscard]] std::string
encodeBase58Token(TokenType type, void const* token, std::size_t size);

[[nodiscard]] std::string
decodeBase58Token(std::string const& s, TokenType type);

namespace b58_ref {
// The reference version does not use gcc extensions (int128 in particular)
[[nodiscard]] std::string
encodeBase58Token(TokenType type, void const* token, std::size_t size);

[[nodiscard]] std::string
decodeBase58Token(std::string const& s, TokenType type);

namespace detail {
// Expose detail functions for unit tests only
std::string
encodeBase58(
    void const* message,
    std::size_t size,
    void* temp,
    std::size_t temp_size);

std::string
decodeBase58(std::string const& s);
}  // namespace detail
}  // namespace b58_ref

#ifndef _MSC_VER
namespace b58_fast {
// Use the fast version (10-15x faster) is using gcc extensions (int128 in
// particular)
[[nodiscard]] B58Result<std::span<std::uint8_t>>
encodeBase58Token(
    TokenType token_type,
    std::span<std::uint8_t const> input,
    std::span<std::uint8_t> out);

[[nodiscard]] B58Result<std::span<std::uint8_t>>
decodeBase58Token(
    TokenType type,
    std::string_view s,
    std::span<std::uint8_t> outBuf);

// This interface matches the old interface, but requires additional allocation
[[nodiscard]] std::string
encodeBase58Token(TokenType type, void const* token, std::size_t size);

// This interface matches the old interface, but requires additional allocation
[[nodiscard]] std::string
decodeBase58Token(std::string const& s, TokenType type);

namespace detail {
// Expose detail functions for unit tests only
B58Result<std::span<std::uint8_t>>
b256_to_b58_be(
    std::span<std::uint8_t const> input,
    std::span<std::uint8_t> out);

B58Result<std::span<std::uint8_t>>
b58_to_b256_be(std::string_view input, std::span<std::uint8_t> out);
}  // namespace detail

}  // namespace b58_fast
#endif  // _MSC_VER
}  // namespace ripple

#endif
