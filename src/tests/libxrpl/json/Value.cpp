#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/json/json_errors.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/json_writer.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <regex>
#include <sstream>
#include <string>

namespace xrpl {

TEST(json_value, limits)
{
    using namespace Json;
    static_assert(Value::minInt == Int(~(UInt(-1) / 2)));
    static_assert(Value::maxInt == Int(UInt(-1) / 2));
    static_assert(Value::maxUInt == UInt(-1));
}

TEST(json_value, construct_and_compare_Json_StaticString)
{
    static constexpr char sample[]{"Contents of a Json::StaticString"};

    static constexpr Json::StaticString test1(sample);
    char const* addrTest1{test1};

    EXPECT_EQ(addrTest1, &sample[0]);
    EXPECT_EQ(test1.c_str(), &sample[0]);

    static constexpr Json::StaticString test2{"Contents of a Json::StaticString"};
    static constexpr Json::StaticString test3{"Another StaticString"};

    EXPECT_EQ(test1, test2);
    EXPECT_NE(test1, test3);

    std::string str{sample};
    EXPECT_EQ(str, test2);
    EXPECT_NE(str, test3);
    EXPECT_EQ(test2, str);
    EXPECT_NE(test3, str);
}

TEST(json_value, different_types)
{
    // Exercise ValueType constructor
    static constexpr Json::StaticString staticStr{"staticStr"};

    auto testCopy = [](Json::ValueType typ) {
        Json::Value val{typ};
        Json::Value cpy{val};
        EXPECT_EQ(val.type(), typ);
        EXPECT_EQ(cpy.type(), typ);
        return val;
    };
    {
        Json::Value const nullV{testCopy(Json::nullValue)};
        EXPECT_TRUE(nullV.isNull());
        EXPECT_FALSE(nullV.isBool());
        EXPECT_FALSE(nullV.isInt());
        EXPECT_FALSE(nullV.isUInt());
        EXPECT_FALSE(nullV.isIntegral());
        EXPECT_FALSE(nullV.isDouble());
        EXPECT_FALSE(nullV.isNumeric());
        EXPECT_FALSE(nullV.isString());
        EXPECT_FALSE(nullV.isArray());
        EXPECT_TRUE(nullV.isArrayOrNull());
        EXPECT_FALSE(nullV.isObject());
        EXPECT_TRUE(nullV.isObjectOrNull());
    }
    {
        Json::Value const intV{testCopy(Json::intValue)};
        EXPECT_FALSE(intV.isNull());
        EXPECT_FALSE(intV.isBool());
        EXPECT_TRUE(intV.isInt());
        EXPECT_FALSE(intV.isUInt());
        EXPECT_TRUE(intV.isIntegral());
        EXPECT_FALSE(intV.isDouble());
        EXPECT_TRUE(intV.isNumeric());
        EXPECT_FALSE(intV.isString());
        EXPECT_FALSE(intV.isArray());
        EXPECT_FALSE(intV.isArrayOrNull());
        EXPECT_FALSE(intV.isObject());
        EXPECT_FALSE(intV.isObjectOrNull());
    }
    {
        Json::Value const uintV{testCopy(Json::uintValue)};
        EXPECT_FALSE(uintV.isNull());
        EXPECT_FALSE(uintV.isBool());
        EXPECT_FALSE(uintV.isInt());
        EXPECT_TRUE(uintV.isUInt());
        EXPECT_TRUE(uintV.isIntegral());
        EXPECT_FALSE(uintV.isDouble());
        EXPECT_TRUE(uintV.isNumeric());
        EXPECT_FALSE(uintV.isString());
        EXPECT_FALSE(uintV.isArray());
        EXPECT_FALSE(uintV.isArrayOrNull());
        EXPECT_FALSE(uintV.isObject());
        EXPECT_FALSE(uintV.isObjectOrNull());
    }
    {
        Json::Value const realV{testCopy(Json::realValue)};
        EXPECT_FALSE(realV.isNull());
        EXPECT_FALSE(realV.isBool());
        EXPECT_FALSE(realV.isInt());
        EXPECT_FALSE(realV.isUInt());
        EXPECT_FALSE(realV.isIntegral());
        EXPECT_TRUE(realV.isDouble());
        EXPECT_TRUE(realV.isNumeric());
        EXPECT_FALSE(realV.isString());
        EXPECT_FALSE(realV.isArray());
        EXPECT_FALSE(realV.isArrayOrNull());
        EXPECT_FALSE(realV.isObject());
        EXPECT_FALSE(realV.isObjectOrNull());
    }
    {
        Json::Value const stringV{testCopy(Json::stringValue)};
        EXPECT_FALSE(stringV.isNull());
        EXPECT_FALSE(stringV.isBool());
        EXPECT_FALSE(stringV.isInt());
        EXPECT_FALSE(stringV.isUInt());
        EXPECT_FALSE(stringV.isIntegral());
        EXPECT_FALSE(stringV.isDouble());
        EXPECT_FALSE(stringV.isNumeric());
        EXPECT_TRUE(stringV.isString());
        EXPECT_FALSE(stringV.isArray());
        EXPECT_FALSE(stringV.isArrayOrNull());
        EXPECT_FALSE(stringV.isObject());
        EXPECT_FALSE(stringV.isObjectOrNull());
    }
    {
        Json::Value const staticStrV{staticStr};
        {
            Json::Value cpy{staticStrV};
            EXPECT_EQ(staticStrV.type(), Json::stringValue);
            EXPECT_EQ(cpy.type(), Json::stringValue);
        }
        EXPECT_FALSE(staticStrV.isNull());
        EXPECT_FALSE(staticStrV.isBool());
        EXPECT_FALSE(staticStrV.isInt());
        EXPECT_FALSE(staticStrV.isUInt());
        EXPECT_FALSE(staticStrV.isIntegral());
        EXPECT_FALSE(staticStrV.isDouble());
        EXPECT_FALSE(staticStrV.isNumeric());
        EXPECT_TRUE(staticStrV.isString());
        EXPECT_FALSE(staticStrV.isArray());
        EXPECT_FALSE(staticStrV.isArrayOrNull());
        EXPECT_FALSE(staticStrV.isObject());
        EXPECT_FALSE(staticStrV.isObjectOrNull());
    }
    {
        Json::Value const boolV{testCopy(Json::booleanValue)};
        EXPECT_FALSE(boolV.isNull());
        EXPECT_TRUE(boolV.isBool());
        EXPECT_FALSE(boolV.isInt());
        EXPECT_FALSE(boolV.isUInt());
        EXPECT_TRUE(boolV.isIntegral());
        EXPECT_FALSE(boolV.isDouble());
        EXPECT_TRUE(boolV.isNumeric());
        EXPECT_FALSE(boolV.isString());
        EXPECT_FALSE(boolV.isArray());
        EXPECT_FALSE(boolV.isArrayOrNull());
        EXPECT_FALSE(boolV.isObject());
        EXPECT_FALSE(boolV.isObjectOrNull());
    }
    {
        Json::Value const arrayV{testCopy(Json::arrayValue)};
        EXPECT_FALSE(arrayV.isNull());
        EXPECT_FALSE(arrayV.isBool());
        EXPECT_FALSE(arrayV.isInt());
        EXPECT_FALSE(arrayV.isUInt());
        EXPECT_FALSE(arrayV.isIntegral());
        EXPECT_FALSE(arrayV.isDouble());
        EXPECT_FALSE(arrayV.isNumeric());
        EXPECT_FALSE(arrayV.isString());
        EXPECT_TRUE(arrayV.isArray());
        EXPECT_TRUE(arrayV.isArrayOrNull());
        EXPECT_FALSE(arrayV.isObject());
        EXPECT_FALSE(arrayV.isObjectOrNull());
    }
    {
        Json::Value const objectV{testCopy(Json::objectValue)};
        EXPECT_FALSE(objectV.isNull());
        EXPECT_FALSE(objectV.isBool());
        EXPECT_FALSE(objectV.isInt());
        EXPECT_FALSE(objectV.isUInt());
        EXPECT_FALSE(objectV.isIntegral());
        EXPECT_FALSE(objectV.isDouble());
        EXPECT_FALSE(objectV.isNumeric());
        EXPECT_FALSE(objectV.isString());
        EXPECT_FALSE(objectV.isArray());
        EXPECT_FALSE(objectV.isArrayOrNull());
        EXPECT_TRUE(objectV.isObject());
        EXPECT_TRUE(objectV.isObjectOrNull());
    }
}

TEST(json_value, compare_strings)
{
    auto doCompare = [&](Json::Value const& lhs,
                         Json::Value const& rhs,
                         bool lhsEqRhs,
                         bool lhsLtRhs,
                         int line) {
        SCOPED_TRACE(line);
        EXPECT_EQ((lhs == rhs), lhsEqRhs);
        EXPECT_NE((lhs != rhs), lhsEqRhs);
        EXPECT_EQ((lhs < rhs), (!(lhsEqRhs || !lhsLtRhs)));
        EXPECT_EQ((lhs <= rhs), (lhsEqRhs || lhsLtRhs));
        EXPECT_EQ((lhs >= rhs), (lhsEqRhs || !lhsLtRhs));
        EXPECT_EQ((lhs > rhs), (!(lhsEqRhs || lhsLtRhs)));
    };

    Json::Value const null0;
    Json::Value const intNeg1{-1};
    Json::Value const int0{Json::intValue};
    Json::Value const intPos1{1};
    Json::Value const uint0{Json::uintValue};
    Json::Value const uint1{1u};
    Json::Value const realNeg1{-1.0};
    Json::Value const real0{Json::realValue};
    Json::Value const realPos1{1.0};
    Json::Value const str0{Json::stringValue};
    Json::Value const str1{"1"};
    Json::Value const boolF{false};
    Json::Value const boolT{true};
    Json::Value const array0{Json::arrayValue};
    Json::Value const array1{[]() {
        Json::Value array1;
        array1[0u] = 1;
        return array1;
    }()};
    Json::Value const obj0{Json::objectValue};
    Json::Value const obj1{[]() {
        Json::Value obj1;
        obj1["one"] = 1;
        return obj1;
    }()};

#pragma push_macro("DO_COMPARE")
    // DO_COMPARE(lhs, rhs, lhsEqualsToRhs lhsLessThanRhs)
#define DO_COMPARE(lhs, rhs, eq, lt) doCompare(lhs, rhs, eq, lt, __LINE__)
    DO_COMPARE(null0, Json::Value{}, true, false);
    DO_COMPARE(null0, intNeg1, false, true);
    DO_COMPARE(null0, int0, false, true);
    DO_COMPARE(null0, intPos1, false, true);
    DO_COMPARE(null0, uint0, false, true);
    DO_COMPARE(null0, uint1, false, true);
    DO_COMPARE(null0, realNeg1, false, true);
    DO_COMPARE(null0, real0, false, true);
    DO_COMPARE(null0, realPos1, false, true);
    DO_COMPARE(null0, str0, false, true);
    DO_COMPARE(null0, str1, false, true);
    DO_COMPARE(null0, boolF, false, true);
    DO_COMPARE(null0, boolT, false, true);
    DO_COMPARE(null0, array0, false, true);
    DO_COMPARE(null0, array1, false, true);
    DO_COMPARE(null0, obj0, false, true);
    DO_COMPARE(null0, obj1, false, true);

    DO_COMPARE(intNeg1, null0, false, false);
    DO_COMPARE(intNeg1, intNeg1, true, false);
    DO_COMPARE(intNeg1, int0, false, true);
    DO_COMPARE(intNeg1, intPos1, false, true);
    DO_COMPARE(intNeg1, uint0, false, true);
    DO_COMPARE(intNeg1, uint1, false, true);
    DO_COMPARE(intNeg1, realNeg1, false, true);
    DO_COMPARE(intNeg1, real0, false, true);
    DO_COMPARE(intNeg1, realPos1, false, true);
    DO_COMPARE(intNeg1, str0, false, true);
    DO_COMPARE(intNeg1, str1, false, true);
    DO_COMPARE(intNeg1, boolF, false, true);
    DO_COMPARE(intNeg1, boolT, false, true);
    DO_COMPARE(intNeg1, array0, false, true);
    DO_COMPARE(intNeg1, array1, false, true);
    DO_COMPARE(intNeg1, obj0, false, true);
    DO_COMPARE(intNeg1, obj1, false, true);

    DO_COMPARE(int0, null0, false, false);
    DO_COMPARE(int0, intNeg1, false, false);
    DO_COMPARE(int0, int0, true, false);
    DO_COMPARE(int0, intPos1, false, true);
    DO_COMPARE(int0, uint0, true, false);
    DO_COMPARE(int0, uint1, false, true);
    DO_COMPARE(int0, realNeg1, false, true);
    DO_COMPARE(int0, real0, false, true);
    DO_COMPARE(int0, realPos1, false, true);
    DO_COMPARE(int0, str0, false, true);
    DO_COMPARE(int0, str1, false, true);
    DO_COMPARE(int0, boolF, false, true);
    DO_COMPARE(int0, boolT, false, true);
    DO_COMPARE(int0, array0, false, true);
    DO_COMPARE(int0, array1, false, true);
    DO_COMPARE(int0, obj0, false, true);
    DO_COMPARE(int0, obj1, false, true);

    DO_COMPARE(intPos1, null0, false, false);
    DO_COMPARE(intPos1, intNeg1, false, false);
    DO_COMPARE(intPos1, int0, false, false);
    DO_COMPARE(intPos1, intPos1, true, false);
    DO_COMPARE(intPos1, uint0, false, false);
    DO_COMPARE(intPos1, uint1, true, false);
    DO_COMPARE(intPos1, realNeg1, false, true);
    DO_COMPARE(intPos1, real0, false, true);
    DO_COMPARE(intPos1, realPos1, false, true);
    DO_COMPARE(intPos1, str0, false, true);
    DO_COMPARE(intPos1, str1, false, true);
    DO_COMPARE(intPos1, boolF, false, true);
    DO_COMPARE(intPos1, boolT, false, true);
    DO_COMPARE(intPos1, array0, false, true);
    DO_COMPARE(intPos1, array1, false, true);
    DO_COMPARE(intPos1, obj0, false, true);
    DO_COMPARE(intPos1, obj1, false, true);

    DO_COMPARE(uint0, null0, false, false);
    DO_COMPARE(uint0, intNeg1, false, false);
    DO_COMPARE(uint0, int0, true, false);
    DO_COMPARE(uint0, intPos1, false, true);
    DO_COMPARE(uint0, uint0, true, false);
    DO_COMPARE(uint0, uint1, false, true);
    DO_COMPARE(uint0, realNeg1, false, true);
    DO_COMPARE(uint0, real0, false, true);
    DO_COMPARE(uint0, realPos1, false, true);
    DO_COMPARE(uint0, str0, false, true);
    DO_COMPARE(uint0, str1, false, true);
    DO_COMPARE(uint0, boolF, false, true);
    DO_COMPARE(uint0, boolT, false, true);
    DO_COMPARE(uint0, array0, false, true);
    DO_COMPARE(uint0, array1, false, true);
    DO_COMPARE(uint0, obj0, false, true);
    DO_COMPARE(uint0, obj1, false, true);

    DO_COMPARE(uint1, null0, false, false);
    DO_COMPARE(uint1, intNeg1, false, false);
    DO_COMPARE(uint1, int0, false, false);
    DO_COMPARE(uint1, intPos1, true, false);
    DO_COMPARE(uint1, uint0, false, false);
    DO_COMPARE(uint1, uint1, true, false);
    DO_COMPARE(uint1, realNeg1, false, true);
    DO_COMPARE(uint1, real0, false, true);
    DO_COMPARE(uint1, realPos1, false, true);
    DO_COMPARE(uint1, str0, false, true);
    DO_COMPARE(uint1, str1, false, true);
    DO_COMPARE(uint1, boolF, false, true);
    DO_COMPARE(uint1, boolT, false, true);
    DO_COMPARE(uint1, array0, false, true);
    DO_COMPARE(uint1, array1, false, true);
    DO_COMPARE(uint1, obj0, false, true);
    DO_COMPARE(uint1, obj1, false, true);

    DO_COMPARE(realNeg1, null0, false, false);
    DO_COMPARE(realNeg1, intNeg1, false, false);
    DO_COMPARE(realNeg1, int0, false, false);
    DO_COMPARE(realNeg1, intPos1, false, false);
    DO_COMPARE(realNeg1, uint0, false, false);
    DO_COMPARE(realNeg1, uint1, false, false);
    DO_COMPARE(realNeg1, realNeg1, true, false);
    DO_COMPARE(realNeg1, real0, false, true);
    DO_COMPARE(realNeg1, realPos1, false, true);
    DO_COMPARE(realNeg1, str0, false, true);
    DO_COMPARE(realNeg1, str1, false, true);
    DO_COMPARE(realNeg1, boolF, false, true);
    DO_COMPARE(realNeg1, boolT, false, true);
    DO_COMPARE(realNeg1, array0, false, true);
    DO_COMPARE(realNeg1, array1, false, true);
    DO_COMPARE(realNeg1, obj0, false, true);
    DO_COMPARE(realNeg1, obj1, false, true);

    DO_COMPARE(real0, null0, false, false);
    DO_COMPARE(real0, intNeg1, false, false);
    DO_COMPARE(real0, int0, false, false);
    DO_COMPARE(real0, intPos1, false, false);
    DO_COMPARE(real0, uint0, false, false);
    DO_COMPARE(real0, uint1, false, false);
    DO_COMPARE(real0, realNeg1, false, false);
    DO_COMPARE(real0, real0, true, false);
    DO_COMPARE(real0, realPos1, false, true);
    DO_COMPARE(real0, str0, false, true);
    DO_COMPARE(real0, str1, false, true);
    DO_COMPARE(real0, boolF, false, true);
    DO_COMPARE(real0, boolT, false, true);
    DO_COMPARE(real0, array0, false, true);
    DO_COMPARE(real0, array1, false, true);
    DO_COMPARE(real0, obj0, false, true);
    DO_COMPARE(real0, obj1, false, true);

    DO_COMPARE(realPos1, null0, false, false);
    DO_COMPARE(realPos1, intNeg1, false, false);
    DO_COMPARE(realPos1, int0, false, false);
    DO_COMPARE(realPos1, intPos1, false, false);
    DO_COMPARE(realPos1, uint0, false, false);
    DO_COMPARE(realPos1, uint1, false, false);
    DO_COMPARE(realPos1, realNeg1, false, false);
    DO_COMPARE(realPos1, real0, false, false);
    DO_COMPARE(realPos1, realPos1, true, false);
    DO_COMPARE(realPos1, str0, false, true);
    DO_COMPARE(realPos1, str1, false, true);
    DO_COMPARE(realPos1, boolF, false, true);
    DO_COMPARE(realPos1, boolT, false, true);
    DO_COMPARE(realPos1, array0, false, true);
    DO_COMPARE(realPos1, array1, false, true);
    DO_COMPARE(realPos1, obj0, false, true);
    DO_COMPARE(realPos1, obj1, false, true);

    DO_COMPARE(str0, null0, false, false);
    DO_COMPARE(str0, intNeg1, false, false);
    DO_COMPARE(str0, int0, false, false);
    DO_COMPARE(str0, intPos1, false, false);
    DO_COMPARE(str0, uint0, false, false);
    DO_COMPARE(str0, uint1, false, false);
    DO_COMPARE(str0, realNeg1, false, false);
    DO_COMPARE(str0, real0, false, false);
    DO_COMPARE(str0, realPos1, false, false);
    DO_COMPARE(str0, str0, true, false);
    DO_COMPARE(str0, str1, false, true);
    DO_COMPARE(str0, boolF, false, true);
    DO_COMPARE(str0, boolT, false, true);
    DO_COMPARE(str0, array0, false, true);
    DO_COMPARE(str0, array1, false, true);
    DO_COMPARE(str0, obj0, false, true);
    DO_COMPARE(str0, obj1, false, true);

    DO_COMPARE(str1, null0, false, false);
    DO_COMPARE(str1, intNeg1, false, false);
    DO_COMPARE(str1, int0, false, false);
    DO_COMPARE(str1, intPos1, false, false);
    DO_COMPARE(str1, uint0, false, false);
    DO_COMPARE(str1, uint1, false, false);
    DO_COMPARE(str1, realNeg1, false, false);
    DO_COMPARE(str1, real0, false, false);
    DO_COMPARE(str1, realPos1, false, false);
    DO_COMPARE(str1, str0, false, false);
    DO_COMPARE(str1, str1, true, false);
    DO_COMPARE(str1, boolF, false, true);
    DO_COMPARE(str1, boolT, false, true);
    DO_COMPARE(str1, array0, false, true);
    DO_COMPARE(str1, array1, false, true);
    DO_COMPARE(str1, obj0, false, true);
    DO_COMPARE(str1, obj1, false, true);

    DO_COMPARE(boolF, null0, false, false);
    DO_COMPARE(boolF, intNeg1, false, false);
    DO_COMPARE(boolF, int0, false, false);
    DO_COMPARE(boolF, intPos1, false, false);
    DO_COMPARE(boolF, uint0, false, false);
    DO_COMPARE(boolF, uint1, false, false);
    DO_COMPARE(boolF, realNeg1, false, false);
    DO_COMPARE(boolF, real0, false, false);
    DO_COMPARE(boolF, realPos1, false, false);
    DO_COMPARE(boolF, str0, false, false);
    DO_COMPARE(boolF, str1, false, false);
    DO_COMPARE(boolF, boolF, true, false);
    DO_COMPARE(boolF, boolT, false, true);
    DO_COMPARE(boolF, array0, false, true);
    DO_COMPARE(boolF, array1, false, true);
    DO_COMPARE(boolF, obj0, false, true);
    DO_COMPARE(boolF, obj1, false, true);

    DO_COMPARE(boolT, null0, false, false);
    DO_COMPARE(boolT, intNeg1, false, false);
    DO_COMPARE(boolT, int0, false, false);
    DO_COMPARE(boolT, intPos1, false, false);
    DO_COMPARE(boolT, uint0, false, false);
    DO_COMPARE(boolT, uint1, false, false);
    DO_COMPARE(boolT, realNeg1, false, false);
    DO_COMPARE(boolT, real0, false, false);
    DO_COMPARE(boolT, realPos1, false, false);
    DO_COMPARE(boolT, str0, false, false);
    DO_COMPARE(boolT, str1, false, false);
    DO_COMPARE(boolT, boolF, false, false);
    DO_COMPARE(boolT, boolT, true, false);
    DO_COMPARE(boolT, array0, false, true);
    DO_COMPARE(boolT, array1, false, true);
    DO_COMPARE(boolT, obj0, false, true);
    DO_COMPARE(boolT, obj1, false, true);

    DO_COMPARE(array0, null0, false, false);
    DO_COMPARE(array0, intNeg1, false, false);
    DO_COMPARE(array0, int0, false, false);
    DO_COMPARE(array0, intPos1, false, false);
    DO_COMPARE(array0, uint0, false, false);
    DO_COMPARE(array0, uint1, false, false);
    DO_COMPARE(array0, realNeg1, false, false);
    DO_COMPARE(array0, real0, false, false);
    DO_COMPARE(array0, realPos1, false, false);
    DO_COMPARE(array0, str0, false, false);
    DO_COMPARE(array0, str1, false, false);
    DO_COMPARE(array0, boolF, false, false);
    DO_COMPARE(array0, boolT, false, false);
    DO_COMPARE(array0, array0, true, false);
    DO_COMPARE(array0, array1, false, true);
    DO_COMPARE(array0, obj0, false, true);
    DO_COMPARE(array0, obj1, false, true);

    DO_COMPARE(array1, null0, false, false);
    DO_COMPARE(array1, intNeg1, false, false);
    DO_COMPARE(array1, int0, false, false);
    DO_COMPARE(array1, intPos1, false, false);
    DO_COMPARE(array1, uint0, false, false);
    DO_COMPARE(array1, uint1, false, false);
    DO_COMPARE(array1, realNeg1, false, false);
    DO_COMPARE(array1, real0, false, false);
    DO_COMPARE(array1, realPos1, false, false);
    DO_COMPARE(array1, str0, false, false);
    DO_COMPARE(array1, str1, false, false);
    DO_COMPARE(array1, boolF, false, false);
    DO_COMPARE(array1, boolT, false, false);
    DO_COMPARE(array1, array0, false, false);
    DO_COMPARE(array1, array1, true, false);
    DO_COMPARE(array1, obj0, false, true);
    DO_COMPARE(array1, obj1, false, true);

    DO_COMPARE(obj0, null0, false, false);
    DO_COMPARE(obj0, intNeg1, false, false);
    DO_COMPARE(obj0, int0, false, false);
    DO_COMPARE(obj0, intPos1, false, false);
    DO_COMPARE(obj0, uint0, false, false);
    DO_COMPARE(obj0, uint1, false, false);
    DO_COMPARE(obj0, realNeg1, false, false);
    DO_COMPARE(obj0, real0, false, false);
    DO_COMPARE(obj0, realPos1, false, false);
    DO_COMPARE(obj0, str0, false, false);
    DO_COMPARE(obj0, str1, false, false);
    DO_COMPARE(obj0, boolF, false, false);
    DO_COMPARE(obj0, boolT, false, false);
    DO_COMPARE(obj0, array0, false, false);
    DO_COMPARE(obj0, array1, false, false);
    DO_COMPARE(obj0, obj0, true, false);
    DO_COMPARE(obj0, obj1, false, true);

    DO_COMPARE(obj1, null0, false, false);
    DO_COMPARE(obj1, intNeg1, false, false);
    DO_COMPARE(obj1, int0, false, false);
    DO_COMPARE(obj1, intPos1, false, false);
    DO_COMPARE(obj1, uint0, false, false);
    DO_COMPARE(obj1, uint1, false, false);
    DO_COMPARE(obj1, realNeg1, false, false);
    DO_COMPARE(obj1, real0, false, false);
    DO_COMPARE(obj1, realPos1, false, false);
    DO_COMPARE(obj1, str0, false, false);
    DO_COMPARE(obj1, str1, false, false);
    DO_COMPARE(obj1, boolF, false, false);
    DO_COMPARE(obj1, boolT, false, false);
    DO_COMPARE(obj1, array0, false, false);
    DO_COMPARE(obj1, array1, false, false);
    DO_COMPARE(obj1, obj0, false, false);
    DO_COMPARE(obj1, obj1, true, false);
#undef DO_COMPARE
#pragma pop_macro("DO_COMPARE")
}

TEST(json_value, bool)
{
    EXPECT_FALSE(Json::Value());

    EXPECT_FALSE(Json::Value(""));

    EXPECT_TRUE(bool(Json::Value("empty")));
    EXPECT_TRUE(bool(Json::Value(false)));
    EXPECT_TRUE(bool(Json::Value(true)));
    EXPECT_TRUE(bool(Json::Value(0)));
    EXPECT_TRUE(bool(Json::Value(1)));

    Json::Value array(Json::arrayValue);
    EXPECT_FALSE(array);
    array.append(0);
    EXPECT_TRUE(bool(array));

    Json::Value object(Json::objectValue);
    EXPECT_FALSE(object);
    object[""] = false;
    EXPECT_TRUE(bool(object));
}

TEST(json_value, bad_json)
{
    char const* s(R"({"method":"ledger","params":[{"ledger_index":1e300}]})");

    Json::Value j;
    Json::Reader r;

    EXPECT_TRUE(r.parse(s, j));
}

TEST(json_value, edge_cases)
{
    std::uint32_t max_uint = std::numeric_limits<std::uint32_t>::max();
    std::int32_t max_int = std::numeric_limits<std::int32_t>::max();
    std::int32_t min_int = std::numeric_limits<std::int32_t>::min();

    std::uint32_t a_uint = max_uint - 1978;
    std::int32_t a_large_int = max_int - 1978;
    std::int32_t a_small_int = min_int + 1978;

    {
        std::string json = "{\"max_uint\":" + std::to_string(max_uint);
        json += ",\"max_int\":" + std::to_string(max_int);
        json += ",\"min_int\":" + std::to_string(min_int);
        json += ",\"a_uint\":" + std::to_string(a_uint);
        json += ",\"a_large_int\":" + std::to_string(a_large_int);
        json += ",\"a_small_int\":" + std::to_string(a_small_int);
        json += "}";

        Json::Value j1;
        Json::Reader r1;

        EXPECT_TRUE(r1.parse(json, j1));
        EXPECT_EQ(j1["max_uint"].asUInt(), max_uint);
        EXPECT_EQ(j1["max_uint"].asAbsUInt(), max_uint);
        EXPECT_EQ(j1["max_int"].asInt(), max_int);
        EXPECT_EQ(j1["max_int"].asAbsUInt(), max_int);
        EXPECT_EQ(j1["min_int"].asInt(), min_int);
        EXPECT_EQ(j1["min_int"].asAbsUInt(), static_cast<std::int64_t>(min_int) * -1);
        EXPECT_EQ(j1["a_uint"].asUInt(), a_uint);
        EXPECT_EQ(j1["a_uint"].asAbsUInt(), a_uint);
        EXPECT_GT(j1["a_uint"], a_large_int);
        EXPECT_GT(j1["a_uint"], a_small_int);
        EXPECT_EQ(j1["a_large_int"].asInt(), a_large_int);
        EXPECT_EQ(j1["a_large_int"].asAbsUInt(), a_large_int);
        EXPECT_EQ(j1["a_large_int"].asUInt(), a_large_int);
        EXPECT_LT(j1["a_large_int"], a_uint);
        EXPECT_EQ(j1["a_small_int"].asInt(), a_small_int);
        EXPECT_EQ(j1["a_small_int"].asAbsUInt(), static_cast<std::int64_t>(a_small_int) * -1);
        EXPECT_LT(j1["a_small_int"], a_uint);
    }

    std::uint64_t overflow = std::uint64_t(max_uint) + 1;
    {
        std::string json = "{\"overflow\":";
        json += std::to_string(overflow);
        json += "}";

        Json::Value j2;
        Json::Reader r2;

        EXPECT_FALSE(r2.parse(json, j2));
    }

    std::int64_t underflow = std::int64_t(min_int) - 1;
    {
        std::string json = "{\"underflow\":";
        json += std::to_string(underflow);
        json += "}";

        Json::Value j3;
        Json::Reader r3;

        EXPECT_FALSE(r3.parse(json, j3));
    }

    {
        Json::Value intString{std::to_string(overflow)};
        EXPECT_THROW(intString.asUInt(), beast::BadLexicalCast);
        EXPECT_THROW(intString.asAbsUInt(), Json::error);

        intString = "4294967295";
        EXPECT_EQ(intString.asUInt(), 4294967295u);
        EXPECT_EQ(intString.asAbsUInt(), 4294967295u);

        intString = "0";
        EXPECT_EQ(intString.asUInt(), 0);
        EXPECT_EQ(intString.asAbsUInt(), 0);

        intString = "-1";
        EXPECT_THROW(intString.asUInt(), beast::BadLexicalCast);
        EXPECT_EQ(intString.asAbsUInt(), 1);

        intString = "-4294967295";
        EXPECT_EQ(intString.asAbsUInt(), 4294967295);

        intString = "-4294967296";
        EXPECT_THROW(intString.asAbsUInt(), Json::error);

        intString = "2147483648";
        EXPECT_THROW(intString.asInt(), beast::BadLexicalCast);
        EXPECT_EQ(intString.asAbsUInt(), 2147483648);

        intString = "2147483647";
        EXPECT_EQ(intString.asInt(), 2147483647);
        EXPECT_EQ(intString.asAbsUInt(), 2147483647);

        intString = "-2147483648";
        EXPECT_EQ(intString.asInt(), -2147483648LL);  // MSVC wants the LL
        EXPECT_EQ(intString.asAbsUInt(), 2147483648LL);

        intString = "-2147483649";
        EXPECT_THROW(intString.asInt(), beast::BadLexicalCast);
        EXPECT_EQ(intString.asAbsUInt(), 2147483649);
    }

    {
        Json::Value intReal{4294967297.0};
        EXPECT_THROW(intReal.asUInt(), Json::error);
        EXPECT_THROW(intReal.asAbsUInt(), Json::error);

        intReal = 4294967295.0;
        EXPECT_EQ(intReal.asUInt(), 4294967295u);
        EXPECT_EQ(intReal.asAbsUInt(), 4294967295u);

        intReal = 0.0;
        EXPECT_EQ(intReal.asUInt(), 0);
        EXPECT_EQ(intReal.asAbsUInt(), 0);

        intReal = -1.0;
        EXPECT_THROW(intReal.asUInt(), Json::error);
        EXPECT_EQ(intReal.asAbsUInt(), 1);

        intReal = -4294967295.0;
        EXPECT_EQ(intReal.asAbsUInt(), 4294967295);

        intReal = -4294967296.0;
        EXPECT_THROW(intReal.asAbsUInt(), Json::error);

        intReal = 2147483648.0;
        EXPECT_THROW(intReal.asInt(), Json::error);
        EXPECT_EQ(intReal.asAbsUInt(), 2147483648);

        intReal = 2147483647.0;
        EXPECT_EQ(intReal.asInt(), 2147483647);
        EXPECT_EQ(intReal.asAbsUInt(), 2147483647);

        intReal = -2147483648.0;
        EXPECT_EQ(intReal.asInt(), -2147483648LL);  // MSVC wants the LL
        EXPECT_EQ(intReal.asAbsUInt(), 2147483648LL);

        intReal = -2147483649.0;
        EXPECT_THROW(intReal.asInt(), Json::error);
        EXPECT_EQ(intReal.asAbsUInt(), 2147483649);
    }
}

TEST(json_value, copy)
{
    Json::Value v1{2.5};
    EXPECT_TRUE(v1.isDouble());
    EXPECT_EQ(v1.asDouble(), 2.5);

    Json::Value v2 = v1;
    EXPECT_TRUE(v1.isDouble());
    EXPECT_EQ(v1.asDouble(), 2.5);
    EXPECT_TRUE(v2.isDouble());
    EXPECT_EQ(v2.asDouble(), 2.5);
    EXPECT_EQ(v1, v2);

    v1 = v2;
    EXPECT_TRUE(v1.isDouble());
    EXPECT_EQ(v1.asDouble(), 2.5);
    EXPECT_TRUE(v2.isDouble());
    EXPECT_EQ(v2.asDouble(), 2.5);
    EXPECT_EQ(v1, v2);
}

TEST(json_value, move)
{
    Json::Value v1{2.5};
    EXPECT_TRUE(v1.isDouble());
    EXPECT_EQ(v1.asDouble(), 2.5);

    Json::Value v2 = std::move(v1);
    EXPECT_FALSE(v1);
    EXPECT_TRUE(v2.isDouble());
    EXPECT_EQ(v2.asDouble(), 2.5);
    EXPECT_NE(v1, v2);

    v1 = std::move(v2);
    EXPECT_TRUE(v1.isDouble());
    EXPECT_EQ(v1.asDouble(), 2.5);
    EXPECT_FALSE(v2);
    EXPECT_NE(v1, v2);
}

TEST(json_value, comparisons)
{
    Json::Value a, b;
    auto testEquals = [&](std::string const& name) {
        EXPECT_TRUE(a == b);
        EXPECT_TRUE(a <= b);
        EXPECT_TRUE(a >= b);

        EXPECT_FALSE(a != b);
        EXPECT_FALSE(a < b);
        EXPECT_FALSE(a > b);

        EXPECT_TRUE(b == a);
        EXPECT_TRUE(b <= a);
        EXPECT_TRUE(b >= a);

        EXPECT_FALSE(b != a);
        EXPECT_FALSE(b < a);
        EXPECT_FALSE(b > a);
    };

    auto testGreaterThan = [&](std::string const& name) {
        EXPECT_FALSE(a == b);
        EXPECT_FALSE(a <= b);
        EXPECT_TRUE(a >= b);

        EXPECT_TRUE(a != b);
        EXPECT_FALSE(a < b);
        EXPECT_TRUE(a > b);

        EXPECT_FALSE(b == a);
        EXPECT_TRUE(b <= a);
        EXPECT_FALSE(b >= a);

        EXPECT_TRUE(b != a);
        EXPECT_TRUE(b < a);
        EXPECT_FALSE(b > a);
    };

    a["a"] = Json::UInt(0);
    b["a"] = Json::Int(0);
    testEquals("zero");

    b["a"] = Json::Int(-1);
    testGreaterThan("negative");

    Json::Int big = std::numeric_limits<int>::max();
    Json::UInt bigger = big;
    bigger++;

    a["a"] = bigger;
    b["a"] = big;
    testGreaterThan("big");
}

TEST(json_value, compact)
{
    Json::Value j;
    Json::Reader r;
    char const* s("{\"array\":[{\"12\":23},{},null,false,0.5]}");

    auto countLines = [](std::string const& str) {
        return 1 + std::count_if(str.begin(), str.end(), [](char c) { return c == '\n'; });
    };

    EXPECT_TRUE(r.parse(s, j));
    {
        std::stringstream ss;
        ss << j;
        EXPECT_GT(countLines(ss.str()), 1);
    }
    {
        std::stringstream ss;
        ss << Json::Compact(std::move(j));
        EXPECT_EQ(countLines(ss.str()), 1);
    }
}

TEST(json_value, conversions)
{
    // We have Json::Int, but not Json::Double or Json::Real.
    // We have Json::Int, Json::Value::Int, and Json::ValueType::intValue.
    // We have Json::ValueType::realValue but Json::Value::asDouble.
    // TODO: What's the thinking here?
    {
        // null
        Json::Value val;
        EXPECT_TRUE(val.isNull());
        // val.asCString() should trigger an assertion failure
        EXPECT_EQ(val.asString(), "");
        EXPECT_EQ(val.asInt(), 0);
        EXPECT_EQ(val.asUInt(), 0);
        EXPECT_EQ(val.asAbsUInt(), 0);
        EXPECT_EQ(val.asDouble(), 0.0);
        EXPECT_FALSE(val.asBool());

        EXPECT_TRUE(val.isConvertibleTo(Json::nullValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::intValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::uintValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::realValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::stringValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::booleanValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::arrayValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::objectValue));
    }
    {
        // int
        Json::Value val = -1234;
        EXPECT_TRUE(val.isInt());
        // val.asCString() should trigger an assertion failure
        EXPECT_EQ(val.asString(), "-1234");
        EXPECT_EQ(val.asInt(), -1234);
        EXPECT_THROW(val.asUInt(), Json::error);
        EXPECT_EQ(val.asAbsUInt(), 1234u);
        EXPECT_EQ(val.asDouble(), -1234.0);
        EXPECT_TRUE(val.asBool());

        EXPECT_FALSE(val.isConvertibleTo(Json::nullValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::intValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::uintValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::realValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::stringValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::booleanValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::arrayValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::objectValue));
    }
    {
        // uint
        Json::Value val = 1234U;
        EXPECT_TRUE(val.isUInt());
        // val.asCString() should trigger an assertion failure
        EXPECT_EQ(val.asString(), "1234");
        EXPECT_EQ(val.asInt(), 1234);
        EXPECT_EQ(val.asUInt(), 1234u);
        EXPECT_EQ(val.asAbsUInt(), 1234u);
        EXPECT_EQ(val.asDouble(), 1234.0);
        EXPECT_TRUE(val.asBool());

        EXPECT_FALSE(val.isConvertibleTo(Json::nullValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::intValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::uintValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::realValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::stringValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::booleanValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::arrayValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::objectValue));
    }
    {
        // real
        Json::Value val = 2.0;
        EXPECT_TRUE(val.isDouble());
        // val.asCString() should trigger an assertion failure
        EXPECT_TRUE(std::regex_match(val.asString(), std::regex("^2\\.0*$")));
        EXPECT_EQ(val.asInt(), 2);
        EXPECT_EQ(val.asUInt(), 2u);
        EXPECT_EQ(val.asAbsUInt(), 2u);
        EXPECT_EQ(val.asDouble(), 2.0);
        EXPECT_TRUE(val.asBool());

        EXPECT_FALSE(val.isConvertibleTo(Json::nullValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::intValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::uintValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::realValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::stringValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::booleanValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::arrayValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::objectValue));
    }
    {
        // numeric string
        Json::Value val = "54321";
        EXPECT_TRUE(val.isString());
        EXPECT_EQ(strcmp(val.asCString(), "54321"), 0);
        EXPECT_EQ(val.asString(), "54321");
        EXPECT_EQ(val.asInt(), 54321);
        EXPECT_EQ(val.asUInt(), 54321u);
        EXPECT_EQ(val.asAbsUInt(), 54321);
        EXPECT_THROW(val.asDouble(), Json::error);
        EXPECT_TRUE(val.asBool());

        EXPECT_FALSE(val.isConvertibleTo(Json::nullValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::intValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::uintValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::realValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::stringValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::booleanValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::arrayValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::objectValue));
    }
    {
        // non-numeric string
        Json::Value val(Json::stringValue);
        EXPECT_TRUE(val.isString());
        EXPECT_EQ(val.asCString(), nullptr);
        EXPECT_EQ(val.asString(), "");
        EXPECT_THROW(val.asInt(), std::exception);
        EXPECT_THROW(val.asUInt(), std::exception);
        EXPECT_THROW(val.asAbsUInt(), std::exception);
        EXPECT_THROW(val.asDouble(), std::exception);
        EXPECT_TRUE(val.asBool() == false);

        EXPECT_TRUE(val.isConvertibleTo(Json::nullValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::intValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::uintValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::realValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::stringValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::booleanValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::arrayValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::objectValue));
    }
    {
        // bool false
        Json::Value val = false;
        EXPECT_TRUE(val.isBool());
        // val.asCString() should trigger an assertion failure
        EXPECT_EQ(val.asString(), "false");
        EXPECT_EQ(val.asInt(), 0);
        EXPECT_EQ(val.asUInt(), 0);
        EXPECT_EQ(val.asAbsUInt(), 0);
        EXPECT_EQ(val.asDouble(), 0.0);
        EXPECT_FALSE(val.asBool());

        EXPECT_TRUE(val.isConvertibleTo(Json::nullValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::intValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::uintValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::realValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::stringValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::booleanValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::arrayValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::objectValue));
    }
    {
        // bool true
        Json::Value val = true;
        EXPECT_TRUE(val.isBool());
        // val.asCString() should trigger an assertion failure
        EXPECT_EQ(val.asString(), "true");
        EXPECT_EQ(val.asInt(), 1);
        EXPECT_EQ(val.asUInt(), 1);
        EXPECT_EQ(val.asAbsUInt(), 1);
        EXPECT_EQ(val.asDouble(), 1.0);
        EXPECT_TRUE(val.asBool());

        EXPECT_FALSE(val.isConvertibleTo(Json::nullValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::intValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::uintValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::realValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::stringValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::booleanValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::arrayValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::objectValue));
    }
    {
        // array type
        Json::Value val(Json::arrayValue);
        EXPECT_TRUE(val.isArray());
        // val.asCString should trigger an assertion failure
        EXPECT_THROW(val.asString(), Json::error);
        EXPECT_THROW(val.asInt(), Json::error);
        EXPECT_THROW(val.asUInt(), Json::error);
        EXPECT_THROW(val.asAbsUInt(), Json::error);
        EXPECT_THROW(val.asDouble(), Json::error);
        EXPECT_FALSE(val.asBool());  // empty or not

        EXPECT_TRUE(val.isConvertibleTo(Json::nullValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::intValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::uintValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::realValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::stringValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::booleanValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::arrayValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::objectValue));
    }
    {
        // object type
        Json::Value val(Json::objectValue);
        EXPECT_TRUE(val.isObject());
        // val.asCString should trigger an assertion failure
        EXPECT_THROW(val.asString(), Json::error);
        EXPECT_THROW(val.asInt(), Json::error);
        EXPECT_THROW(val.asUInt(), Json::error);
        EXPECT_THROW(val.asAbsUInt(), Json::error);
        EXPECT_THROW(val.asDouble(), Json::error);
        EXPECT_FALSE(val.asBool());  // empty or not

        EXPECT_TRUE(val.isConvertibleTo(Json::nullValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::intValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::uintValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::realValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::stringValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::booleanValue));
        EXPECT_FALSE(val.isConvertibleTo(Json::arrayValue));
        EXPECT_TRUE(val.isConvertibleTo(Json::objectValue));
    }
}

TEST(json_value, access_members)
{
    Json::Value val;
    EXPECT_EQ(val.type(), Json::nullValue);
    EXPECT_EQ(val.size(), 0);
    EXPECT_FALSE(val.isValidIndex(0));
    EXPECT_FALSE(val.isMember("key"));
    {
        Json::Value const constVal = val;
        EXPECT_EQ(constVal[7u].type(), Json::nullValue);
        EXPECT_FALSE(constVal.isMember("key"));
        EXPECT_EQ(constVal["key"].type(), Json::nullValue);
        EXPECT_TRUE(constVal.getMemberNames().empty());
        EXPECT_EQ(constVal.get(1u, "default0"), "default0");
        EXPECT_EQ(constVal.get(std::string("not"), "oh"), "oh");
        EXPECT_EQ(constVal.get("missing", "default2"), "default2");
    }

    val = -7;
    EXPECT_EQ(val.type(), Json::intValue);
    EXPECT_EQ(val.size(), 0);
    EXPECT_FALSE(val.isValidIndex(0));
    EXPECT_FALSE(val.isMember("key"));

    val = 42u;
    EXPECT_EQ(val.type(), Json::uintValue);
    EXPECT_EQ(val.size(), 0);
    EXPECT_FALSE(val.isValidIndex(0));
    EXPECT_FALSE(val.isMember("key"));

    val = 3.14159;
    EXPECT_EQ(val.type(), Json::realValue);
    EXPECT_EQ(val.size(), 0);
    EXPECT_FALSE(val.isValidIndex(0));
    EXPECT_FALSE(val.isMember("key"));

    val = true;
    EXPECT_EQ(val.type(), Json::booleanValue);
    EXPECT_EQ(val.size(), 0);
    EXPECT_FALSE(val.isValidIndex(0));
    EXPECT_FALSE(val.isMember("key"));

    val = "string";
    EXPECT_EQ(val.type(), Json::stringValue);
    EXPECT_EQ(val.size(), 0);
    EXPECT_FALSE(val.isValidIndex(0));
    EXPECT_FALSE(val.isMember("key"));

    val = Json::Value(Json::objectValue);
    EXPECT_EQ(val.type(), Json::objectValue);
    EXPECT_EQ(val.size(), 0);
    static Json::StaticString const staticThree("three");
    val[staticThree] = 3;
    val["two"] = 2;
    EXPECT_EQ(val.size(), 2);
    EXPECT_TRUE(val.isValidIndex(1));
    EXPECT_FALSE(val.isValidIndex(2));
    EXPECT_EQ(val[staticThree], 3);
    EXPECT_TRUE(val.isMember("two"));
    EXPECT_TRUE(val.isMember(staticThree));
    EXPECT_FALSE(val.isMember("key"));
    {
        Json::Value const constVal = val;
        EXPECT_EQ(constVal["two"], 2);
        EXPECT_EQ(constVal["four"].type(), Json::nullValue);
        EXPECT_EQ(constVal[staticThree], 3);
        EXPECT_TRUE(constVal.isMember("two"));
        EXPECT_TRUE(constVal.isMember(staticThree));
        EXPECT_FALSE(constVal.isMember("key"));
        EXPECT_EQ(val.get(std::string("two"), "backup"), 2);
        EXPECT_EQ(val.get("missing", "default2"), "default2");
    }

    val = Json::Value(Json::arrayValue);
    EXPECT_EQ(val.type(), Json::arrayValue);
    EXPECT_EQ(val.size(), 0);
    val[0u] = "zero";
    val[1u] = "one";
    EXPECT_EQ(val.size(), 2);
    EXPECT_TRUE(val.isValidIndex(1));
    EXPECT_FALSE(val.isValidIndex(2));
    EXPECT_EQ(val[20u].type(), Json::nullValue);
    EXPECT_FALSE(val.isMember("key"));
    {
        Json::Value const constVal = val;
        EXPECT_EQ(constVal[0u], "zero");
        EXPECT_EQ(constVal[2u].type(), Json::nullValue);
        EXPECT_FALSE(constVal.isMember("key"));
        EXPECT_EQ(val.get(1u, "default0"), "one");
        EXPECT_EQ(val.get(3u, "default1"), "default1");
    }
}

TEST(json_value, remove_members)
{
    Json::Value val;
    EXPECT_EQ(val.removeMember(std::string("member")).type(), Json::nullValue);

    val = Json::Value(Json::objectValue);
    static Json::StaticString const staticThree("three");
    val[staticThree] = 3;
    val["two"] = 2;
    EXPECT_EQ(val.size(), 2);

    EXPECT_EQ(val.removeMember(std::string("six")).type(), Json::nullValue);
    EXPECT_EQ(val.size(), 2);

    EXPECT_EQ(val.removeMember(staticThree), 3);
    EXPECT_EQ(val.size(), 1);

    EXPECT_EQ(val.removeMember(staticThree).type(), Json::nullValue);
    EXPECT_EQ(val.size(), 1);

    EXPECT_EQ(val.removeMember(std::string("two")), 2);
    EXPECT_EQ(val.size(), 0);

    EXPECT_EQ(val.removeMember(std::string("two")).type(), Json::nullValue);
    EXPECT_EQ(val.size(), 0);
}

TEST(json_value, iterator)
{
    {
        // Iterating an array.
        Json::Value arr{Json::arrayValue};
        arr[0u] = "zero";
        arr[1u] = "one";
        arr[2u] = "two";
        arr[3u] = "three";

        Json::ValueIterator const b{arr.begin()};
        Json::ValueIterator const e{arr.end()};

        Json::ValueIterator i1 = b;
        Json::ValueIterator i2 = e;
        --i2;

        // key(), index(), and memberName() on an array iterator.
        EXPECT_TRUE(b != e);
        EXPECT_FALSE(b == e);
        EXPECT_EQ(i1.key(), 0);
        EXPECT_EQ(i2.key(), 3);
        EXPECT_EQ(i1.index(), 0);
        EXPECT_EQ(i2.index(), 3);
        EXPECT_STREQ(i1.memberName(), "");
        EXPECT_STREQ(i2.memberName(), "");

        // Pre and post increment and decrement.
        *i1++ = "0";
        EXPECT_EQ(*i1, "one");
        *i1 = "1";
        ++i1;

        *i2-- = "3";
        EXPECT_EQ(*i2, "two");
        EXPECT_EQ(i1, i2);
        *i2 = "2";
        EXPECT_EQ(*i1, "2");
    }
    {
        // Iterating a const object.
        Json::Value const obj{[]() {
            Json::Value obj{Json::objectValue};
            obj["0"] = 0;
            obj["1"] = 1;
            obj["2"] = 2;
            obj["3"] = 3;
            return obj;
        }()};

        Json::ValueConstIterator i1{obj.begin()};
        Json::ValueConstIterator i2{obj.end()};
        --i2;

        // key(), index(), and memberName() on an object iterator.
        EXPECT_TRUE(i1 != i2);
        EXPECT_FALSE(i1 == i2);
        EXPECT_EQ(i1.key(), "0");
        EXPECT_EQ(i2.key(), "3");
        EXPECT_EQ(i1.index(), -1);
        EXPECT_EQ(i2.index(), -1);
        EXPECT_STREQ(i1.memberName(), "0");
        EXPECT_STREQ(i2.memberName(), "3");

        // Pre and post increment and decrement.
        EXPECT_EQ(*i1++, 0);
        EXPECT_EQ(*i1, 1);
        ++i1;

        EXPECT_EQ(*i2--, 3);
        EXPECT_EQ(*i2, 2);
        EXPECT_EQ(i1, i2);
        EXPECT_EQ(*i1, 2);
    }
    {
        // Iterating a non-const null object.
        Json::Value nul{};
        EXPECT_EQ(nul.begin(), nul.end());
    }
    {
        // Iterating a const Int.
        Json::Value const i{-3};
        EXPECT_EQ(i.begin(), i.end());
    }
}

TEST(json_value, nest_limits)
{
    Json::Reader r;
    {
        auto nest = [](std::uint32_t depth) -> std::string {
            std::string s = "{";
            for (std::uint32_t i{1}; i <= depth; ++i)
                s += "\"obj\":{";
            for (std::uint32_t i{1}; i <= depth; ++i)
                s += "}";
            s += "}";
            return s;
        };

        {
            // Within object nest limit
            auto json{nest(std::min(10u, Json::Reader::nest_limit))};
            Json::Value j;
            EXPECT_TRUE(r.parse(json, j));
        }

        {
            // Exceed object nest limit
            auto json{nest(Json::Reader::nest_limit + 1)};
            Json::Value j;
            EXPECT_FALSE(r.parse(json, j));
        }
    }

    auto nest = [](std::uint32_t depth) -> std::string {
        std::string s = "{";
        for (std::uint32_t i{1}; i <= depth; ++i)
            s += "\"array\":[{";
        for (std::uint32_t i{1}; i <= depth; ++i)
            s += "]}";
        s += "}";
        return s;
    };
    {
        // Exceed array nest limit
        auto json{nest(Json::Reader::nest_limit + 1)};
        Json::Value j;
        EXPECT_FALSE(r.parse(json, j));
    }
}

TEST(json_value, memory_leak)
{
    // When run with the address sanitizer, this test confirms there is no
    // memory leak with the scenarios below.
    {
        Json::Value a;
        a[0u] = 1;
        EXPECT_EQ(a.type(), Json::arrayValue);
        EXPECT_EQ(a[0u].type(), Json::intValue);
        a = std::move(a[0u]);
        EXPECT_EQ(a.type(), Json::intValue);
    }
    {
        Json::Value b;
        Json::Value temp;
        temp["a"] = "Probably avoids the small string optimization";
        temp["b"] = "Also probably avoids the small string optimization";
        EXPECT_EQ(temp.type(), Json::objectValue);
        b.append(temp);
        EXPECT_EQ(temp.type(), Json::objectValue);
        EXPECT_EQ(b.size(), 1);

        b.append(std::move(temp));
        EXPECT_EQ(b.size(), 2);

        // Note that the type() == nullValue check is implementation
        // specific and not guaranteed to be valid in the future.
        EXPECT_EQ(temp.type(), Json::nullValue);
    }
}

}  // namespace xrpl
