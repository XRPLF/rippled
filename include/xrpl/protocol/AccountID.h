#ifndef XRPL_PROTOCOL_ACCOUNTID_H_INCLUDED
#define XRPL_PROTOCOL_ACCOUNTID_H_INCLUDED

#include <xrpl/protocol/tokens.h>
// VFALCO Uncomment when the header issues are resolved
// #include <ripple/protocol/PublicKey.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/json_get_or_throw.h>

#include <cstddef>
#include <optional>
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
    @return std::nullopt if a parse error occurs
*/
template <>
std::optional<AccountID>
parseBase58(std::string const& s);

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

/** Initialize the global cache used to map AccountID to base58 conversions.

    The cache is optional and need not be initialized. But because conversion
    is expensive (it requires a SHA-256 operation) in most cases the overhead
    of the cache is worth the benefit.

    @param count The number of entries the cache should accommodate. Zero will
                 disable the cache, releasing any memory associated with it.

    @note The function will only initialize the cache the first time it is
          invoked. Subsequent invocations do nothing.
 */
void
initAccountIdCache(std::size_t count);

}  // namespace ripple

//------------------------------------------------------------------------------
namespace Json {
template <>
inline ripple::AccountID
getOrThrow(Json::Value const& v, ripple::SField const& field)
{
    using namespace ripple;

    std::string const b58 = getOrThrow<std::string>(v, field);
    if (auto const r = parseBase58<AccountID>(b58))
        return *r;
    Throw<JsonTypeMismatchError>(field.getJsonName(), "AccountID");
}
}  // namespace Json

//------------------------------------------------------------------------------

namespace std {

// DEPRECATED
// VFALCO Use beast::uhash or a hardened container
template <>
struct hash<ripple::AccountID> : ripple::AccountID::hasher
{
    hash() = default;
};

}  // namespace std

#endif
