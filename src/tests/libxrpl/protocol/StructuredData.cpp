#include <xrpl/protocol/StructuredData.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/Protocol.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

using namespace xrpl;

namespace {

std::vector<std::uint8_t>
bytes(std::initializer_list<std::uint8_t> il)
{
    return {il};
}

Slice
slice(std::vector<std::uint8_t> const& v)
{
    return {v.data(), v.size()};
}

std::vector<std::uint8_t>
operator+(std::vector<std::uint8_t> a, std::vector<std::uint8_t> const& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

std::vector<std::uint8_t>
utf8(std::string const& s)
{
    return {s.begin(), s.end()};
}

}  // namespace

TEST(StructuredData, wellFormedSchemas)
{
    // Every fixed-width code, str/bin, and combinations
    for (std::uint8_t code = 0x01; code <= 0x0F; ++code)
        EXPECT_TRUE(isWellFormedSchema(slice(bytes({code})))) << "code " << int(code);

    // The spec's worked example: str, u16, u32, u64
    EXPECT_TRUE(isWellFormedSchema(slice(bytes({0x0E, 0x03, 0x04, 0x05}))));

    // Arrays and tuples
    EXPECT_TRUE(isWellFormedSchema(slice(bytes({0x20, 0x05}))));              // u64[]
    EXPECT_TRUE(isWellFormedSchema(slice(bytes({0x30, 0x09, 0x05, 0x31}))));  // (account,u64)
    EXPECT_TRUE(
        isWellFormedSchema(slice(bytes({0x20, 0x30, 0x09, 0x05, 0x31}))));  // (account,u64)[]
    EXPECT_TRUE(isWellFormedSchema(slice(bytes({0x20, 0x20, 0x02}))));      // u8[][]
}

TEST(StructuredData, malformedSchemas)
{
    EXPECT_FALSE(isWellFormedSchema(slice(bytes({}))));            // empty
    EXPECT_FALSE(isWellFormedSchema(slice(bytes({0x00}))));        // reserved
    EXPECT_FALSE(isWellFormedSchema(slice(bytes({0x10}))));        // reserved
    EXPECT_FALSE(isWellFormedSchema(slice(bytes({0xFF}))));        // reserved
    EXPECT_FALSE(isWellFormedSchema(slice(bytes({0x20}))));        // array, no element
    EXPECT_FALSE(isWellFormedSchema(slice(bytes({0x30, 0x05}))));  // unterminated tuple
    EXPECT_FALSE(isWellFormedSchema(slice(bytes({0x31}))));        // close without open
    EXPECT_FALSE(isWellFormedSchema(slice(bytes({0x05, 0x00}))));  // trailing reserved

    // Over the size cap
    std::vector<std::uint8_t> big(kMaxSchemaLength + 1, 0x02);
    EXPECT_FALSE(isWellFormedSchema(slice(big)));
    std::vector<std::uint8_t> atCap(kMaxSchemaLength, 0x02);
    EXPECT_TRUE(isWellFormedSchema(slice(atCap)));

    // Nesting: depth 8 is fine, depth 9 is not
    std::vector<std::uint8_t> depth8;
    for (int i = 0; i < 8; ++i)
        depth8.push_back(0x20);
    depth8.push_back(0x02);
    EXPECT_TRUE(isWellFormedSchema(slice(depth8)));
    std::vector<std::uint8_t> depth9;
    for (int i = 0; i < 9; ++i)
        depth9.push_back(0x20);
    depth9.push_back(0x02);
    EXPECT_FALSE(isWellFormedSchema(slice(depth9)));
}

TEST(StructuredData, fixedWidthData)
{
    auto const schema = bytes({0x02, 0x03, 0x05});  // u8, u16, u64
    auto const data = bytes({0xAA}) + bytes({0x01, 0xA9}) +
        bytes({0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x42, 0x40});
    EXPECT_TRUE(dataMatchesSchema(slice(schema), slice(data)));

    // Truncated and trailing both fail
    auto truncated = data;
    truncated.pop_back();
    EXPECT_FALSE(dataMatchesSchema(slice(schema), slice(truncated)));
    auto trailing = data;
    trailing.push_back(0x00);
    EXPECT_FALSE(dataMatchesSchema(slice(schema), slice(trailing)));

    // Empty data never matches a schema with fields
    EXPECT_FALSE(dataMatchesSchema(slice(schema), slice(bytes({}))));
}

TEST(StructuredData, boolValues)
{
    auto const schema = bytes({0x01});
    EXPECT_TRUE(dataMatchesSchema(slice(schema), slice(bytes({0x00}))));
    EXPECT_TRUE(dataMatchesSchema(slice(schema), slice(bytes({0x01}))));
    EXPECT_FALSE(dataMatchesSchema(slice(schema), slice(bytes({0x02}))));
    EXPECT_FALSE(dataMatchesSchema(slice(schema), slice(bytes({0xFF}))));
}

TEST(StructuredData, varLengthData)
{
    auto const schema = bytes({0x0E});  // str

    // Empty string: VL prefix 0, no content
    EXPECT_TRUE(dataMatchesSchema(slice(schema), slice(bytes({0x00}))));

    // Short string
    auto const hello = bytes({0x05}) + utf8("hello");
    EXPECT_TRUE(dataMatchesSchema(slice(schema), slice(hello)));

    // Multibyte UTF-8: prefix counts bytes, not characters
    auto const accents = utf8("\xC3\xA9\xC3\xA9");  // "éé", 4 bytes
    EXPECT_TRUE(dataMatchesSchema(slice(schema), slice(bytes({0x04}) + accents)));
    EXPECT_FALSE(dataMatchesSchema(slice(schema), slice(bytes({0x02}) + accents)));

    // VL prefix longer than remaining input
    EXPECT_FALSE(dataMatchesSchema(slice(schema), slice(bytes({0x05}) + utf8("hi"))));

    // Two-byte VL prefix: 300 bytes = 193 + (b0-193)*256 + b1 with b0=193, b1=107
    std::vector<std::uint8_t> payload(300, 0x41);
    EXPECT_TRUE(dataMatchesSchema(slice(schema), slice(bytes({193, 107}) + payload)));

    // 0xFF is not a valid VL prefix
    EXPECT_FALSE(dataMatchesSchema(slice(schema), slice(bytes({0xFF, 0x00}))));
}

TEST(StructuredData, arraysAndTuples)
{
    // u16[] with 0 and 3 elements
    auto const arr = bytes({0x20, 0x03});
    EXPECT_TRUE(dataMatchesSchema(slice(arr), slice(bytes({0x00}))));
    EXPECT_TRUE(
        dataMatchesSchema(slice(arr), slice(bytes({0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03}))));
    // Count says 3, only 2 present
    EXPECT_FALSE(dataMatchesSchema(slice(arr), slice(bytes({0x03, 0x00, 0x01, 0x00, 0x02}))));
    // Missing count byte entirely
    EXPECT_FALSE(dataMatchesSchema(slice(arr), slice(bytes({}))));

    // (u8,str)[]: two records, second with an empty string
    auto const tupArr = bytes({0x20, 0x30, 0x02, 0x0E, 0x31});
    auto const recs = bytes({0x02}) + bytes({0x07, 0x02}) + utf8("ab") + bytes({0x09, 0x00});
    EXPECT_TRUE(dataMatchesSchema(slice(tupArr), slice(recs)));

    // Tuple fields pack flat, no framing in the data
    auto const tup = bytes({0x30, 0x02, 0x02, 0x31});
    EXPECT_TRUE(dataMatchesSchema(slice(tup), slice(bytes({0x01, 0x02}))));
    EXPECT_FALSE(dataMatchesSchema(slice(tup), slice(bytes({0x01}))));
}

TEST(StructuredData, specWorkedExample)
{
    // cusip:str, coupon:u16, maturity:u32, face:u64
    auto const schema = bytes({0x0E, 0x03, 0x04, 0x05});
    auto const data = bytes({0x09}) + utf8("912828YK0") + bytes({0x01, 0xA9}) +
        bytes({0x71, 0x3F, 0xB3, 0x00}) + bytes({0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x42, 0x40});
    EXPECT_EQ(data.size(), 24u);
    EXPECT_TRUE(isWellFormedSchema(slice(schema)));
    EXPECT_TRUE(dataMatchesSchema(slice(schema), slice(data)));
}

TEST(StructuredData, malformedSchemaNeverMatches)
{
    // dataMatchesSchema re-validates the schema rather than misreading data
    EXPECT_FALSE(dataMatchesSchema(slice(bytes({})), slice(bytes({}))));
    EXPECT_FALSE(dataMatchesSchema(slice(bytes({0x00})), slice(bytes({0x00}))));
    EXPECT_FALSE(dataMatchesSchema(slice(bytes({0x20})), slice(bytes({0x00}))));
    EXPECT_FALSE(dataMatchesSchema(slice(bytes({0x30, 0x02})), slice(bytes({0x01}))));
}
