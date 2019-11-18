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

#include <ripple/json/json_reader.h>
#include <ripple/json/json_value.h>
#include <ripple/json/json_writer.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/beast/type_name.h>
#include <ripple/beast/unit_test.h>

#include <algorithm>
#include <regex>

namespace ripple {

struct json_value_test : beast::unit_test::suite
{
    void test_StaticString()
    {
        static constexpr char sample[] {"Contents of a Json::StaticString"};

        static constexpr Json::StaticString test1 (sample);
        char const* addrTest1 {test1};

        BEAST_EXPECT (addrTest1 == &sample[0]);
        BEAST_EXPECT (test1.c_str() == &sample[0]);

        static constexpr Json::StaticString test2 {
            "Contents of a Json::StaticString"};
        static constexpr Json::StaticString test3 {"Another StaticString"};

        BEAST_EXPECT (test1 == test2);
        BEAST_EXPECT (test1 != test3);

        std::string str {sample};
        BEAST_EXPECT (str == test2);
        BEAST_EXPECT (str != test3);
        BEAST_EXPECT (test2 == str);
        BEAST_EXPECT (test3 != str);
    }

    void test_types()
    {
        // Exercise ValueType constructor
        static constexpr Json::StaticString staticStr {"staticStr"};

        auto testCopy = [this] (Json::ValueType typ)
        {
            Json::Value val {typ};
            Json::Value cpy {val};
            BEAST_EXPECT (val.type() == typ);
            BEAST_EXPECT (cpy.type() == typ);
            return val;
        };
        {
            Json::Value const nullV {testCopy (Json::nullValue)};
            BEAST_EXPECT (  nullV.isNull());
            BEAST_EXPECT (! nullV.isBool());
            BEAST_EXPECT (! nullV.isInt());
            BEAST_EXPECT (! nullV.isUInt());
            BEAST_EXPECT (! nullV.isIntegral());
            BEAST_EXPECT (! nullV.isDouble());
            BEAST_EXPECT (! nullV.isNumeric());
            BEAST_EXPECT (! nullV.isString());
            BEAST_EXPECT (! nullV.isArray());
            BEAST_EXPECT (  nullV.isArrayOrNull());
            BEAST_EXPECT (! nullV.isObject());
            BEAST_EXPECT (  nullV.isObjectOrNull());
        }
        {
            Json::Value const intV {testCopy (Json::intValue)};
            BEAST_EXPECT (!  intV.isNull());
            BEAST_EXPECT (!  intV.isBool());
            BEAST_EXPECT (  intV.isInt());
            BEAST_EXPECT (! intV.isUInt());
            BEAST_EXPECT (  intV.isIntegral());
            BEAST_EXPECT (! intV.isDouble());
            BEAST_EXPECT (  intV.isNumeric());
            BEAST_EXPECT (! intV.isString());
            BEAST_EXPECT (! intV.isArray());
            BEAST_EXPECT (! intV.isArrayOrNull());
            BEAST_EXPECT (! intV.isObject());
            BEAST_EXPECT (! intV.isObjectOrNull());
        }
        {
            Json::Value const uintV {testCopy (Json::uintValue)};
            BEAST_EXPECT (! uintV.isNull());
            BEAST_EXPECT (! uintV.isBool());
            BEAST_EXPECT (! uintV.isInt());
            BEAST_EXPECT (  uintV.isUInt());
            BEAST_EXPECT (  uintV.isIntegral());
            BEAST_EXPECT (! uintV.isDouble());
            BEAST_EXPECT (  uintV.isNumeric());
            BEAST_EXPECT (! uintV.isString());
            BEAST_EXPECT (! uintV.isArray());
            BEAST_EXPECT (! uintV.isArrayOrNull());
            BEAST_EXPECT (! uintV.isObject());
            BEAST_EXPECT (! uintV.isObjectOrNull());
        }
        {
            Json::Value const realV {testCopy (Json::realValue)};
            BEAST_EXPECT (! realV.isNull());
            BEAST_EXPECT (! realV.isBool());
            BEAST_EXPECT (! realV.isInt());
            BEAST_EXPECT (! realV.isUInt());
            BEAST_EXPECT (! realV.isIntegral());
            BEAST_EXPECT (  realV.isDouble());
            BEAST_EXPECT (  realV.isNumeric());
            BEAST_EXPECT (! realV.isString());
            BEAST_EXPECT (! realV.isArray());
            BEAST_EXPECT (! realV.isArrayOrNull());
            BEAST_EXPECT (! realV.isObject());
            BEAST_EXPECT (! realV.isObjectOrNull());
        }
        {
            Json::Value const stringV {testCopy (Json::stringValue)};
            BEAST_EXPECT (! stringV.isNull());
            BEAST_EXPECT (! stringV.isBool());
            BEAST_EXPECT (! stringV.isInt());
            BEAST_EXPECT (! stringV.isUInt());
            BEAST_EXPECT (! stringV.isIntegral());
            BEAST_EXPECT (! stringV.isDouble());
            BEAST_EXPECT (! stringV.isNumeric());
            BEAST_EXPECT (  stringV.isString());
            BEAST_EXPECT (! stringV.isArray());
            BEAST_EXPECT (! stringV.isArrayOrNull());
            BEAST_EXPECT (! stringV.isObject());
            BEAST_EXPECT (! stringV.isObjectOrNull());
        }
        {
            Json::Value const staticStrV {staticStr};
            {
                Json::Value cpy {staticStrV};
                BEAST_EXPECT (staticStrV.type() == Json::stringValue);
                BEAST_EXPECT (cpy.type() == Json::stringValue);
            }
            BEAST_EXPECT (! staticStrV.isNull());
            BEAST_EXPECT (! staticStrV.isBool());
            BEAST_EXPECT (! staticStrV.isInt());
            BEAST_EXPECT (! staticStrV.isUInt());
            BEAST_EXPECT (! staticStrV.isIntegral());
            BEAST_EXPECT (! staticStrV.isDouble());
            BEAST_EXPECT (! staticStrV.isNumeric());
            BEAST_EXPECT (  staticStrV.isString());
            BEAST_EXPECT (! staticStrV.isArray());
            BEAST_EXPECT (! staticStrV.isArrayOrNull());
            BEAST_EXPECT (! staticStrV.isObject());
            BEAST_EXPECT (! staticStrV.isObjectOrNull());
        }
        {
            Json::Value const boolV {testCopy (Json::booleanValue)};
            BEAST_EXPECT (! boolV.isNull());
            BEAST_EXPECT (  boolV.isBool());
            BEAST_EXPECT (! boolV.isInt());
            BEAST_EXPECT (! boolV.isUInt());
            BEAST_EXPECT (  boolV.isIntegral());
            BEAST_EXPECT (! boolV.isDouble());
            BEAST_EXPECT (  boolV.isNumeric());
            BEAST_EXPECT (! boolV.isString());
            BEAST_EXPECT (! boolV.isArray());
            BEAST_EXPECT (! boolV.isArrayOrNull());
            BEAST_EXPECT (! boolV.isObject());
            BEAST_EXPECT (! boolV.isObjectOrNull());
        }
        {
            Json::Value const arrayV {testCopy (Json::arrayValue)};
            BEAST_EXPECT (! arrayV.isNull());
            BEAST_EXPECT (! arrayV.isBool());
            BEAST_EXPECT (! arrayV.isInt());
            BEAST_EXPECT (! arrayV.isUInt());
            BEAST_EXPECT (! arrayV.isIntegral());
            BEAST_EXPECT (! arrayV.isDouble());
            BEAST_EXPECT (! arrayV.isNumeric());
            BEAST_EXPECT (! arrayV.isString());
            BEAST_EXPECT (  arrayV.isArray());
            BEAST_EXPECT (  arrayV.isArrayOrNull());
            BEAST_EXPECT (! arrayV.isObject());
            BEAST_EXPECT (! arrayV.isObjectOrNull());
        }
        {
            Json::Value const objectV {testCopy (Json::objectValue)};
            BEAST_EXPECT (! objectV.isNull());
            BEAST_EXPECT (! objectV.isBool());
            BEAST_EXPECT (! objectV.isInt());
            BEAST_EXPECT (! objectV.isUInt());
            BEAST_EXPECT (! objectV.isIntegral());
            BEAST_EXPECT (! objectV.isDouble());
            BEAST_EXPECT (! objectV.isNumeric());
            BEAST_EXPECT (! objectV.isString());
            BEAST_EXPECT (! objectV.isArray());
            BEAST_EXPECT (! objectV.isArrayOrNull());
            BEAST_EXPECT (  objectV.isObject());
            BEAST_EXPECT (  objectV.isObjectOrNull());
        }
    }

    void test_compare()
    {
        auto doCompare = [this] (
            Json::Value const& lhs,
            Json::Value const& rhs,
            bool lhsEqRhs,
            bool lhsLtRhs,
            int line)
        {
            auto fmt = [this] (bool cond, char const* text, int line)
            {
                if (cond)
                    this->pass();
                else
                    this->fail (text, __FILE__, line);
            };
            fmt ((lhs == rhs) == lhsEqRhs, "Value ==", line);
            fmt ((lhs != rhs) != lhsEqRhs, "Value !=", line);
            fmt ((lhs <  rhs)  == (! (lhsEqRhs | !lhsLtRhs)), "Value <", line);
            fmt ((lhs <= rhs)  == (   lhsEqRhs |  lhsLtRhs ), "Value <=", line);
            fmt ((lhs >= rhs)  == (   lhsEqRhs | !lhsLtRhs ), "Value >=", line);
            fmt ((lhs >  rhs)  == (! (lhsEqRhs |  lhsLtRhs)), "Value >", line);
        };

        Json::Value const null0;
        Json::Value const intNeg1 {-1};
        Json::Value const int0 {Json::intValue};
        Json::Value const intPos1 {1};
        Json::Value const uint0 {Json::uintValue};
        Json::Value const uint1 {1u};
        Json::Value const realNeg1 {-1.0};
        Json::Value const real0 {Json::realValue};
        Json::Value const realPos1 {1.0};
        Json::Value const str0 {Json::stringValue};
        Json::Value const str1 {"1"};
        Json::Value const boolF {false};
        Json::Value const boolT {true};
        Json::Value const array0 {Json::arrayValue};
        Json::Value const array1 {
            []()
            {
                Json::Value array1;
                array1[0u] = 1;
                return array1;
            }()
        };
        Json::Value const obj0 {Json::objectValue};
        Json::Value const obj1 {
            []()
            {
                Json::Value obj1;
                obj1["one"] = 1;
                return obj1;
            }()
        };
        //                                 lhs == rhs lhs < rhs
        doCompare (null0, Json::Value{},      true,    false, __LINE__);
        doCompare (null0, intNeg1,           false,     true, __LINE__);
        doCompare (null0, int0,              false,     true, __LINE__);
        doCompare (null0, intPos1,           false,     true, __LINE__);
        doCompare (null0, uint0,             false,     true, __LINE__);
        doCompare (null0, uint1,             false,     true, __LINE__);
        doCompare (null0, realNeg1,          false,     true, __LINE__);
        doCompare (null0, real0,             false,     true, __LINE__);
        doCompare (null0, realPos1,          false,     true, __LINE__);
        doCompare (null0, str0,              false,     true, __LINE__);
        doCompare (null0, str1,              false,     true, __LINE__);
        doCompare (null0, boolF,             false,     true, __LINE__);
        doCompare (null0, boolT,             false,     true, __LINE__);
        doCompare (null0, array0,            false,     true, __LINE__);
        doCompare (null0, array1,            false,     true, __LINE__);
        doCompare (null0, obj0,              false,     true, __LINE__);
        doCompare (null0, obj1,              false,     true, __LINE__);
        //                                 lhs == rhs lhs < rhs
        doCompare (intNeg1, null0,           false,    false, __LINE__);
        doCompare (intNeg1, intNeg1,          true,    false, __LINE__);
        doCompare (intNeg1, int0,            false,     true, __LINE__);
        doCompare (intNeg1, intPos1,         false,     true, __LINE__);
        doCompare (intNeg1, uint0,           false,     true, __LINE__);
        doCompare (intNeg1, uint1,           false,     true, __LINE__);
        doCompare (intNeg1, realNeg1,        false,     true, __LINE__);
        doCompare (intNeg1, real0,           false,     true, __LINE__);
        doCompare (intNeg1, realPos1,        false,     true, __LINE__);
        doCompare (intNeg1, str0,            false,     true, __LINE__);
        doCompare (intNeg1, str1,            false,     true, __LINE__);
        doCompare (intNeg1, boolF,           false,     true, __LINE__);
        doCompare (intNeg1, boolT,           false,     true, __LINE__);
        doCompare (intNeg1, array0,          false,     true, __LINE__);
        doCompare (intNeg1, array1,          false,     true, __LINE__);
        doCompare (intNeg1, obj0,            false,     true, __LINE__);
        doCompare (intNeg1, obj1,            false,     true, __LINE__);
        //                                 lhs == rhs lhs < rhs
        doCompare (int0, null0,              false,    false, __LINE__);
        doCompare (int0, intNeg1,            false,    false, __LINE__);
        doCompare (int0, int0,                true,    false, __LINE__);
        doCompare (int0, intPos1,            false,     true, __LINE__);
        doCompare (int0, uint0,               true,    false, __LINE__);
        doCompare (int0, uint1,              false,     true, __LINE__);
        doCompare (int0, realNeg1,           false,     true, __LINE__);
        doCompare (int0, real0,              false,     true, __LINE__);
        doCompare (int0, realPos1,           false,     true, __LINE__);
        doCompare (int0, str0,               false,     true, __LINE__);
        doCompare (int0, str1,               false,     true, __LINE__);
        doCompare (int0, boolF,              false,     true, __LINE__);
        doCompare (int0, boolT,              false,     true, __LINE__);
        doCompare (int0, array0,             false,     true, __LINE__);
        doCompare (int0, array1,             false,     true, __LINE__);
        doCompare (int0, obj0,               false,     true, __LINE__);
        doCompare (int0, obj1,               false,     true, __LINE__);
        //                                 lhs == rhs lhs < rhs
        doCompare (intPos1, null0,           false,    false, __LINE__);
        doCompare (intPos1, intNeg1,         false,    false, __LINE__);
        doCompare (intPos1, int0,            false,    false, __LINE__);
        doCompare (intPos1, intPos1,          true,    false, __LINE__);
        doCompare (intPos1, uint0,           false,    false, __LINE__);
        doCompare (intPos1, uint1,            true,    false, __LINE__);
        doCompare (intPos1, realNeg1,        false,     true, __LINE__);
        doCompare (intPos1, real0,           false,     true, __LINE__);
        doCompare (intPos1, realPos1,        false,     true, __LINE__);
        doCompare (intPos1, str0,            false,     true, __LINE__);
        doCompare (intPos1, str1,            false,     true, __LINE__);
        doCompare (intPos1, boolF,           false,     true, __LINE__);
        doCompare (intPos1, boolT,           false,     true, __LINE__);
        doCompare (intPos1, array0,          false,     true, __LINE__);
        doCompare (intPos1, array1,          false,     true, __LINE__);
        doCompare (intPos1, obj0,            false,     true, __LINE__);
        doCompare (intPos1, obj1,            false,     true, __LINE__);
        //                                 lhs == rhs lhs < rhs
        doCompare (uint0, null0,             false,    false, __LINE__);
        doCompare (uint0, intNeg1,           false,    false, __LINE__);
        doCompare (uint0, int0,               true,    false, __LINE__);
        doCompare (uint0, intPos1,           false,     true, __LINE__);
        doCompare (uint0, uint0,              true,    false, __LINE__);
        doCompare (uint0, uint1,             false,     true, __LINE__);
        doCompare (uint0, realNeg1,          false,     true, __LINE__);
        doCompare (uint0, real0,             false,     true, __LINE__);
        doCompare (uint0, realPos1,          false,     true, __LINE__);
        doCompare (uint0, str0,              false,     true, __LINE__);
        doCompare (uint0, str1,              false,     true, __LINE__);
        doCompare (uint0, boolF,             false,     true, __LINE__);
        doCompare (uint0, boolT,             false,     true, __LINE__);
        doCompare (uint0, array0,            false,     true, __LINE__);
        doCompare (uint0, array1,            false,     true, __LINE__);
        doCompare (uint0, obj0,              false,     true, __LINE__);
        doCompare (uint0, obj1,              false,     true, __LINE__);
        //                                 lhs == rhs lhs < rhs
        doCompare (uint1, null0,             false,    false, __LINE__);
        doCompare (uint1, intNeg1,           false,    false, __LINE__);
        doCompare (uint1, int0,              false,    false, __LINE__);
        doCompare (uint1, intPos1,            true,    false, __LINE__);
        doCompare (uint1, uint0,             false,    false, __LINE__);
        doCompare (uint1, uint1,              true,    false, __LINE__);
        doCompare (uint1, realNeg1,          false,     true, __LINE__);
        doCompare (uint1, real0,             false,     true, __LINE__);
        doCompare (uint1, realPos1,          false,     true, __LINE__);
        doCompare (uint1, str0,              false,     true, __LINE__);
        doCompare (uint1, str1,              false,     true, __LINE__);
        doCompare (uint1, boolF,             false,     true, __LINE__);
        doCompare (uint1, boolT,             false,     true, __LINE__);
        doCompare (uint1, array0,            false,     true, __LINE__);
        doCompare (uint1, array1,            false,     true, __LINE__);
        doCompare (uint1, obj0,              false,     true, __LINE__);
        doCompare (uint1, obj1,              false,     true, __LINE__);
        //                                 lhs == rhs lhs < rhs
        doCompare (realNeg1, null0,          false,    false, __LINE__);
        doCompare (realNeg1, intNeg1,        false,    false, __LINE__);
        doCompare (realNeg1, int0,           false,    false, __LINE__);
        doCompare (realNeg1, intPos1,        false,    false, __LINE__);
        doCompare (realNeg1, uint0,          false,    false, __LINE__);
        doCompare (realNeg1, uint1,          false,    false, __LINE__);
        doCompare (realNeg1, realNeg1,        true,    false, __LINE__);
        doCompare (realNeg1, real0,          false,     true, __LINE__);
        doCompare (realNeg1, realPos1,       false,     true, __LINE__);
        doCompare (realNeg1, str0,           false,     true, __LINE__);
        doCompare (realNeg1, str1,           false,     true, __LINE__);
        doCompare (realNeg1, boolF,          false,     true, __LINE__);
        doCompare (realNeg1, boolT,          false,     true, __LINE__);
        doCompare (realNeg1, array0,         false,     true, __LINE__);
        doCompare (realNeg1, array1,         false,     true, __LINE__);
        doCompare (realNeg1, obj0,           false,     true, __LINE__);
        doCompare (realNeg1, obj1,           false,     true, __LINE__);
        //                                 lhs == rhs lhs < rhs
        doCompare (real0, null0,             false,    false, __LINE__);
        doCompare (real0, intNeg1,           false,    false, __LINE__);
        doCompare (real0, int0,              false,    false, __LINE__);
        doCompare (real0, intPos1,           false,    false, __LINE__);
        doCompare (real0, uint0,             false,    false, __LINE__);
        doCompare (real0, uint1,             false,    false, __LINE__);
        doCompare (real0, realNeg1,          false,    false, __LINE__);
        doCompare (real0, real0,              true,    false, __LINE__);
        doCompare (real0, realPos1,          false,     true, __LINE__);
        doCompare (real0, str0,              false,     true, __LINE__);
        doCompare (real0, str1,              false,     true, __LINE__);
        doCompare (real0, boolF,             false,     true, __LINE__);
        doCompare (real0, boolT,             false,     true, __LINE__);
        doCompare (real0, array0,            false,     true, __LINE__);
        doCompare (real0, array1,            false,     true, __LINE__);
        doCompare (real0, obj0,              false,     true, __LINE__);
        doCompare (real0, obj1,              false,     true, __LINE__);
        //                                 lhs == rhs lhs < rhs
        doCompare (realPos1, null0,          false,    false, __LINE__);
        doCompare (realPos1, intNeg1,        false,    false, __LINE__);
        doCompare (realPos1, int0,           false,    false, __LINE__);
        doCompare (realPos1, intPos1,        false,    false, __LINE__);
        doCompare (realPos1, uint0,          false,    false, __LINE__);
        doCompare (realPos1, uint1,          false,    false, __LINE__);
        doCompare (realPos1, realNeg1,       false,    false, __LINE__);
        doCompare (realPos1, real0,          false,    false, __LINE__);
        doCompare (realPos1, realPos1,        true,    false, __LINE__);
        doCompare (realPos1, str0,           false,     true, __LINE__);
        doCompare (realPos1, str1,           false,     true, __LINE__);
        doCompare (realPos1, boolF,          false,     true, __LINE__);
        doCompare (realPos1, boolT,          false,     true, __LINE__);
        doCompare (realPos1, array0,         false,     true, __LINE__);
        doCompare (realPos1, array1,         false,     true, __LINE__);
        doCompare (realPos1, obj0,           false,     true, __LINE__);
        doCompare (realPos1, obj1,           false,     true, __LINE__);
        //                                 lhs == rhs lhs < rhs
        doCompare (str0, null0,              false,    false, __LINE__);
        doCompare (str0, intNeg1,            false,    false, __LINE__);
        doCompare (str0, int0,               false,    false, __LINE__);
        doCompare (str0, intPos1,            false,    false, __LINE__);
        doCompare (str0, uint0,              false,    false, __LINE__);
        doCompare (str0, uint1,              false,    false, __LINE__);
        doCompare (str0, realNeg1,           false,    false, __LINE__);
        doCompare (str0, real0,              false,    false, __LINE__);
        doCompare (str0, realPos1,           false,    false, __LINE__);
        doCompare (str0, str0,                true,    false, __LINE__);
        doCompare (str0, str1,               false,     true, __LINE__);
        doCompare (str0, boolF,              false,     true, __LINE__);
        doCompare (str0, boolT,              false,     true, __LINE__);
        doCompare (str0, array0,             false,     true, __LINE__);
        doCompare (str0, array1,             false,     true, __LINE__);
        doCompare (str0, obj0,               false,     true, __LINE__);
        doCompare (str0, obj1,               false,     true, __LINE__);
        //                                 lhs == rhs lhs < rhs
        doCompare (str1, null0,              false,    false, __LINE__);
        doCompare (str1, intNeg1,            false,    false, __LINE__);
        doCompare (str1, int0,               false,    false, __LINE__);
        doCompare (str1, intPos1,            false,    false, __LINE__);
        doCompare (str1, uint0,              false,    false, __LINE__);
        doCompare (str1, uint1,              false,    false, __LINE__);
        doCompare (str1, realNeg1,           false,    false, __LINE__);
        doCompare (str1, real0,              false,    false, __LINE__);
        doCompare (str1, realPos1,           false,    false, __LINE__);
        doCompare (str1, str0,               false,    false, __LINE__);
        doCompare (str1, str1,                true,    false, __LINE__);
        doCompare (str1, boolF,              false,     true, __LINE__);
        doCompare (str1, boolT,              false,     true, __LINE__);
        doCompare (str1, array0,             false,     true, __LINE__);
        doCompare (str1, array1,             false,     true, __LINE__);
        doCompare (str1, obj0,               false,     true, __LINE__);
        doCompare (str1, obj1,               false,     true, __LINE__);
        //                                 lhs == rhs lhs < rhs
        doCompare (boolF, null0,             false,    false, __LINE__);
        doCompare (boolF, intNeg1,           false,    false, __LINE__);
        doCompare (boolF, int0,              false,    false, __LINE__);
        doCompare (boolF, intPos1,           false,    false, __LINE__);
        doCompare (boolF, uint0,             false,    false, __LINE__);
        doCompare (boolF, uint1,             false,    false, __LINE__);
        doCompare (boolF, realNeg1,          false,    false, __LINE__);
        doCompare (boolF, real0,             false,    false, __LINE__);
        doCompare (boolF, realPos1,          false,    false, __LINE__);
        doCompare (boolF, str0,              false,    false, __LINE__);
        doCompare (boolF, str1,              false,    false, __LINE__);
        doCompare (boolF, boolF,              true,    false, __LINE__);
        doCompare (boolF, boolT,             false,     true, __LINE__);
        doCompare (boolF, array0,            false,     true, __LINE__);
        doCompare (boolF, array1,            false,     true, __LINE__);
        doCompare (boolF, obj0,              false,     true, __LINE__);
        doCompare (boolF, obj1,              false,     true, __LINE__);
        //                                 lhs == rhs lhs < rhs
        doCompare (boolT, null0,             false,    false, __LINE__);
        doCompare (boolT, intNeg1,           false,    false, __LINE__);
        doCompare (boolT, int0,              false,    false, __LINE__);
        doCompare (boolT, intPos1,           false,    false, __LINE__);
        doCompare (boolT, uint0,             false,    false, __LINE__);
        doCompare (boolT, uint1,             false,    false, __LINE__);
        doCompare (boolT, realNeg1,          false,    false, __LINE__);
        doCompare (boolT, real0,             false,    false, __LINE__);
        doCompare (boolT, realPos1,          false,    false, __LINE__);
        doCompare (boolT, str0,              false,    false, __LINE__);
        doCompare (boolT, str1,              false,    false, __LINE__);
        doCompare (boolT, boolF,             false,    false, __LINE__);
        doCompare (boolT, boolT,              true,    false, __LINE__);
        doCompare (boolT, array0,            false,     true, __LINE__);
        doCompare (boolT, array1,            false,     true, __LINE__);
        doCompare (boolT, obj0,              false,     true, __LINE__);
        doCompare (boolT, obj1,              false,     true, __LINE__);
        //                                 lhs == rhs lhs < rhs
        doCompare (array0, null0,            false,    false, __LINE__);
        doCompare (array0, intNeg1,          false,    false, __LINE__);
        doCompare (array0, int0,             false,    false, __LINE__);
        doCompare (array0, intPos1,          false,    false, __LINE__);
        doCompare (array0, uint0,            false,    false, __LINE__);
        doCompare (array0, uint1,            false,    false, __LINE__);
        doCompare (array0, realNeg1,         false,    false, __LINE__);
        doCompare (array0, real0,            false,    false, __LINE__);
        doCompare (array0, realPos1,         false,    false, __LINE__);
        doCompare (array0, str0,             false,    false, __LINE__);
        doCompare (array0, str1,             false,    false, __LINE__);
        doCompare (array0, boolF,            false,    false, __LINE__);
        doCompare (array0, boolT,            false,    false, __LINE__);
        doCompare (array0, array0,            true,    false, __LINE__);
        doCompare (array0, array1,           false,     true, __LINE__);
        doCompare (array0, obj0,             false,     true, __LINE__);
        doCompare (array0, obj1,             false,     true, __LINE__);
        //                                 lhs == rhs lhs < rhs
        doCompare (array1, null0,            false,    false, __LINE__);
        doCompare (array1, intNeg1,          false,    false, __LINE__);
        doCompare (array1, int0,             false,    false, __LINE__);
        doCompare (array1, intPos1,          false,    false, __LINE__);
        doCompare (array1, uint0,            false,    false, __LINE__);
        doCompare (array1, uint1,            false,    false, __LINE__);
        doCompare (array1, realNeg1,         false,    false, __LINE__);
        doCompare (array1, real0,            false,    false, __LINE__);
        doCompare (array1, realPos1,         false,    false, __LINE__);
        doCompare (array1, str0,             false,    false, __LINE__);
        doCompare (array1, str1,             false,    false, __LINE__);
        doCompare (array1, boolF,            false,    false, __LINE__);
        doCompare (array1, boolT,            false,    false, __LINE__);
        doCompare (array1, array0,           false,    false, __LINE__);
        doCompare (array1, array1,            true,    false, __LINE__);
        doCompare (array1, obj0,             false,     true, __LINE__);
        doCompare (array1, obj1,             false,     true, __LINE__);
        //                                 lhs == rhs lhs < rhs
        doCompare (obj0, null0,              false,    false, __LINE__);
        doCompare (obj0, intNeg1,            false,    false, __LINE__);
        doCompare (obj0, int0,               false,    false, __LINE__);
        doCompare (obj0, intPos1,            false,    false, __LINE__);
        doCompare (obj0, uint0,              false,    false, __LINE__);
        doCompare (obj0, uint1,              false,    false, __LINE__);
        doCompare (obj0, realNeg1,           false,    false, __LINE__);
        doCompare (obj0, real0,              false,    false, __LINE__);
        doCompare (obj0, realPos1,           false,    false, __LINE__);
        doCompare (obj0, str0,               false,    false, __LINE__);
        doCompare (obj0, str1,               false,    false, __LINE__);
        doCompare (obj0, boolF,              false,    false, __LINE__);
        doCompare (obj0, boolT,              false,    false, __LINE__);
        doCompare (obj0, array0,             false,    false, __LINE__);
        doCompare (obj0, array1,             false,    false, __LINE__);
        doCompare (obj0, obj0,                true,    false, __LINE__);
        doCompare (obj0, obj1,               false,     true, __LINE__);
        //                                 lhs == rhs lhs < rhs
        doCompare (obj1, null0,              false,    false, __LINE__);
        doCompare (obj1, intNeg1,            false,    false, __LINE__);
        doCompare (obj1, int0,               false,    false, __LINE__);
        doCompare (obj1, intPos1,            false,    false, __LINE__);
        doCompare (obj1, uint0,              false,    false, __LINE__);
        doCompare (obj1, uint1,              false,    false, __LINE__);
        doCompare (obj1, realNeg1,           false,    false, __LINE__);
        doCompare (obj1, real0,              false,    false, __LINE__);
        doCompare (obj1, realPos1,           false,    false, __LINE__);
        doCompare (obj1, str0,               false,    false, __LINE__);
        doCompare (obj1, str1,               false,    false, __LINE__);
        doCompare (obj1, boolF,              false,    false, __LINE__);
        doCompare (obj1, boolT,              false,    false, __LINE__);
        doCompare (obj1, array0,             false,    false, __LINE__);
        doCompare (obj1, array1,             false,    false, __LINE__);
        doCompare (obj1, obj0,               false,    false, __LINE__);
        doCompare (obj1, obj1,                true,    false, __LINE__);
    }

    void test_bool()
    {
        BEAST_EXPECT(! Json::Value());

        BEAST_EXPECT(! Json::Value(""));

        BEAST_EXPECT(bool (Json::Value("empty")));
        BEAST_EXPECT(bool (Json::Value(false)));
        BEAST_EXPECT(bool (Json::Value(true)));
        BEAST_EXPECT(bool (Json::Value(0)));
        BEAST_EXPECT(bool (Json::Value(1)));

        Json::Value array (Json::arrayValue);
        BEAST_EXPECT(! array);
        array.append(0);
        BEAST_EXPECT(bool (array));

        Json::Value object (Json::objectValue);
        BEAST_EXPECT(! object);
        object[""] = false;
        BEAST_EXPECT(bool (object));
    }

    void test_bad_json ()
    {
        char const* s (
            "{\"method\":\"ledger\",\"params\":[{\"ledger_index\":1e300}]}"
            );

        Json::Value j;
        Json::Reader r;

        r.parse (s, j);
        pass ();
    }

    void test_edge_cases ()
    {
        std::string json;

        std::uint32_t max_uint = std::numeric_limits<std::uint32_t>::max ();
        std::int32_t max_int = std::numeric_limits<std::int32_t>::max ();
        std::int32_t min_int = std::numeric_limits<std::int32_t>::min ();

        std::uint32_t a_uint = max_uint - 1978;
        std::int32_t a_large_int = max_int - 1978;
        std::int32_t a_small_int = min_int + 1978;

        json  = "{\"max_uint\":"    + std::to_string (max_uint);
        json += ",\"max_int\":"     + std::to_string (max_int);
        json += ",\"min_int\":"     + std::to_string (min_int);
        json += ",\"a_uint\":"      + std::to_string (a_uint);
        json += ",\"a_large_int\":" + std::to_string (a_large_int);
        json += ",\"a_small_int\":" + std::to_string (a_small_int);
        json += "}";

        Json::Value j1;
        Json::Reader r1;

        BEAST_EXPECT(r1.parse (json, j1));
        BEAST_EXPECT(j1["max_uint"].asUInt() == max_uint);
        BEAST_EXPECT(j1["max_int"].asInt() == max_int);
        BEAST_EXPECT(j1["min_int"].asInt() == min_int);
        BEAST_EXPECT(j1["a_uint"].asUInt() == a_uint);
        BEAST_EXPECT(j1["a_uint"] > a_large_int);
        BEAST_EXPECT(j1["a_uint"] > a_small_int);
        BEAST_EXPECT(j1["a_large_int"].asInt() == a_large_int);
        BEAST_EXPECT(j1["a_large_int"].asUInt() == a_large_int);
        BEAST_EXPECT(j1["a_large_int"] < a_uint);
        BEAST_EXPECT(j1["a_small_int"].asInt() == a_small_int);
        BEAST_EXPECT(j1["a_small_int"] < a_uint);

        json  = "{\"overflow\":";
        json += std::to_string(std::uint64_t(max_uint) + 1);
        json += "}";

        Json::Value j2;
        Json::Reader r2;

        BEAST_EXPECT(!r2.parse (json, j2));

        json  = "{\"underflow\":";
        json += std::to_string(std::int64_t(min_int) - 1);
        json += "}";

        Json::Value j3;
        Json::Reader r3;

        BEAST_EXPECT(!r3.parse (json, j3));

        Json::Value intString {"4294967296"};
        try
        {
            [[maybe_unused]] std::uint32_t const uTooBig {intString.asUInt()};
            fail("4294967296", __FILE__, __LINE__);
        } catch (beast::BadLexicalCast const&)
        {
            pass();
        }

        intString = "4294967295";
        BEAST_EXPECT (intString.asUInt() == 4294967295u);

        intString = "0";
        BEAST_EXPECT (intString.asUInt() == 0);

        intString = "-1";
        try
        {
            [[maybe_unused]] std::uint32_t const uTooSmall {intString.asUInt()};
            fail("-1", __FILE__, __LINE__);
        } catch (beast::BadLexicalCast const&)
        {
            pass();
        }

        intString = "2147483648";
        try
        {
            [[maybe_unused]] std::int32_t tooPos {intString.asInt()};
            fail("2147483648", __FILE__, __LINE__);
        } catch (beast::BadLexicalCast const&)
        {
            pass();
        }

        intString = "2147483647";
        BEAST_EXPECT (intString.asInt() == 2147483647);

        intString = "-2147483648";
        BEAST_EXPECT (intString.asInt() == -2147483648LL); // MSVC wants the LL

        intString = "-2147483649";
        try
        {
            [[maybe_unused]] std::int32_t tooNeg {intString.asInt()};
            fail("-2147483649", __FILE__, __LINE__);
        } catch (beast::BadLexicalCast const&)
        {
            pass();
        }
    }

    void
    test_copy ()
    {
        Json::Value v1{2.5};
        BEAST_EXPECT(v1.isDouble ());
        BEAST_EXPECT(v1.asDouble () == 2.5);

        Json::Value v2 = v1;
        BEAST_EXPECT(v1.isDouble ());
        BEAST_EXPECT(v1.asDouble () == 2.5);
        BEAST_EXPECT(v2.isDouble ());
        BEAST_EXPECT(v2.asDouble () == 2.5);
        BEAST_EXPECT(v1 == v2);

        v1 = v2;
        BEAST_EXPECT(v1.isDouble ());
        BEAST_EXPECT(v1.asDouble () == 2.5);
        BEAST_EXPECT(v2.isDouble ());
        BEAST_EXPECT(v2.asDouble () == 2.5);
        BEAST_EXPECT(v1 == v2);

        pass ();
    }

    void
    test_move ()
    {
        Json::Value v1{2.5};
        BEAST_EXPECT(v1.isDouble ());
        BEAST_EXPECT(v1.asDouble () == 2.5);

        Json::Value v2 = std::move(v1);
        BEAST_EXPECT(!v1);
        BEAST_EXPECT(v2.isDouble ());
        BEAST_EXPECT(v2.asDouble () == 2.5);
        BEAST_EXPECT(v1 != v2);

        v1 = std::move(v2);
        BEAST_EXPECT(v1.isDouble ());
        BEAST_EXPECT(v1.asDouble () == 2.5);
        BEAST_EXPECT(! v2);
        BEAST_EXPECT(v1 != v2);

        pass ();
    }

    void
    test_comparisons()
    {
        Json::Value a, b;
        auto testEquals = [&] (std::string const& name) {
            BEAST_EXPECT(a == b);
            BEAST_EXPECT(a <= b);
            BEAST_EXPECT(a >= b);

            BEAST_EXPECT(! (a != b));
            BEAST_EXPECT(! (a < b));
            BEAST_EXPECT(! (a > b));

            BEAST_EXPECT(b == a);
            BEAST_EXPECT(b <= a);
            BEAST_EXPECT(b >= a);

            BEAST_EXPECT(! (b != a));
            BEAST_EXPECT(! (b < a));
            BEAST_EXPECT(! (b > a));
        };

        auto testGreaterThan = [&] (std::string const& name) {
            BEAST_EXPECT(! (a == b));
            BEAST_EXPECT(! (a <= b));
            BEAST_EXPECT(a >= b);

            BEAST_EXPECT(a != b);
            BEAST_EXPECT(! (a < b));
            BEAST_EXPECT(a > b);

            BEAST_EXPECT(! (b == a));
            BEAST_EXPECT(b <= a);
            BEAST_EXPECT(! (b >= a));

            BEAST_EXPECT(b != a);
            BEAST_EXPECT(b < a);
            BEAST_EXPECT(! (b > a));
        };

        a["a"] = Json::UInt (0);
        b["a"] = Json::Int (0);
        testEquals ("zero");

        b["a"] = Json::Int (-1);
        testGreaterThan ("negative");

        Json::Int big = std::numeric_limits<int>::max();
        Json::UInt bigger = big;
        bigger++;

        a["a"] = bigger;
        b["a"] = big;
        testGreaterThan ("big");
    }

    void test_compact ()
    {
        Json::Value j;
        Json::Reader r;
        char const* s ("{\"array\":[{\"12\":23},{},null,false,0.5]}");

        auto countLines = [](std::string const & str)
        {
            return 1 + std::count_if(str.begin(), str.end(), [](char c){
                return c == '\n';
            });
        };

        BEAST_EXPECT(r.parse(s,j));
        {
            std::stringstream ss;
            ss << j;
            BEAST_EXPECT(countLines(ss.str()) > 1);
        }
        {
            std::stringstream ss;
            ss << Json::Compact(std::move(j));
            BEAST_EXPECT(countLines(ss.str()) == 1);
        }
    }

    void test_conversions ()
    {
        // We have Json::Int, but not Json::Double or Json::Real.
        // We have Json::Int, Json::Value::Int, and Json::ValueType::intValue.
        // We have Json::ValueType::realValue but Json::Value::asDouble.
        // TODO: What's the thinking here?
        {
            // null
            Json::Value val;
            BEAST_EXPECT(val.isNull());
//          BEAST_EXPECT(strcmp (val.asCString(), ?) == 0); // asserts
            BEAST_EXPECT(val.asString() == "");
            BEAST_EXPECT(val.asInt() == 0);
            BEAST_EXPECT(val.asUInt() == 0);
            BEAST_EXPECT(val.asDouble() == 0.0);
            BEAST_EXPECT(val.asBool() == false);

            BEAST_EXPECT(  val.isConvertibleTo (Json::nullValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::intValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::uintValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::realValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::stringValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::booleanValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::arrayValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::objectValue));
        }
        {
            // int
            Json::Value val = -1234;
            BEAST_EXPECT(val.isInt());
//          BEAST_EXPECT(strcmp (val.asCString(), ?) == 0); // asserts
            BEAST_EXPECT(val.asString() == "-1234");
            BEAST_EXPECT(val.asInt() == -1234);
//          BEAST_EXPECT(val.asUInt() == ?);                // asserts or throws
            BEAST_EXPECT(val.asDouble() == -1234.0);
            BEAST_EXPECT(val.asBool() == true);

            BEAST_EXPECT(! val.isConvertibleTo (Json::nullValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::intValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::uintValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::realValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::stringValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::booleanValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::arrayValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::objectValue));
        }
        {
            // uint
            Json::Value val = 1234U;
            BEAST_EXPECT(val.isUInt());
//          BEAST_EXPECT(strcmp (val.asCString(), ?) == 0); // asserts
            BEAST_EXPECT(val.asString() == "1234");
            BEAST_EXPECT(val.asInt() == 1234);
            BEAST_EXPECT(val.asUInt() == 1234u);
            BEAST_EXPECT(val.asDouble() == 1234.0);
            BEAST_EXPECT(val.asBool() == true);

            BEAST_EXPECT(! val.isConvertibleTo (Json::nullValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::intValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::uintValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::realValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::stringValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::booleanValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::arrayValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::objectValue));
        }
        {
            // real
            Json::Value val = 2.0;
            BEAST_EXPECT(val.isDouble());
//          BEAST_EXPECT(strcmp (val.asCString(), ?) == 0); // asserts
            BEAST_EXPECT(std::regex_match(
                val.asString(), std::regex("^2\\.0*$")));
            BEAST_EXPECT(val.asInt() == 2);
            BEAST_EXPECT(val.asUInt() == 2u);
            BEAST_EXPECT(val.asDouble() == 2.0);
            BEAST_EXPECT(val.asBool() == true);

            BEAST_EXPECT(! val.isConvertibleTo (Json::nullValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::intValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::uintValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::realValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::stringValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::booleanValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::arrayValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::objectValue));
        }
        {
            // numeric string
            Json::Value val = "54321";
            BEAST_EXPECT(val.isString());
            BEAST_EXPECT(strcmp (val.asCString(), "54321") == 0);
            BEAST_EXPECT(val.asString() == "54321");
            BEAST_EXPECT(val.asInt() == 54321);
            BEAST_EXPECT(val.asUInt() == 54321u);
//          BEAST_EXPECT(val.asDouble() == 54321.0);        // asserts or throws
            BEAST_EXPECT(val.asBool() == true);

            BEAST_EXPECT(! val.isConvertibleTo (Json::nullValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::intValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::uintValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::realValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::stringValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::booleanValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::arrayValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::objectValue));
        }
        {
            // non-numeric string
            Json::Value val (Json::stringValue);
            BEAST_EXPECT(val.isString());
            BEAST_EXPECT(val.asCString() == nullptr);
            BEAST_EXPECT(val.asString() == "");
            try {
                BEAST_EXPECT(val.asInt() == 0);
                fail("expected exception", __FILE__, __LINE__);
            } catch (std::exception const&) {
                pass();
            }
            try {
                BEAST_EXPECT(val.asUInt() == 0);
                fail("expected exception", __FILE__, __LINE__);
            } catch (std::exception const&) {
                pass();
            }
//          BEAST_EXPECT(val.asDouble() == ?);              // asserts or throws
            BEAST_EXPECT(val.asBool() == false);

            BEAST_EXPECT(  val.isConvertibleTo (Json::nullValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::intValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::uintValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::realValue));
            BEAST_EXPECT(val.isConvertibleTo (Json::stringValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::booleanValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::arrayValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::objectValue));
        }
        {
            // bool false
            Json::Value val = false;
            BEAST_EXPECT(val.isBool());
//          BEAST_EXPECT(strcmp (val.asCString(), ?) == 0); // asserts
            BEAST_EXPECT(val.asString() == "false");
            BEAST_EXPECT(val.asInt() == 0);
            BEAST_EXPECT(val.asUInt() == 0);
            BEAST_EXPECT(val.asDouble() == 0.0);
            BEAST_EXPECT(val.asBool() == false);

            BEAST_EXPECT(  val.isConvertibleTo (Json::nullValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::intValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::uintValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::realValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::stringValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::booleanValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::arrayValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::objectValue));
        }
        {
            // bool true
            Json::Value val = true;
            BEAST_EXPECT(val.isBool());
//          BEAST_EXPECT(strcmp (val.asCString(), ?) == 0); // asserts
            BEAST_EXPECT(val.asString() == "true");
            BEAST_EXPECT(val.asInt() == 1);
            BEAST_EXPECT(val.asUInt() == 1);
            BEAST_EXPECT(val.asDouble() == 1.0);
            BEAST_EXPECT(val.asBool() == true);

            BEAST_EXPECT(! val.isConvertibleTo (Json::nullValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::intValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::uintValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::realValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::stringValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::booleanValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::arrayValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::objectValue));
        }
        {
            // array type
            Json::Value val (Json::arrayValue);
            BEAST_EXPECT (val.isArray());
//          BEAST_EXPECT(strcmp (val.asCString(), ?) == 0); // asserts
//          BEAST_EXPECT(val.asString() == ?);              // asserts or throws
//          BEAST_EXPECT(val.asInt() == ?);                 // asserts or throws
//          BEAST_EXPECT(val.asUInt() == ?);                // asserts or throws
//          BEAST_EXPECT(val.asDouble() == ?);              // asserts or throws
            BEAST_EXPECT(val.asBool() == false);            // empty or not

            BEAST_EXPECT(  val.isConvertibleTo (Json::nullValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::intValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::uintValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::realValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::stringValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::booleanValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::arrayValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::objectValue));
        }
        {
            // object type
            Json::Value val (Json::objectValue);
            BEAST_EXPECT (val.isObject());
//          BEAST_EXPECT(strcmp (val.asCString(), ?) == 0); // asserts
//          BEAST_EXPECT(strcmp (val.asCString(), ?) == 0); // asserts
//          BEAST_EXPECT(val.asString() == ?);              // asserts or throws
//          BEAST_EXPECT(val.asInt() == ?);                 // asserts or throws
//          BEAST_EXPECT(val.asUInt() == ?);                // asserts or throws
            BEAST_EXPECT(val.asBool() == false);            // empty or not

            BEAST_EXPECT(  val.isConvertibleTo (Json::nullValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::intValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::uintValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::realValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::stringValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::booleanValue));
            BEAST_EXPECT(! val.isConvertibleTo (Json::arrayValue));
            BEAST_EXPECT(  val.isConvertibleTo (Json::objectValue));
        }
    }

    void
    test_access()
    {
        Json::Value val;
        BEAST_EXPECT (val.type() == Json::nullValue);
        BEAST_EXPECT (val.size() == 0);
        BEAST_EXPECT (! val.isValidIndex(0));
        BEAST_EXPECT (! val.isMember("key"));
        {
            Json::Value const constVal = val;
            BEAST_EXPECT (constVal[7u].type() == Json::nullValue);
            BEAST_EXPECT (! constVal.isMember("key"));
            BEAST_EXPECT (constVal["key"].type() == Json::nullValue);
            BEAST_EXPECT (constVal.getMemberNames().empty());
            BEAST_EXPECT (constVal.get (1u, "default0") == "default0");
            BEAST_EXPECT (constVal.get (std::string ("not"), "oh") == "oh");
            BEAST_EXPECT (constVal.get ("missing", "default2") == "default2");
        }

        val = -7;
        BEAST_EXPECT (val.type() == Json::intValue);
        BEAST_EXPECT (val.size() == 0);
        BEAST_EXPECT (! val.isValidIndex(0));
        BEAST_EXPECT (! val.isMember("key"));

        val = 42u;
        BEAST_EXPECT (val.type() == Json::uintValue);
        BEAST_EXPECT (val.size() == 0);
        BEAST_EXPECT (! val.isValidIndex(0));
        BEAST_EXPECT (! val.isMember("key"));

        val = 3.14159;
        BEAST_EXPECT (val.type() == Json::realValue);
        BEAST_EXPECT (val.size() == 0);
        BEAST_EXPECT (! val.isValidIndex(0));
        BEAST_EXPECT (! val.isMember("key"));

        val = true;
        BEAST_EXPECT (val.type() == Json::booleanValue);
        BEAST_EXPECT (val.size() == 0);
        BEAST_EXPECT (! val.isValidIndex(0));
        BEAST_EXPECT (! val.isMember("key"));

        val = "string";
        BEAST_EXPECT (val.type() == Json::stringValue);
        BEAST_EXPECT (val.size() == 0);
        BEAST_EXPECT (! val.isValidIndex(0));
        BEAST_EXPECT (! val.isMember("key"));

        val = Json::Value (Json::objectValue);
        BEAST_EXPECT (val.type() == Json::objectValue);
        BEAST_EXPECT (val.size() == 0);
        static Json::StaticString const staticThree ("three");
        val[staticThree] = 3;
        val["two"] = 2;
        BEAST_EXPECT (val.size() == 2);
        BEAST_EXPECT (val.isValidIndex(1));
        BEAST_EXPECT (! val.isValidIndex(2));
        BEAST_EXPECT (val[staticThree] == 3);
        BEAST_EXPECT (val.isMember("two"));
        BEAST_EXPECT (val.isMember(staticThree));
        BEAST_EXPECT (! val.isMember("key"));
        {
            Json::Value const constVal = val;
            BEAST_EXPECT (constVal["two"] == 2);
            BEAST_EXPECT (constVal["four"].type() == Json::nullValue);
            BEAST_EXPECT (constVal[staticThree] == 3);
            BEAST_EXPECT (constVal.isMember("two"));
            BEAST_EXPECT (constVal.isMember(staticThree));
            BEAST_EXPECT (! constVal.isMember("key"));
            BEAST_EXPECT (val.get (std::string("two"), "backup") == 2);
            BEAST_EXPECT (val.get ("missing", "default2") == "default2");
        }

        val = Json::Value (Json::arrayValue);
        BEAST_EXPECT (val.type() == Json::arrayValue);
        BEAST_EXPECT (val.size() == 0);
        val[0u] = "zero";
        val[1u] = "one";
        BEAST_EXPECT (val.size() == 2);
        BEAST_EXPECT (val.isValidIndex(1));
        BEAST_EXPECT (! val.isValidIndex(2));
        BEAST_EXPECT (val[20u].type() == Json::nullValue);
        BEAST_EXPECT (! val.isMember("key"));
        {
            Json::Value const constVal = val;
            BEAST_EXPECT (constVal[0u] == "zero");
            BEAST_EXPECT (constVal[2u].type() == Json::nullValue);
            BEAST_EXPECT (! constVal.isMember("key"));
            BEAST_EXPECT (val.get (1u, "default0") == "one");
            BEAST_EXPECT (val.get (3u, "default1") == "default1");
        }
    }

    void
    test_removeMember()
    {
        Json::Value val;
        BEAST_EXPECT (
            val.removeMember (std::string("member")).type() == Json::nullValue);

        val = Json::Value (Json::objectValue);
        static Json::StaticString const staticThree ("three");
        val[staticThree] = 3;
        val["two"] = 2;
        BEAST_EXPECT (val.size() == 2);

        BEAST_EXPECT (
            val.removeMember (std::string("six")).type() == Json::nullValue);
        BEAST_EXPECT (val.size() == 2);

        BEAST_EXPECT (val.removeMember (staticThree) == 3);
        BEAST_EXPECT (val.size() == 1);

        BEAST_EXPECT (val.removeMember (staticThree).type() == Json::nullValue);
        BEAST_EXPECT (val.size() == 1);

        BEAST_EXPECT (val.removeMember (std::string ("two")) == 2);
        BEAST_EXPECT (val.size() == 0);

        BEAST_EXPECT (
            val.removeMember (std::string ("two")).type() == Json::nullValue);
        BEAST_EXPECT (val.size() == 0);
    }

    void
    test_iterator()
    {
        {
            // Iterating an array.
            Json::Value arr {Json::arrayValue};
            arr[0u] = "zero";
            arr[1u] = "one";
            arr[2u] = "two";
            arr[3u] = "three";

            Json::ValueIterator const b {arr.begin()};
            Json::ValueIterator const e {arr.end()};

            Json::ValueIterator i1 = b;
            Json::ValueIterator i2 = e;
            --i2;

            // key(), index(), and memberName() on an object iterator.
            BEAST_EXPECT (b != e);
            BEAST_EXPECT (! (b == e));
            BEAST_EXPECT (i1.key() == 0);
            BEAST_EXPECT (i2.key() == 3);
            BEAST_EXPECT (i1.index() == 0);
            BEAST_EXPECT (i2.index() == 3);
            BEAST_EXPECT (std::strcmp (i1.memberName(), "") == 0);
            BEAST_EXPECT (std::strcmp (i2.memberName(), "") == 0);

            // Pre and post increment and decrement.
            *i1++ = "0";
            BEAST_EXPECT (*i1 == "one");
            *i1 = "1";
            ++i1;

            *i2-- = "3";
            BEAST_EXPECT (*i2 == "two");
            BEAST_EXPECT (i1 == i2);
            *i2 = "2";
            BEAST_EXPECT (*i1 == "2");
        }
        {
            // Iterating a const object.
            Json::Value const obj {[] ()
                {
                    Json::Value obj {Json::objectValue};
                    obj["0"] = 0;
                    obj["1"] = 1;
                    obj["2"] = 2;
                    obj["3"] = 3;
                    return obj;
                }()
            };

            Json::ValueConstIterator i1 {obj.begin()};
            Json::ValueConstIterator i2 {obj.end()};
            --i2;

            // key(), index(), and memberName() on an object iterator.
            BEAST_EXPECT (i1 != i2);
            BEAST_EXPECT (! (i1 == i2));
            BEAST_EXPECT (i1.key() == "0");
            BEAST_EXPECT (i2.key() == "3");
            BEAST_EXPECT (i1.index() == -1);
            BEAST_EXPECT (i2.index() == -1);
            BEAST_EXPECT (std::strcmp (i1.memberName(), "0") == 0);
            BEAST_EXPECT (std::strcmp (i2.memberName(), "3") == 0);

            // Pre and post increment and decrement.
            BEAST_EXPECT (*i1++ == 0);
            BEAST_EXPECT (*i1   == 1);
            ++i1;

            BEAST_EXPECT (*i2-- == 3);
            BEAST_EXPECT (*i2   == 2);
            BEAST_EXPECT (i1    == i2);
            BEAST_EXPECT (*i1   == 2);
        }
        {
            // Iterating a non-const null object.
            Json::Value nul {};
            BEAST_EXPECT (nul.begin() == nul.end());
        }
        {
            // Iterating a const Int.
            Json::Value const i {-3};
            BEAST_EXPECT (i.begin() == i.end());
        }
    }

    void test_nest_limits ()
    {
        Json::Reader r;
        {
            auto nest = [](std::uint32_t depth)->std::string {
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
                BEAST_EXPECT(r.parse(json, j));
            }

            {
                // Exceed object nest limit
                auto json{nest(Json::Reader::nest_limit + 1)};
                Json::Value j;
                BEAST_EXPECT(!r.parse(json, j));
            }
        }

        auto nest = [](std::uint32_t depth)->std::string {
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
            BEAST_EXPECT(!r.parse(json, j));
        }
    }

    void
    test_leak()
    {
        // When run with the address sanitizer, this test confirms there is no
        // memory leak with the scenario below.
        Json::Value a;
        a[0u] = 1;
        a = std::move(a[0u]);
        pass();
    }

    void run () override
    {
        test_StaticString ();
        test_types ();
        test_compare ();
        test_bool ();
        test_bad_json ();
        test_edge_cases ();
        test_copy ();
        test_move ();
        test_comparisons ();
        test_compact ();
        test_conversions ();
        test_access ();
        test_removeMember ();
        test_iterator ();
        test_nest_limits ();
        test_leak ();
    }
};

BEAST_DEFINE_TESTSUITE(json_value, json, ripple);

} // ripple
