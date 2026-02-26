#include <xrpl/json/Output.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_writer.h>

#include <gtest/gtest.h>

#include <string>

using namespace xrpl;
using namespace Json;

static void
checkOutput(std::string const& valueDesc)
{
    std::string output;
    Json::Value value;
    ASSERT_TRUE(Json::Reader().parse(valueDesc, value));
    auto out = stringOutput(output);
    outputJson(value, out);

    auto expected = Json::FastWriter().write(value);
    EXPECT_EQ(output, expected);
    EXPECT_EQ(output, valueDesc);
    EXPECT_EQ(output, jsonAsString(value));
}

TEST(JsonOutput, output_cases)
{
    checkOutput("{}");
    checkOutput("[]");
    checkOutput(R"([23,4.25,true,null,"string"])");
    checkOutput(R"({"hello":"world"})");
    checkOutput("[{}]");
    checkOutput("[[]]");
    checkOutput(R"({"array":[{"12":23},{},null,false,0.5]})");
}
