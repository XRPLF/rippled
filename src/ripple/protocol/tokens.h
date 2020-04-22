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

#include <boost/optional.hpp>
#include <cstdint>
#include <string>

namespace ripple {

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
boost::optional<T>
parseBase58(std::string const& s);

template <class T>
boost::optional<T>
parseBase58(TokenType type, std::string const& s);

template <class T>
boost::optional<T>
parseHex(std::string const& s);

template <class T>
boost::optional<T>
parseHexOrBase58(std::string const& s);

// Facilities for converting Ripple tokens
// to and from their human readable strings

/*  Base-58 encode a Ripple Token

    Ripple Tokens have a one-byte prefx indicating
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
base58EncodeToken(TokenType type, void const* token, std::size_t size);

/*  Base-58 encode a Bitcoin Token
 *
 *  provided here for symmetry, but should never be needed
 *  except for testing.
 *
 *  @see base58EncodeToken for format description.
 *
 */
std::string
base58EncodeTokenBitcoin(TokenType type, void const* token, std::size_t size);

/** Decode a Base58 token

    The type and checksum must match or an
    empty string is returned.
*/
std::string
decodeBase58Token(std::string const& s, TokenType type);

/** Decode a Base58 token using Bitcoin alphabet

    The type and checksum must match or an
    empty string is returned.

    This is used to detect user error. Specifically,
    when an AccountID is specified using the wrong
    base58 alphabet, so that a better error message
    may be returned.
*/
std::string
decodeBase58TokenBitcoin(std::string const& s, TokenType type);

}  // namespace ripple

#endif
