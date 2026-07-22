#include <xrpl/nodestore/detail/varint.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using namespace xrpl::node_store;

TEST(varint, encode_decode)
{
    std::vector<std::size_t> const kVALUES = {
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
        0xffffffffffffffffUL};

    for (auto const v : kVALUES)
    {
        SCOPED_TRACE("value=" + std::to_string(v));
        std::array<std::uint8_t, varint_traits<std::size_t>::kMax> vi{};
        auto const n0 = writeVarint(vi.data(), v);
        EXPECT_GT(n0, 0u) << "write error";
        EXPECT_EQ(n0, sizeVarint(v)) << "size error";
        std::size_t v1 = 0;
        auto const n1 = readVarint(vi.data(), n0, v1);
        EXPECT_EQ(n1, n0) << "read error";
        EXPECT_EQ(v1, v) << "wrong value";
    }
}
