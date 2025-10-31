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

#include <xrpl/json/Output.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_writer.h>

#include <doctest/doctest.h>

#include <string>

using namespace ripple;
using namespace Json;

TEST_SUITE_BEGIN("JsonOutput");

static void
checkOutput(std::string const& valueDesc)
{
    std::string output;
    Json::Value value;
    REQUIRE(Json::Reader().parse(valueDesc, value));
    auto out = stringOutput(output);
    outputJson(value, out);

    auto expected = Json::FastWriter().write(value);
    CHECK(output == expected);
    CHECK(output == valueDesc);
    CHECK(output == jsonAsString(value));
}

TEST_CASE("output cases")
{
    checkOutput("{}");
    checkOutput("[]");
    checkOutput(R"([23,4.25,true,null,"string"])");
    checkOutput(R"({"hello":"world"})");
    checkOutput("[{}]");
    checkOutput("[[]]");
    checkOutput(R"({"array":[{"12":23},{},null,false,0.5]})");
}

TEST_SUITE_END();
