#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STBlob.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/UintTypes.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <vector>

namespace xrpl {

// Typed field canonical values

using Uint8Value = std::decay_t<typename SF_UINT8::type::value_type>;
inline Uint8Value
canonical_UINT8()
{
    return Uint8Value{1};
}

using Uint16Value = std::decay_t<typename SF_UINT16::type::value_type>;
inline Uint16Value
canonical_UINT16()
{
    return Uint16Value{1};
}

using Uint32Value = std::decay_t<typename SF_UINT32::type::value_type>;
inline Uint32Value
canonical_UINT32()
{
    return Uint32Value{1};
}

using Uint64Value = std::decay_t<typename SF_UINT64::type::value_type>;
inline Uint64Value
canonical_UINT64()
{
    return Uint64Value{1};
}

using Uint128Value = std::decay_t<typename SF_UINT128::type::value_type>;
inline Uint128Value
canonical_UINT128()
{
    return Uint128Value{1};
}

using Uint160Value = std::decay_t<typename SF_UINT160::type::value_type>;
inline Uint160Value
canonical_UINT160()
{
    return Uint160Value{1};
}

using Uint192Value = std::decay_t<typename SF_UINT192::type::value_type>;
inline Uint192Value
canonical_UINT192()
{
    return Uint192Value{1};
}

using Uint256Value = std::decay_t<typename SF_UINT256::type::value_type>;
inline Uint256Value
canonical_UINT256()
{
    return Uint256Value{1};
}

using Int32Value = std::decay_t<typename SF_INT32::type::value_type>;
inline Int32Value
canonical_INT32()
{
    return Int32Value{42};
}

using NumberValue = std::decay_t<typename SF_NUMBER::type::value_type>;
inline NumberValue
canonical_NUMBER()
{
    return NumberValue{123};
}

using AmountValue = std::decay_t<typename SF_AMOUNT::type::value_type>;
inline AmountValue
canonical_AMOUNT()
{
    return AmountValue{XRPAmount{1}};
}

using AccountValue = std::decay_t<typename SF_ACCOUNT::type::value_type>;
inline AccountValue
canonical_ACCOUNT()
{
    return xrpAccount();
}

using CurrencyValue = std::decay_t<typename SF_CURRENCY::type::value_type>;
inline CurrencyValue
canonical_CURRENCY()
{
    return xrpCurrency();
}

using IssueValue = std::decay_t<typename SF_ISSUE::type::value_type>;
inline IssueValue
canonical_ISSUE()
{
    return IssueValue{xrpIssue()};
}

using Vector256Value = std::decay_t<typename SF_VECTOR256::type::value_type>;
inline Vector256Value
canonical_VECTOR256()
{
    return Vector256Value{uint256{1}};
}

using BlobValue = std::decay_t<typename SF_VL::type::value_type>;
inline BlobValue
canonical_VL()
{
    static constexpr std::array<std::uint8_t, 3> data{{'a', 'b', 'c'}};
    return BlobValue{data.data(), data.size()};
}

using XChainBridgeValue = std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type>;
inline XChainBridgeValue
canonical_XCHAIN_BRIDGE()
{
    return XChainBridgeValue{xrpAccount(), xrpIssue(), xrpAccount(), xrpIssue()};
}

// Untyped field canonical values

inline STArray
canonical_ARRAY()
{
    return STArray{};
}

inline STObject
canonical_OBJECT()
{
    return STObject{sfGeneric};
}

inline STPathSet
canonical_PATHSET()
{
    STPathSet result{};
    result.push_back(STPath{});
    return result;
}

// Field comparison helpers for generated tests

template <class T>
void
expectEqualField(T const& expected, T const& actual, char const* fieldName)
{
    EXPECT_EQ(expected, actual) << "Field " << fieldName << " mismatch";
}

// Specialization for STObject (no operator==, use isEquivalent)
template <>
inline void
expectEqualField<STObject>(STObject const& expected, STObject const& actual, char const* fieldName)
{
    EXPECT_TRUE(expected.isEquivalent(actual)) << "Field " << fieldName << " mismatch";
}

// Specialization for STPathSet (no operator==, use isEquivalent)
template <>
inline void
expectEqualField<STPathSet>(
    STPathSet const& expected,
    STPathSet const& actual,
    char const* fieldName)
{
    EXPECT_TRUE(expected.isEquivalent(actual)) << "Field " << fieldName << " mismatch";
}

// Overloads for std::reference_wrapper (used by optional ARRAY/PATHSET fields)
template <class T>
void
expectEqualField(T const& expected, std::reference_wrapper<T const> actual, char const* fieldName)
{
    expectEqualField(expected, actual.get(), fieldName);
}

}  // namespace xrpl
