#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/json/json_errors.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/json_writer.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <regex>
#include <sstream>
#include <string>

namespace ripple {

TEST_SUITE_BEGIN("json_value");

TEST_CASE("limits")
{
    using namespace Json;
    static_assert(Value::minInt == Int(~(UInt(-1) / 2)));
    static_assert(Value::maxInt == Int(UInt(-1) / 2));
    static_assert(Value::maxUInt == UInt(-1));
}

TEST_CASE("construct and compare Json::StaticString")
{
    static constexpr char sample[]{"Contents of a Json::StaticString"};

    static constexpr Json::StaticString test1(sample);
    char const* addrTest1{test1};

    CHECK(addrTest1 == &sample[0]);
    CHECK(test1.c_str() == &sample[0]);

    static constexpr Json::StaticString test2{
        "Contents of a Json::StaticString"};
    static constexpr Json::StaticString test3{"Another StaticString"};

    CHECK(test1 == test2);
    CHECK(test1 != test3);

    std::string str{sample};
    CHECK(str == test2);
    CHECK(str != test3);
    CHECK(test2 == str);
    CHECK(test3 != str);
}

TEST_CASE("different types")
{
    // Exercise ValueType constructor
    static constexpr Json::StaticString staticStr{"staticStr"};

    auto testCopy = [](Json::ValueType typ) {
        Json::Value val{typ};
        Json::Value cpy{val};
        CHECK(val.type() == typ);
        CHECK(cpy.type() == typ);
        return val;
    };
    {
        Json::Value const nullV{testCopy(Json::nullValue)};
        CHECK(nullV.isNull());
        CHECK(!nullV.isBool());
        CHECK(!nullV.isInt());
        CHECK(!nullV.isUInt());
        CHECK(!nullV.isIntegral());
        CHECK(!nullV.isDouble());
        CHECK(!nullV.isNumeric());
        CHECK(!nullV.isString());
        CHECK(!nullV.isArray());
        CHECK(nullV.isArrayOrNull());
        CHECK(!nullV.isObject());
        CHECK(nullV.isObjectOrNull());
    }
    {
        Json::Value const intV{testCopy(Json::intValue)};
        CHECK(!intV.isNull());
        CHECK(!intV.isBool());
        CHECK(intV.isInt());
        CHECK(!intV.isUInt());
        CHECK(intV.isIntegral());
        CHECK(!intV.isDouble());
        CHECK(intV.isNumeric());
        CHECK(!intV.isString());
        CHECK(!intV.isArray());
        CHECK(!intV.isArrayOrNull());
        CHECK(!intV.isObject());
        CHECK(!intV.isObjectOrNull());
    }
    {
        Json::Value const uintV{testCopy(Json::uintValue)};
        CHECK(!uintV.isNull());
        CHECK(!uintV.isBool());
        CHECK(!uintV.isInt());
        CHECK(uintV.isUInt());
        CHECK(uintV.isIntegral());
        CHECK(!uintV.isDouble());
        CHECK(uintV.isNumeric());
        CHECK(!uintV.isString());
        CHECK(!uintV.isArray());
        CHECK(!uintV.isArrayOrNull());
        CHECK(!uintV.isObject());
        CHECK(!uintV.isObjectOrNull());
    }
    {
        Json::Value const realV{testCopy(Json::realValue)};
        CHECK(!realV.isNull());
        CHECK(!realV.isBool());
        CHECK(!realV.isInt());
        CHECK(!realV.isUInt());
        CHECK(!realV.isIntegral());
        CHECK(realV.isDouble());
        CHECK(realV.isNumeric());
        CHECK(!realV.isString());
        CHECK(!realV.isArray());
        CHECK(!realV.isArrayOrNull());
        CHECK(!realV.isObject());
        CHECK(!realV.isObjectOrNull());
    }
    {
        Json::Value const stringV{testCopy(Json::stringValue)};
        CHECK(!stringV.isNull());
        CHECK(!stringV.isBool());
        CHECK(!stringV.isInt());
        CHECK(!stringV.isUInt());
        CHECK(!stringV.isIntegral());
        CHECK(!stringV.isDouble());
        CHECK(!stringV.isNumeric());
        CHECK(stringV.isString());
        CHECK(!stringV.isArray());
        CHECK(!stringV.isArrayOrNull());
        CHECK(!stringV.isObject());
        CHECK(!stringV.isObjectOrNull());
    }
    {
        Json::Value const staticStrV{staticStr};
        {
            Json::Value cpy{staticStrV};
            CHECK(staticStrV.type() == Json::stringValue);
            CHECK(cpy.type() == Json::stringValue);
        }
        CHECK(!staticStrV.isNull());
        CHECK(!staticStrV.isBool());
        CHECK(!staticStrV.isInt());
        CHECK(!staticStrV.isUInt());
        CHECK(!staticStrV.isIntegral());
        CHECK(!staticStrV.isDouble());
        CHECK(!staticStrV.isNumeric());
        CHECK(staticStrV.isString());
        CHECK(!staticStrV.isArray());
        CHECK(!staticStrV.isArrayOrNull());
        CHECK(!staticStrV.isObject());
        CHECK(!staticStrV.isObjectOrNull());
    }
    {
        Json::Value const boolV{testCopy(Json::booleanValue)};
        CHECK(!boolV.isNull());
        CHECK(boolV.isBool());
        CHECK(!boolV.isInt());
        CHECK(!boolV.isUInt());
        CHECK(boolV.isIntegral());
        CHECK(!boolV.isDouble());
        CHECK(boolV.isNumeric());
        CHECK(!boolV.isString());
        CHECK(!boolV.isArray());
        CHECK(!boolV.isArrayOrNull());
        CHECK(!boolV.isObject());
        CHECK(!boolV.isObjectOrNull());
    }
    {
        Json::Value const arrayV{testCopy(Json::arrayValue)};
        CHECK(!arrayV.isNull());
        CHECK(!arrayV.isBool());
        CHECK(!arrayV.isInt());
        CHECK(!arrayV.isUInt());
        CHECK(!arrayV.isIntegral());
        CHECK(!arrayV.isDouble());
        CHECK(!arrayV.isNumeric());
        CHECK(!arrayV.isString());
        CHECK(arrayV.isArray());
        CHECK(arrayV.isArrayOrNull());
        CHECK(!arrayV.isObject());
        CHECK(!arrayV.isObjectOrNull());
    }
    {
        Json::Value const objectV{testCopy(Json::objectValue)};
        CHECK(!objectV.isNull());
        CHECK(!objectV.isBool());
        CHECK(!objectV.isInt());
        CHECK(!objectV.isUInt());
        CHECK(!objectV.isIntegral());
        CHECK(!objectV.isDouble());
        CHECK(!objectV.isNumeric());
        CHECK(!objectV.isString());
        CHECK(!objectV.isArray());
        CHECK(!objectV.isArrayOrNull());
        CHECK(objectV.isObject());
        CHECK(objectV.isObjectOrNull());
    }
}

TEST_CASE("compare strings")
{
    auto doCompare = [&](Json::Value const& lhs,
                         Json::Value const& rhs,
                         bool lhsEqRhs,
                         bool lhsLtRhs,
                         int line) {
        CAPTURE(line);
        CHECK((lhs == rhs) == lhsEqRhs);
        CHECK((lhs != rhs) != lhsEqRhs);
        CHECK((lhs < rhs) == (!(lhsEqRhs || !lhsLtRhs)));
        CHECK((lhs <= rhs) == (lhsEqRhs || lhsLtRhs));
        CHECK((lhs >= rhs) == (lhsEqRhs || !lhsLtRhs));
        CHECK((lhs > rhs) == (!(lhsEqRhs || lhsLtRhs)));
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

TEST_CASE("bool")
{
    CHECK(!Json::Value());

    CHECK(!Json::Value(""));

    CHECK(bool(Json::Value("empty")));
    CHECK(bool(Json::Value(false)));
    CHECK(bool(Json::Value(true)));
    CHECK(bool(Json::Value(0)));
    CHECK(bool(Json::Value(1)));

    Json::Value array(Json::arrayValue);
    CHECK(!array);
    array.append(0);
    CHECK(bool(array));

    Json::Value object(Json::objectValue);
    CHECK(!object);
    object[""] = false;
    CHECK(bool(object));
}

TEST_CASE("bad json")
{
    char const* s(R"({"method":"ledger","params":[{"ledger_index":1e300}]})");

    Json::Value j;
    Json::Reader r;

    CHECK(r.parse(s, j));
}

TEST_CASE("edge cases")
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

        CHECK(r1.parse(json, j1));
        CHECK(j1["max_uint"].asUInt() == max_uint);
        CHECK(j1["max_uint"].asAbsUInt() == max_uint);
        CHECK(j1["max_int"].asInt() == max_int);
        CHECK(j1["max_int"].asAbsUInt() == max_int);
        CHECK(j1["min_int"].asInt() == min_int);
        CHECK(
            j1["min_int"].asAbsUInt() ==
            static_cast<std::int64_t>(min_int) * -1);
        CHECK(j1["a_uint"].asUInt() == a_uint);
        CHECK(j1["a_uint"].asAbsUInt() == a_uint);
        CHECK(j1["a_uint"] > a_large_int);
        CHECK(j1["a_uint"] > a_small_int);
        CHECK(j1["a_large_int"].asInt() == a_large_int);
        CHECK(j1["a_large_int"].asAbsUInt() == a_large_int);
        CHECK(j1["a_large_int"].asUInt() == a_large_int);
        CHECK(j1["a_large_int"] < a_uint);
        CHECK(j1["a_small_int"].asInt() == a_small_int);
        CHECK(
            j1["a_small_int"].asAbsUInt() ==
            static_cast<std::int64_t>(a_small_int) * -1);
        CHECK(j1["a_small_int"] < a_uint);
    }

    std::uint64_t overflow = std::uint64_t(max_uint) + 1;
    {
        std::string json = "{\"overflow\":";
        json += std::to_string(overflow);
        json += "}";

        Json::Value j2;
        Json::Reader r2;

        CHECK(!r2.parse(json, j2));
    }

    std::int64_t underflow = std::int64_t(min_int) - 1;
    {
        std::string json = "{\"underflow\":";
        json += std::to_string(underflow);
        json += "}";

        Json::Value j3;
        Json::Reader r3;

        CHECK(!r3.parse(json, j3));
    }

    {
        Json::Value intString{std::to_string(overflow)};
        CHECK_THROWS_AS(intString.asUInt(), beast::BadLexicalCast);
        CHECK_THROWS_AS(intString.asAbsUInt(), Json::error);

        intString = "4294967295";
        CHECK(intString.asUInt() == 4294967295u);
        CHECK(intString.asAbsUInt() == 4294967295u);

        intString = "0";
        CHECK(intString.asUInt() == 0);
        CHECK(intString.asAbsUInt() == 0);

        intString = "-1";
        CHECK_THROWS_AS(intString.asUInt(), beast::BadLexicalCast);
        CHECK(intString.asAbsUInt() == 1);

        intString = "-4294967295";
        CHECK(intString.asAbsUInt() == 4294967295);

        intString = "-4294967296";
        CHECK_THROWS_AS(intString.asAbsUInt(), Json::error);

        intString = "2147483648";
        CHECK_THROWS_AS(intString.asInt(), beast::BadLexicalCast);
        CHECK(intString.asAbsUInt() == 2147483648);

        intString = "2147483647";
        CHECK(intString.asInt() == 2147483647);
        CHECK(intString.asAbsUInt() == 2147483647);

        intString = "-2147483648";
        CHECK(intString.asInt() == -2147483648LL);  // MSVC wants the LL
        CHECK(intString.asAbsUInt() == 2147483648LL);

        intString = "-2147483649";
        CHECK_THROWS_AS(intString.asInt(), beast::BadLexicalCast);
        CHECK(intString.asAbsUInt() == 2147483649);
    }

    {
        Json::Value intReal{4294967297.0};
        CHECK_THROWS_AS(intReal.asUInt(), Json::error);
        CHECK_THROWS_AS(intReal.asAbsUInt(), Json::error);

        intReal = 4294967295.0;
        CHECK(intReal.asUInt() == 4294967295u);
        CHECK(intReal.asAbsUInt() == 4294967295u);

        intReal = 0.0;
        CHECK(intReal.asUInt() == 0);
        CHECK(intReal.asAbsUInt() == 0);

        intReal = -1.0;
        CHECK_THROWS_AS(intReal.asUInt(), Json::error);
        CHECK(intReal.asAbsUInt() == 1);

        intReal = -4294967295.0;
        CHECK(intReal.asAbsUInt() == 4294967295);

        intReal = -4294967296.0;
        CHECK_THROWS_AS(intReal.asAbsUInt(), Json::error);

        intReal = 2147483648.0;
        CHECK_THROWS_AS(intReal.asInt(), Json::error);
        CHECK(intReal.asAbsUInt() == 2147483648);

        intReal = 2147483647.0;
        CHECK(intReal.asInt() == 2147483647);
        CHECK(intReal.asAbsUInt() == 2147483647);

        intReal = -2147483648.0;
        CHECK(intReal.asInt() == -2147483648LL);  // MSVC wants the LL
        CHECK(intReal.asAbsUInt() == 2147483648LL);

        intReal = -2147483649.0;
        CHECK_THROWS_AS(intReal.asInt(), Json::error);
        CHECK(intReal.asAbsUInt() == 2147483649);
    }
}

TEST_CASE("copy")
{
    Json::Value v1{2.5};
    CHECK(v1.isDouble());
    CHECK(v1.asDouble() == 2.5);

    Json::Value v2 = v1;
    CHECK(v1.isDouble());
    CHECK(v1.asDouble() == 2.5);
    CHECK(v2.isDouble());
    CHECK(v2.asDouble() == 2.5);
    CHECK(v1 == v2);

    v1 = v2;
    CHECK(v1.isDouble());
    CHECK(v1.asDouble() == 2.5);
    CHECK(v2.isDouble());
    CHECK(v2.asDouble() == 2.5);
    CHECK(v1 == v2);
}

TEST_CASE("move")
{
    Json::Value v1{2.5};
    CHECK(v1.isDouble());
    CHECK(v1.asDouble() == 2.5);

    Json::Value v2 = std::move(v1);
    CHECK(!v1);
    CHECK(v2.isDouble());
    CHECK(v2.asDouble() == 2.5);
    CHECK(v1 != v2);

    v1 = std::move(v2);
    CHECK(v1.isDouble());
    CHECK(v1.asDouble() == 2.5);
    CHECK(!v2);
    CHECK(v1 != v2);
}

TEST_CASE("comparisons")
{
    Json::Value a, b;
    auto testEquals = [&](std::string const& name) {
        CHECK(a == b);
        CHECK(a <= b);
        CHECK(a >= b);

        CHECK(!(a != b));
        CHECK(!(a < b));
        CHECK(!(a > b));

        CHECK(b == a);
        CHECK(b <= a);
        CHECK(b >= a);

        CHECK(!(b != a));
        CHECK(!(b < a));
        CHECK(!(b > a));
    };

    auto testGreaterThan = [&](std::string const& name) {
        CHECK(!(a == b));
        CHECK(!(a <= b));
        CHECK(a >= b);

        CHECK(a != b);
        CHECK(!(a < b));
        CHECK(a > b);

        CHECK(!(b == a));
        CHECK(b <= a);
        CHECK(!(b >= a));

        CHECK(b != a);
        CHECK(b < a);
        CHECK(!(b > a));
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

TEST_CASE("compact")
{
    Json::Value j;
    Json::Reader r;
    char const* s("{\"array\":[{\"12\":23},{},null,false,0.5]}");

    auto countLines = [](std::string const& str) {
        return 1 + std::count_if(str.begin(), str.end(), [](char c) {
                   return c == '\n';
               });
    };

    CHECK(r.parse(s, j));
    {
        std::stringstream ss;
        ss << j;
        CHECK(countLines(ss.str()) > 1);
    }
    {
        std::stringstream ss;
        ss << Json::Compact(std::move(j));
        CHECK(countLines(ss.str()) == 1);
    }
}

TEST_CASE("conversions")
{
    // We have Json::Int, but not Json::Double or Json::Real.
    // We have Json::Int, Json::Value::Int, and Json::ValueType::intValue.
    // We have Json::ValueType::realValue but Json::Value::asDouble.
    // TODO: What's the thinking here?
    {
        // null
        Json::Value val;
        CHECK(val.isNull());
        // val.asCString() should trigger an assertion failure
        CHECK(val.asString() == "");
        CHECK(val.asInt() == 0);
        CHECK(val.asUInt() == 0);
        CHECK(val.asAbsUInt() == 0);
        CHECK(val.asDouble() == 0.0);
        CHECK(val.asBool() == false);

        CHECK(val.isConvertibleTo(Json::nullValue));
        CHECK(val.isConvertibleTo(Json::intValue));
        CHECK(val.isConvertibleTo(Json::uintValue));
        CHECK(val.isConvertibleTo(Json::realValue));
        CHECK(val.isConvertibleTo(Json::stringValue));
        CHECK(val.isConvertibleTo(Json::booleanValue));
        CHECK(val.isConvertibleTo(Json::arrayValue));
        CHECK(val.isConvertibleTo(Json::objectValue));
    }
    {
        // int
        Json::Value val = -1234;
        CHECK(val.isInt());
        // val.asCString() should trigger an assertion failure
        CHECK(val.asString() == "-1234");
        CHECK(val.asInt() == -1234);
        CHECK_THROWS_AS(val.asUInt(), Json::error);
        CHECK(val.asAbsUInt() == 1234u);
        CHECK(val.asDouble() == -1234.0);
        CHECK(val.asBool() == true);

        CHECK(!val.isConvertibleTo(Json::nullValue));
        CHECK(val.isConvertibleTo(Json::intValue));
        CHECK(!val.isConvertibleTo(Json::uintValue));
        CHECK(val.isConvertibleTo(Json::realValue));
        CHECK(val.isConvertibleTo(Json::stringValue));
        CHECK(val.isConvertibleTo(Json::booleanValue));
        CHECK(!val.isConvertibleTo(Json::arrayValue));
        CHECK(!val.isConvertibleTo(Json::objectValue));
    }
    {
        // uint
        Json::Value val = 1234U;
        CHECK(val.isUInt());
        // val.asCString() should trigger an assertion failure
        CHECK(val.asString() == "1234");
        CHECK(val.asInt() == 1234);
        CHECK(val.asUInt() == 1234u);
        CHECK(val.asAbsUInt() == 1234u);
        CHECK(val.asDouble() == 1234.0);
        CHECK(val.asBool() == true);

        CHECK(!val.isConvertibleTo(Json::nullValue));
        CHECK(val.isConvertibleTo(Json::intValue));
        CHECK(val.isConvertibleTo(Json::uintValue));
        CHECK(val.isConvertibleTo(Json::realValue));
        CHECK(val.isConvertibleTo(Json::stringValue));
        CHECK(val.isConvertibleTo(Json::booleanValue));
        CHECK(!val.isConvertibleTo(Json::arrayValue));
        CHECK(!val.isConvertibleTo(Json::objectValue));
    }
    {
        // real
        Json::Value val = 2.0;
        CHECK(val.isDouble());
        // val.asCString() should trigger an assertion failure
        CHECK(std::regex_match(val.asString(), std::regex("^2\\.0*$")));
        CHECK(val.asInt() == 2);
        CHECK(val.asUInt() == 2u);
        CHECK(val.asAbsUInt() == 2u);
        CHECK(val.asDouble() == 2.0);
        CHECK(val.asBool() == true);

        CHECK(!val.isConvertibleTo(Json::nullValue));
        CHECK(val.isConvertibleTo(Json::intValue));
        CHECK(val.isConvertibleTo(Json::uintValue));
        CHECK(val.isConvertibleTo(Json::realValue));
        CHECK(val.isConvertibleTo(Json::stringValue));
        CHECK(val.isConvertibleTo(Json::booleanValue));
        CHECK(!val.isConvertibleTo(Json::arrayValue));
        CHECK(!val.isConvertibleTo(Json::objectValue));
    }
    {
        // numeric string
        Json::Value val = "54321";
        CHECK(val.isString());
        CHECK(strcmp(val.asCString(), "54321") == 0);
        CHECK(val.asString() == "54321");
        CHECK(val.asInt() == 54321);
        CHECK(val.asUInt() == 54321u);
        CHECK(val.asAbsUInt() == 54321);
        CHECK_THROWS_AS(val.asDouble(), Json::error);
        CHECK(val.asBool() == true);

        CHECK(!val.isConvertibleTo(Json::nullValue));
        CHECK(!val.isConvertibleTo(Json::intValue));
        CHECK(!val.isConvertibleTo(Json::uintValue));
        CHECK(!val.isConvertibleTo(Json::realValue));
        CHECK(val.isConvertibleTo(Json::stringValue));
        CHECK(!val.isConvertibleTo(Json::booleanValue));
        CHECK(!val.isConvertibleTo(Json::arrayValue));
        CHECK(!val.isConvertibleTo(Json::objectValue));
    }
    {
        // non-numeric string
        Json::Value val(Json::stringValue);
        CHECK(val.isString());
        CHECK(val.asCString() == nullptr);
        CHECK(val.asString() == "");
        CHECK_THROWS_AS(val.asInt(), std::exception);
        CHECK_THROWS_AS(val.asUInt(), std::exception);
        CHECK_THROWS_AS(val.asAbsUInt(), std::exception);
        CHECK_THROWS_AS(val.asDouble(), std::exception);
        CHECK(val.asBool() == false);

        CHECK(val.isConvertibleTo(Json::nullValue));
        CHECK(!val.isConvertibleTo(Json::intValue));
        CHECK(!val.isConvertibleTo(Json::uintValue));
        CHECK(!val.isConvertibleTo(Json::realValue));
        CHECK(val.isConvertibleTo(Json::stringValue));
        CHECK(!val.isConvertibleTo(Json::booleanValue));
        CHECK(!val.isConvertibleTo(Json::arrayValue));
        CHECK(!val.isConvertibleTo(Json::objectValue));
    }
    {
        // bool false
        Json::Value val = false;
        CHECK(val.isBool());
        // val.asCString() should trigger an assertion failure
        CHECK(val.asString() == "false");
        CHECK(val.asInt() == 0);
        CHECK(val.asUInt() == 0);
        CHECK(val.asAbsUInt() == 0);
        CHECK(val.asDouble() == 0.0);
        CHECK(val.asBool() == false);

        CHECK(val.isConvertibleTo(Json::nullValue));
        CHECK(val.isConvertibleTo(Json::intValue));
        CHECK(val.isConvertibleTo(Json::uintValue));
        CHECK(val.isConvertibleTo(Json::realValue));
        CHECK(val.isConvertibleTo(Json::stringValue));
        CHECK(val.isConvertibleTo(Json::booleanValue));
        CHECK(!val.isConvertibleTo(Json::arrayValue));
        CHECK(!val.isConvertibleTo(Json::objectValue));
    }
    {
        // bool true
        Json::Value val = true;
        CHECK(val.isBool());
        // val.asCString() should trigger an assertion failure
        CHECK(val.asString() == "true");
        CHECK(val.asInt() == 1);
        CHECK(val.asUInt() == 1);
        CHECK(val.asAbsUInt() == 1);
        CHECK(val.asDouble() == 1.0);
        CHECK(val.asBool() == true);

        CHECK(!val.isConvertibleTo(Json::nullValue));
        CHECK(val.isConvertibleTo(Json::intValue));
        CHECK(val.isConvertibleTo(Json::uintValue));
        CHECK(val.isConvertibleTo(Json::realValue));
        CHECK(val.isConvertibleTo(Json::stringValue));
        CHECK(val.isConvertibleTo(Json::booleanValue));
        CHECK(!val.isConvertibleTo(Json::arrayValue));
        CHECK(!val.isConvertibleTo(Json::objectValue));
    }
    {
        // array type
        Json::Value val(Json::arrayValue);
        CHECK(val.isArray());
        // val.asCString should trigger an assertion failure
        CHECK_THROWS_AS(val.asString(), Json::error);
        CHECK_THROWS_AS(val.asInt(), Json::error);
        CHECK_THROWS_AS(val.asUInt(), Json::error);
        CHECK_THROWS_AS(val.asAbsUInt(), Json::error);
        CHECK_THROWS_AS(val.asDouble(), Json::error);
        CHECK(val.asBool() == false);  // empty or not

        CHECK(val.isConvertibleTo(Json::nullValue));
        CHECK(!val.isConvertibleTo(Json::intValue));
        CHECK(!val.isConvertibleTo(Json::uintValue));
        CHECK(!val.isConvertibleTo(Json::realValue));
        CHECK(!val.isConvertibleTo(Json::stringValue));
        CHECK(!val.isConvertibleTo(Json::booleanValue));
        CHECK(val.isConvertibleTo(Json::arrayValue));
        CHECK(!val.isConvertibleTo(Json::objectValue));
    }
    {
        // object type
        Json::Value val(Json::objectValue);
        CHECK(val.isObject());
        // val.asCString should trigger an assertion failure
        CHECK_THROWS_AS(val.asString(), Json::error);
        CHECK_THROWS_AS(val.asInt(), Json::error);
        CHECK_THROWS_AS(val.asUInt(), Json::error);
        CHECK_THROWS_AS(val.asAbsUInt(), Json::error);
        CHECK_THROWS_AS(val.asDouble(), Json::error);
        CHECK(val.asBool() == false);  // empty or not

        CHECK(val.isConvertibleTo(Json::nullValue));
        CHECK(!val.isConvertibleTo(Json::intValue));
        CHECK(!val.isConvertibleTo(Json::uintValue));
        CHECK(!val.isConvertibleTo(Json::realValue));
        CHECK(!val.isConvertibleTo(Json::stringValue));
        CHECK(!val.isConvertibleTo(Json::booleanValue));
        CHECK(!val.isConvertibleTo(Json::arrayValue));
        CHECK(val.isConvertibleTo(Json::objectValue));
    }
}

TEST_CASE("access members")
{
    Json::Value val;
    CHECK(val.type() == Json::nullValue);
    CHECK(val.size() == 0);
    CHECK(!val.isValidIndex(0));
    CHECK(!val.isMember("key"));
    {
        Json::Value const constVal = val;
        CHECK(constVal[7u].type() == Json::nullValue);
        CHECK(!constVal.isMember("key"));
        CHECK(constVal["key"].type() == Json::nullValue);
        CHECK(constVal.getMemberNames().empty());
        CHECK(constVal.get(1u, "default0") == "default0");
        CHECK(constVal.get(std::string("not"), "oh") == "oh");
        CHECK(constVal.get("missing", "default2") == "default2");
    }

    val = -7;
    CHECK(val.type() == Json::intValue);
    CHECK(val.size() == 0);
    CHECK(!val.isValidIndex(0));
    CHECK(!val.isMember("key"));

    val = 42u;
    CHECK(val.type() == Json::uintValue);
    CHECK(val.size() == 0);
    CHECK(!val.isValidIndex(0));
    CHECK(!val.isMember("key"));

    val = 3.14159;
    CHECK(val.type() == Json::realValue);
    CHECK(val.size() == 0);
    CHECK(!val.isValidIndex(0));
    CHECK(!val.isMember("key"));

    val = true;
    CHECK(val.type() == Json::booleanValue);
    CHECK(val.size() == 0);
    CHECK(!val.isValidIndex(0));
    CHECK(!val.isMember("key"));

    val = "string";
    CHECK(val.type() == Json::stringValue);
    CHECK(val.size() == 0);
    CHECK(!val.isValidIndex(0));
    CHECK(!val.isMember("key"));

    val = Json::Value(Json::objectValue);
    CHECK(val.type() == Json::objectValue);
    CHECK(val.size() == 0);
    static Json::StaticString const staticThree("three");
    val[staticThree] = 3;
    val["two"] = 2;
    CHECK(val.size() == 2);
    CHECK(val.isValidIndex(1));
    CHECK(!val.isValidIndex(2));
    CHECK(val[staticThree] == 3);
    CHECK(val.isMember("two"));
    CHECK(val.isMember(staticThree));
    CHECK(!val.isMember("key"));
    {
        Json::Value const constVal = val;
        CHECK(constVal["two"] == 2);
        CHECK(constVal["four"].type() == Json::nullValue);
        CHECK(constVal[staticThree] == 3);
        CHECK(constVal.isMember("two"));
        CHECK(constVal.isMember(staticThree));
        CHECK(!constVal.isMember("key"));
        CHECK(val.get(std::string("two"), "backup") == 2);
        CHECK(val.get("missing", "default2") == "default2");
    }

    val = Json::Value(Json::arrayValue);
    CHECK(val.type() == Json::arrayValue);
    CHECK(val.size() == 0);
    val[0u] = "zero";
    val[1u] = "one";
    CHECK(val.size() == 2);
    CHECK(val.isValidIndex(1));
    CHECK(!val.isValidIndex(2));
    CHECK(val[20u].type() == Json::nullValue);
    CHECK(!val.isMember("key"));
    {
        Json::Value const constVal = val;
        CHECK(constVal[0u] == "zero");
        CHECK(constVal[2u].type() == Json::nullValue);
        CHECK(!constVal.isMember("key"));
        CHECK(val.get(1u, "default0") == "one");
        CHECK(val.get(3u, "default1") == "default1");
    }
}

TEST_CASE("remove members")
{
    Json::Value val;
    CHECK(val.removeMember(std::string("member")).type() == Json::nullValue);

    val = Json::Value(Json::objectValue);
    static Json::StaticString const staticThree("three");
    val[staticThree] = 3;
    val["two"] = 2;
    CHECK(val.size() == 2);

    CHECK(val.removeMember(std::string("six")).type() == Json::nullValue);
    CHECK(val.size() == 2);

    CHECK(val.removeMember(staticThree) == 3);
    CHECK(val.size() == 1);

    CHECK(val.removeMember(staticThree).type() == Json::nullValue);
    CHECK(val.size() == 1);

    CHECK(val.removeMember(std::string("two")) == 2);
    CHECK(val.size() == 0);

    CHECK(val.removeMember(std::string("two")).type() == Json::nullValue);
    CHECK(val.size() == 0);
}

TEST_CASE("iterator")
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

        // key(), index(), and memberName() on an object iterator.
        CHECK(b != e);
        CHECK(!(b == e));
        CHECK(i1.key() == 0);
        CHECK(i2.key() == 3);
        CHECK(i1.index() == 0);
        CHECK(i2.index() == 3);
        CHECK(std::strcmp(i1.memberName(), "") == 0);
        CHECK(std::strcmp(i2.memberName(), "") == 0);

        // Pre and post increment and decrement.
        *i1++ = "0";
        CHECK(*i1 == "one");
        *i1 = "1";
        ++i1;

        *i2-- = "3";
        CHECK(*i2 == "two");
        CHECK(i1 == i2);
        *i2 = "2";
        CHECK(*i1 == "2");
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
        CHECK(i1 != i2);
        CHECK(!(i1 == i2));
        CHECK(i1.key() == "0");
        CHECK(i2.key() == "3");
        CHECK(i1.index() == -1);
        CHECK(i2.index() == -1);
        CHECK(std::strcmp(i1.memberName(), "0") == 0);
        CHECK(std::strcmp(i2.memberName(), "3") == 0);

        // Pre and post increment and decrement.
        CHECK(*i1++ == 0);
        CHECK(*i1 == 1);
        ++i1;

        CHECK(*i2-- == 3);
        CHECK(*i2 == 2);
        CHECK(i1 == i2);
        CHECK(*i1 == 2);
    }
    {
        // Iterating a non-const null object.
        Json::Value nul{};
        CHECK(nul.begin() == nul.end());
    }
    {
        // Iterating a const Int.
        Json::Value const i{-3};
        CHECK(i.begin() == i.end());
    }
}

TEST_CASE("nest limits")
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
            CHECK(r.parse(json, j));
        }

        {
            // Exceed object nest limit
            auto json{nest(Json::Reader::nest_limit + 1)};
            Json::Value j;
            CHECK(!r.parse(json, j));
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
        CHECK(!r.parse(json, j));
    }
}

TEST_CASE("memory leak")
{
    // When run with the address sanitizer, this test confirms there is no
    // memory leak with the scenarios below.
    {
        Json::Value a;
        a[0u] = 1;
        CHECK(a.type() == Json::arrayValue);
        CHECK(a[0u].type() == Json::intValue);
        a = std::move(a[0u]);
        CHECK(a.type() == Json::intValue);
    }
    {
        Json::Value b;
        Json::Value temp;
        temp["a"] = "Probably avoids the small string optimization";
        temp["b"] = "Also probably avoids the small string optimization";
        CHECK(temp.type() == Json::objectValue);
        b.append(temp);
        CHECK(temp.type() == Json::objectValue);
        CHECK(b.size() == 1);

        b.append(std::move(temp));
        CHECK(b.size() == 2);

        // Note that the type() == nullValue check is implementation
        // specific and not guaranteed to be valid in the future.
        CHECK(temp.type() == Json::nullValue);
    }
}

TEST_SUITE_END();

}  // namespace ripple
