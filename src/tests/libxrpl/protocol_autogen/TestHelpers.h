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

using UInt8Value = std::decay_t<typename SF_UINT8::type::value_type>;
inline UInt8Value
canonical_UINT8()
{
    return UInt8Value{1};
}

using UInt16Value = std::decay_t<typename SF_UINT16::type::value_type>;
inline UInt16Value
canonical_UINT16()
{
    return UInt16Value{1};
}

using UInt32Value = std::decay_t<typename SF_UINT32::type::value_type>;
inline UInt32Value
canonical_UINT32()
{
    return UInt32Value{1};
}

using UInt64Value = std::decay_t<typename SF_UINT64::type::value_type>;
inline UInt64Value
canonical_UINT64()
{
    return UInt64Value{1};
}

using UInt128Value = std::decay_t<typename SF_UINT128::type::value_type>;
inline UInt128Value
canonical_UINT128()
{
    return UInt128Value{1};
}

using UInt160Value = std::decay_t<typename SF_UINT160::type::value_type>;
inline UInt160Value
canonical_UINT160()
{
    return UInt160Value{1};
}

using UInt192Value = std::decay_t<typename SF_UINT192::type::value_type>;
inline UInt192Value
canonical_UINT192()
{
    return UInt192Value{1};
}

using UInt256Value = std::decay_t<typename SF_UINT256::type::value_type>;
inline UInt256Value
canonical_UINT256()
{
    return UInt256Value{1};
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
    result.pushBack(STPath{});
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
