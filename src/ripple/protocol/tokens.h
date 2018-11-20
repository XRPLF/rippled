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

#include <ripple/basics/Slice.h>

#include <boost/optional.hpp>
#include <cstdint>
#include <string>

namespace ripple {

enum class TokenType : std::uint8_t
{
    None             = 1,       // Used for ripple lib encoded ed25519 seeds
    NodePublic       = 28,
    NodePrivate      = 32,
    AccountID        = 0,
    AccountPublic    = 35,
    AccountSecret    = 34,
    FamilyGenerator  = 41,      // unused
    FamilySeed       = 33
};

  // The largest base58 encoded token in rippled is 38 bytes.
  // (PublicKey=33 bytes + 1 token + 4 checksum).
constexpr std::size_t MaxDecodedTokenBytes = 38;

template <class T>
boost::optional<T>
parseBase58 (std::string const& s);

template<class T>
boost::optional<T>
parseBase58 (TokenType type, std::string const& s);

template <class T>
boost::optional<T>
parseHex (std::string const& s);

template <class T>
boost::optional<T>
parseHexOrBase58 (std::string const& s);

// Facilities for converting Ripple tokens
// to and from their human readable strings

/*  Base-58 encode a Ripple Token

    Ripple Tokens have a one-byte prefix indicating
    the type of token, followed by the data for the
    token, and finally a 4-byte checksum.

    Tokens include the following:

        Wallet Seed
        Account Public Key
        Account ID

    @param type A single byte representing the TokenType
    @param token A pointer to storage of not
                 less than 2*(size+6) bytes
    @param size the size of the token buffer in bytes
*/
std::string
base58EncodeToken (TokenType type, void const* token, std::size_t size);

/*  Base-58 encode a Bitcoin Token
 *
 *  provided here for symmetry, but should never be needed
 *  except for testing.
 *
 *  @see base58EncodeToken for format description.
 *
 */
std::string
base58EncodeTokenBitcoin (TokenType type, void const* token, std::size_t size);

/** Decode a Base58 token

    The type and checksum must match or `false` is returned. The
    value will be decoded into the slice specified by the `result` parameter. The
    decoded result must fit exactly into the specified buffer or `false` is returned.
    `true` is returned on successful decoding.
*/
bool
decodeBase58Token(
    Slice s,
    TokenType type,
    MutableSlice result);

/** Distinguish between ripple lib encoded seeds and regular encoded seeds.

    Ripple lib encoded seeds start with a three-byte prefix of:
    <TokenType::None><0xE1><0x4B> rather than the usual one-byte prefix of:
    <TokenType::FamilySeed>
*/
enum class ExtraB58Encoding {None, RippleLib};

/** Decode a base58 family seed.

    Return `boost::none` if the encoding could not be interpreted as a family seed.
    return the extra encoding type if the encoding is a family seed. The extra encoding
    type is either `RippleLib` for ripple lib encoded seeds (these are ed25519 seeds with
    a special prefix) to `None` for regular seeds.
*/
boost::optional<ExtraB58Encoding>
decodeBase58FamilySeed(Slice s, MutableSlice result);

/** Decode a Base58 token using Bitcoin alphabet

   The type and checksum must match or `boost::none is returned``. The slice
   must decode into exactly as many bytes as specified as the result slice or
   `false` is returned. The value will be decoded into the slice specified
   by the `result` parameter.

   @note This is used to detect user error. Specifically, when an AccountID is
   specified using the wrong base58 alphabet, so that a better error message may
   be returned.
*/
bool
decodeBase58TokenBitcoin(
    Slice s,
    TokenType type,
    MutableSlice result);

/** Metadata associated with an encoding.

    Tokens are encoded as:
    <1-byte TokenType><Optional 2-byte Encoding type for ripple-lib><Data><4-byte checksum>
    The metadata includes the non-data part of the encoding. If an encoding does not include
    an encoding type, the metadata will use 2-bytes of zeros.
*/
struct DecodeMetadata
{
    std::array<std::uint8_t, 2> encodingType;
    std::uint8_t tokenType;
    std::array<std::uint8_t, 4> checksum;
    bool isRippleLibEncoded() const;
};

/** Low-level decode routine. This can be used to when the token type is unknown. If the
    Token type is known, use either `decodeBase58Token` or `decodeBase58FamilySeed`.

    The checksum must match or `boost::none` is returned. The value will be
    decoded into the slice specified by the `result` parameter. The result may be
    smaller than the specified slice. In that case the returned slice will be a
    proper subset of the result slice. If the result is larger than the space
    allowed by `result` then `boost::none` is returned.
*/
boost::optional<std::pair<Slice, DecodeMetadata>>
decodeBase58Resizable(
    Slice s,
    MutableSlice result);

} // ripple

#endif
