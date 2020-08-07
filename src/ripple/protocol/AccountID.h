//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2014 Ripple Labs Inc.

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

#ifndef RIPPLE_PROTOCOL_ACCOUNTID_H_INCLUDED
#define RIPPLE_PROTOCOL_ACCOUNTID_H_INCLUDED

#include <ripple/protocol/tokens.h>
// VFALCO Uncomment when the header issues are resolved
//#include <ripple/protocol/PublicKey.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/basics/base_uint.h>
#include <ripple/json/json_value.h>
#include <boost/optional.hpp>
#include <cstddef>
#include <mutex>
#include <string>

namespace ripple {

namespace detail {

class AccountIDTag
{
public:
    explicit AccountIDTag() = default;
};

}  // namespace detail

/** A 160-bit unsigned that uniquely identifies an account. */
using AccountID = base_uint<160, detail::AccountIDTag>;

/** Convert AccountID to base58 checked string */
std::string
toBase58(AccountID const& v);

/** Parse AccountID from checked, base58 string.
    @return boost::none if a parse error occurs
*/
template <>
boost::optional<AccountID>
parseBase58(std::string const& s);

// Compatibility with legacy code
bool
deprecatedParseBase58(AccountID& account, Json::Value const& jv);

/** Parse AccountID from hexadecimal string

    If the string is not exactly 40
    hex digits, boost::none is returned.

    @return boost::none if a parse error occurs
*/
template <>
boost::optional<AccountID>
parseHex(std::string const& s);

/** Parse AccountID from hex or checked base58 string.

    @return boost::none if a parse error occurs
*/
template <>
boost::optional<AccountID>
parseHexOrBase58(std::string const& s);

/** Compute AccountID from public key.

    The account ID is computed as the 160-bit hash of the
    public key data. This excludes the version byte and
    guard bytes included in the base58 representation.

*/
// VFALCO In PublicKey.h for now
// AccountID
// calcAccountID (PublicKey const& pk);

/** A special account that's used as the "issuer" for XRP. */
AccountID const&
xrpAccount();

/** A placeholder for empty accounts. */
AccountID const&
noAccount();

/** Convert hex or base58 string to AccountID.

    @return `true` if the parsing was successful.
*/
// DEPRECATED
bool
to_issuer(AccountID&, std::string const&);

// DEPRECATED Should be checking the currency or native flag
inline bool
isXRP(AccountID const& c)
{
    return c == beast::zero;
}

// DEPRECATED
inline std::string
to_string(AccountID const& account)
{
    return toBase58(account);
}

// DEPRECATED
inline std::ostream&
operator<<(std::ostream& os, AccountID const& x)
{
    os << to_string(x);
    return os;
}

//------------------------------------------------------------------------------

/** Caches the base58 representations of AccountIDs

    This operation occurs with sufficient frequency to
    justify having a cache. In the future, rippled should
    require clients to receive "binary" results, where
    AccountIDs are hex-encoded.
*/
class AccountIDCache
{
private:
    std::mutex mutable mutex_;
    std::size_t capacity_;
    hash_map<AccountID, std::string> mutable m0_;
    hash_map<AccountID, std::string> mutable m1_;

public:
    AccountIDCache(AccountIDCache const&) = delete;
    AccountIDCache&
    operator=(AccountIDCache const&) = delete;

    explicit AccountIDCache(std::size_t capacity);

    /** Return ripple::toBase58 for the AccountID

        Thread Safety:
            Safe to call from any thread concurrently

        @note This function intentionally returns a
              copy for correctness.
    */
    std::string
    toBase58(AccountID const&) const;
};

}  // namespace ripple

//------------------------------------------------------------------------------

namespace std {

// DEPRECATED
// VFALCO Use beast::uhash or a hardened container
template <>
struct hash<ripple::AccountID> : ripple::AccountID::hasher
{
    explicit hash() = default;
};

}  // namespace std

#endif
