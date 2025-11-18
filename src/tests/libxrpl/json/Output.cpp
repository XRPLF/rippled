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
