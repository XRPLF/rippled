#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>

#include <functional>
#include <ostream>
#include <string>

namespace xrpl {
namespace detail {

class CurrencyTag
{
public:
    explicit CurrencyTag() = default;
};

class DirectoryTag
{
public:
    explicit DirectoryTag() = default;
};

class NodeIDTag
{
public:
    explicit NodeIDTag() = default;
};

}  // namespace detail

/**
 * Directory is an index into the directory of offer books.
 * The last 64 bits of this are the quality.
 */
using Directory = BaseUInt<256, detail::DirectoryTag>;

/**
 * Currency is a hash representing a specific currency.
 */
using Currency = BaseUInt<160, detail::CurrencyTag>;

/**
 * NodeID is a 160-bit hash representing one node.
 */
using NodeID = BaseUInt<160, detail::NodeIDTag>;

/**
 * MPTID is a 192-bit value representing MPT Issuance ID,
 * which is a concatenation of a 32-bit sequence (big endian)
 * and a 160-bit account
 */
using MPTID = BaseUInt<192>;

/**
 * Domain is a 256-bit hash representing a specific domain.
 */
using Domain = BaseUInt<256>;

/**
 * XRP currency.
 */
constexpr inline Currency const&
xrpCurrency() noexcept
{
    static constexpr Currency const kCurrency(beast::kZero);
    return kCurrency;
}

/**
 * A placeholder for empty currencies.
 */
constexpr inline Currency const&
noCurrency() noexcept
{
    static constexpr Currency const kCurrency(xrpCurrency().next());
    return kCurrency;
}

/**
 * We deliberately disallow the currency that looks like "XRP" because too
 * many people were using it instead of the correct XRP currency.
 *
 * Note that this doesn't catch "xRP" or "xrp" or other case variations.
 */
constexpr inline Currency const&
badCurrency() noexcept
{
    static constexpr Currency kCurrency{"0000000000000000000000005852500000000000"};
    return kCurrency;
}

constexpr inline bool
isXRP(Currency const& c) noexcept
{
    return c == xrpCurrency();
}

/**
 * Returns "", "XRP", or three letter ISO code.
 */
std::string
to_string(Currency const& c);

/**
 * Tries to convert a string to a Currency, returns true on success.
 *
 * @note This function will return success if the resulting currency is
 *       badCurrency(). This legacy behavior is unfortunate; changing this
 *       will require very careful checking everywhere and may mean having
 *       to rewrite some unit test code.
 */
bool
toCurrency(Currency&, std::string_view);

/**
 * Tries to convert a string to a Currency, returns noCurrency() on failure.
 *
 * @note This function can return badCurrency(). This legacy behavior is
 *       unfortunate; changing this will require very careful checking
 *       everywhere and may mean having to rewrite some unit test code.
 */
Currency
toCurrency(std::string_view);

inline std::ostream&
operator<<(std::ostream& os, Currency const& x)
{
    os << to_string(x);
    return os;
}

}  // namespace xrpl

namespace std {

template <>
struct hash<xrpl::Currency> : xrpl::Currency::hasher
{
    hash() = default;
};

template <>
struct hash<xrpl::NodeID> : xrpl::NodeID::hasher
{
    hash() = default;
};

template <>
struct hash<xrpl::Directory> : xrpl::Directory::hasher
{
    hash() = default;
};

template <>
struct hash<xrpl::uint256> : xrpl::uint256::hasher
{
    hash() = default;
};

}  // namespace std
