#pragma once

#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstddef>

template <class T>
inline bool
checkBounds(T const& value, T const& min, T const& max)
{
    return value >= min && value <= max;
}

template <class T>
inline bool
checkMax(T const& value, T const& max)
{
    return value <= max;
}

template <class T>
inline bool
checkSize(T const& value, std::size_t const max)
{
    return value.size() <= max;
}

template <class T>
inline bool
checkSizeNonEmpty(T const& value, std::size_t const max)
{
    return value.size() <= max && !value.empty();
}

// Checks whether a hash-like identifier field (e.g. a uint256 object ID) is
// unset/zero.
template <class T>
inline bool
isZeroId(T const& id)
{
    return id == beast::kZero;
}

// Checks whether an amount is a strictly positive XRP amount.
inline bool
checkPositiveXRPAmount(xrpl::STAmount const& amount)
{
    return xrpl::isXRP(amount) && amount > beast::kZero;
}

// Checks whether an amount (of any asset type) is strictly positive.
inline bool
checkPositiveAmount(xrpl::STAmount const& amount)
{
    return amount > beast::kZero;
}

// Checks whether a currency code is the reserved "bad"/XRP currency code,
// i.e. not a valid IOU currency.
inline bool
isBadCurrency(xrpl::Currency const& currency)
{
    return xrpl::badCurrency() == currency;
}
