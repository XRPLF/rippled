#pragma once

#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstddef>

namespace xrpl {

// Checks whether value is within [min, max], inclusive.
template <class T>
inline bool
checkBounds(T const& value, T const& min, T const& max)
{
    return value >= min && value <= max;
}

// Checks whether a container's size is at most max.
template <class T>
inline bool
checkSize(T const& value, std::size_t const max)
{
    return value.size() <= max;
}

// Checks whether a container is non-empty and its size is at most max.
template <class T>
inline bool
checkSizeAndNonEmpty(T const& value, std::size_t const max)
{
    return !value.empty() && value.size() <= max;
}

// Checks whether a hash-like identifier field (e.g. a uint256 object ID) is
// unset/zero.
template <class T>
inline bool
isZeroId(T const& id)
{
    return id == beast::kZero;
}

// Checks whether an amount is not a strictly positive XRP amount.
inline bool
isNonPositiveXRPAmount(STAmount const& amount)
{
    return !isXRP(amount) || amount <= beast::kZero;
}

// Checks whether an amount (of any asset type) is not strictly positive.
inline bool
isNonPositiveAmount(STAmount const& amount)
{
    return amount <= beast::kZero;
}

// Checks whether a currency code is the reserved "bad"/XRP currency code,
// i.e. not a valid IOU currency.
inline bool
isBadCurrency(Currency const& currency)
{
    return badCurrency() == currency;
}

}  // namespace xrpl
