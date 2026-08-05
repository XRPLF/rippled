#include <xrpl/basics/Slice.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>

using namespace xrpl;

static std::uint8_t const kData[] = {
    0xa8, 0xa1, 0x38, 0x45, 0x23, 0xec, 0xe4, 0x23, 0x71, 0x6d, 0x2a, 0x18, 0xb4, 0x70, 0xcb, 0xf5,
    0xac, 0x2d, 0x89, 0x4d, 0x19, 0x9c, 0xf0, 0x2c, 0x15, 0xd1, 0xf9, 0x9b, 0x66, 0xd2, 0x30, 0xd3};

TEST(Slice, equality_and_inequality)
{
    Slice const s0{};

    EXPECT_EQ(s0.size(), 0);
    EXPECT_EQ(s0.data(), nullptr);
    EXPECT_EQ(s0, s0);

    // Test slices of equal and unequal size pointing to same data:
    for (std::size_t i = 0; i != sizeof(kData); ++i)
    {
        Slice const s1{kData, i};

        EXPECT_EQ(s1.size(), i);
        EXPECT_NE(s1.data(), nullptr);

        if (i == 0)
        {
            EXPECT_EQ(s1, s0);
        }
        else
        {
            EXPECT_NE(s1, s0);
        }

        for (std::size_t j = 0; j != sizeof(kData); ++j)
        {
            Slice const s2{kData, j};

            if (i == j)
            {
                EXPECT_EQ(s1, s2);
            }
            else
            {
                EXPECT_NE(s1, s2);
            }
        }
    }

    // Test slices of equal size but pointing to different data:
    std::array<std::uint8_t, sizeof(kData)> a{};
    std::array<std::uint8_t, sizeof(kData)> b{};

    for (std::size_t i = 0; i != sizeof(kData); ++i)
        a[i] = b[i] = kData[i];

    EXPECT_EQ(makeSlice(a), makeSlice(b));
    b[7]++;
    EXPECT_NE(makeSlice(a), makeSlice(b));
    a[7]++;
    EXPECT_EQ(makeSlice(a), makeSlice(b));
}

TEST(Slice, indexing)
{
    Slice const s{kData, sizeof(kData)};

    for (std::size_t i = 0; i != sizeof(kData); ++i)
        EXPECT_EQ(s[i], kData[i]);
}

TEST(Slice, advancing)
{
    for (std::size_t i = 0; i < sizeof(kData); ++i)
    {
        for (std::size_t j = 0; i + j < sizeof(kData); ++j)
        {
            Slice s(kData + i, sizeof(kData) - i);
            s += j;

            EXPECT_EQ(s.data(), kData + i + j);
            EXPECT_EQ(s.size(), sizeof(kData) - i - j);
        }
    }
}
