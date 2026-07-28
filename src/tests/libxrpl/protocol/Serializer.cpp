#include <xrpl/protocol/Serializer.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>

using namespace xrpl;

TEST(Serializer, add32_roundtrip)
{
    static constexpr auto kValues = std::to_array<std::int32_t>({
        std::numeric_limits<std::int32_t>::min(),
        -1,
        0,
        1,
        std::numeric_limits<std::int32_t>::max(),
    });

    for (std::int32_t const value : kValues)
    {
        Serializer s;
        s.add32(value);
        EXPECT_EQ(s.size(), 4);
        SerialIter sit(s.slice());
        EXPECT_EQ(sit.geti32(), value);
    }
}

TEST(Serializer, add64_roundtrip)
{
    static constexpr auto kValues = std::to_array<std::int64_t>({
        std::numeric_limits<std::int64_t>::min(),
        -1,
        0,
        1,
        std::numeric_limits<std::int64_t>::max(),
    });

    for (std::int64_t const value : kValues)
    {
        Serializer s;
        s.add64(value);
        EXPECT_EQ(s.size(), 8);
        SerialIter sit(s.slice());
        EXPECT_EQ(sit.geti64(), value);
    }
}
