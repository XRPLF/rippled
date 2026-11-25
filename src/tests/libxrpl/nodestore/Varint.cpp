#include <xrpl/nodestore/detail/Varint.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

using namespace xrpl::node_store;

TEST(Varint, encode_decode)
{
    static constexpr auto kValues = std::to_array<std::size_t>({
        0,
        1,
        2,
        126,
        127,
        128,
        253,
        254,
        255,
        16127,
        16128,
        16129,
        0xff,
        0xffff,
        0xffffffff,
        0xffffffffffffUL,
        std::numeric_limits<std::size_t>::max(),
    });

    for (auto const value : kValues)
    {
        std::array<std::uint8_t, VarintTraits<std::size_t>::kMax> buffer{};
        auto const bytesWritten = writeVarint(buffer.data(), value);
        EXPECT_GT(bytesWritten, 0u);
        EXPECT_EQ(bytesWritten, sizeVarint(value));

        std::size_t decoded = 0;
        auto const bytesRead = readVarint(buffer.data(), bytesWritten, decoded);
        EXPECT_EQ(bytesRead, bytesWritten);
        EXPECT_EQ(value, decoded);
    }
}
