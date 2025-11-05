#include <xrpl/basics/Slice.h>

#include <doctest/doctest.h>

#include <array>
#include <cstdint>

using namespace ripple;

static std::uint8_t const data[] = {
    0xa8, 0xa1, 0x38, 0x45, 0x23, 0xec, 0xe4, 0x23, 0x71, 0x6d, 0x2a,
    0x18, 0xb4, 0x70, 0xcb, 0xf5, 0xac, 0x2d, 0x89, 0x4d, 0x19, 0x9c,
    0xf0, 0x2c, 0x15, 0xd1, 0xf9, 0x9b, 0x66, 0xd2, 0x30, 0xd3};

TEST_SUITE_BEGIN("Slice");

TEST_CASE("equality & inequality")
{
    Slice const s0{};

    CHECK(s0.size() == 0);
    CHECK(s0.data() == nullptr);
    CHECK(s0 == s0);

    // Test slices of equal and unequal size pointing to same data:
    for (std::size_t i = 0; i != sizeof(data); ++i)
    {
        Slice const s1{data, i};

        CHECK(s1.size() == i);
        CHECK(s1.data() != nullptr);

        if (i == 0)
            CHECK(s1 == s0);
        else
            CHECK(s1 != s0);

        for (std::size_t j = 0; j != sizeof(data); ++j)
        {
            Slice const s2{data, j};

            if (i == j)
                CHECK(s1 == s2);
            else
                CHECK(s1 != s2);
        }
    }

    // Test slices of equal size but pointing to different data:
    std::array<std::uint8_t, sizeof(data)> a;
    std::array<std::uint8_t, sizeof(data)> b;

    for (std::size_t i = 0; i != sizeof(data); ++i)
        a[i] = b[i] = data[i];

    CHECK(makeSlice(a) == makeSlice(b));
    b[7]++;
    CHECK(makeSlice(a) != makeSlice(b));
    a[7]++;
    CHECK(makeSlice(a) == makeSlice(b));
}

TEST_CASE("indexing")
{
    Slice const s{data, sizeof(data)};

    for (std::size_t i = 0; i != sizeof(data); ++i)
        CHECK(s[i] == data[i]);
}

TEST_CASE("advancing")
{
    for (std::size_t i = 0; i < sizeof(data); ++i)
    {
        for (std::size_t j = 0; i + j < sizeof(data); ++j)
        {
            Slice s(data + i, sizeof(data) - i);
            s += j;

            CHECK(s.data() == data + i + j);
            CHECK(s.size() == sizeof(data) - i - j);
        }
    }
}

TEST_SUITE_END();
