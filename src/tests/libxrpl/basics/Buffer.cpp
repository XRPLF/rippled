#include <xrpl/basics/Buffer.h>

#include <xrpl/basics/Slice.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

namespace xrpl::test {

struct BufferTest : public ::testing::Test
{
    static bool
    sane(Buffer const& b)
    {
        if (b.empty())
            return b.data() == nullptr;

        return b.data() != nullptr;
    }
};

TEST_F(BufferTest, buffer)
{
    std::uint8_t const data[] = {0xa8, 0xa1, 0x38, 0x45, 0x23, 0xec, 0xe4, 0x23, 0x71, 0x6d, 0x2a,
                                 0x18, 0xb4, 0x70, 0xcb, 0xf5, 0xac, 0x2d, 0x89, 0x4d, 0x19, 0x9c,
                                 0xf0, 0x2c, 0x15, 0xd1, 0xf9, 0x9b, 0x66, 0xd2, 0x30, 0xd3};

    Buffer const b0;
    EXPECT_TRUE(sane(b0));
    EXPECT_TRUE(b0.empty());

    Buffer b1{0};
    EXPECT_TRUE(sane(b1));
    EXPECT_TRUE(b1.empty());
    std::memcpy(b1.alloc(16), data, 16);
    EXPECT_TRUE(sane(b1));
    EXPECT_FALSE(b1.empty());
    EXPECT_EQ(b1.size(), 16);

    Buffer b2{b1.size()};
    EXPECT_TRUE(sane(b2));
    EXPECT_FALSE(b2.empty());
    EXPECT_EQ(b2.size(), b1.size());
    std::memcpy(b2.data(), data + 16, 16);

    Buffer b3{data, sizeof(data)};
    EXPECT_TRUE(sane(b3));
    EXPECT_FALSE(b3.empty());
    EXPECT_EQ(b3.size(), sizeof(data));
    EXPECT_EQ(std::memcmp(b3.data(), data, b3.size()), 0);

    // Check equality and inequality comparisons.
    // For code readability, we want to use general
    // EXPECT_TRUE instead of specific EXPECT_EQ etc.
    EXPECT_TRUE(b0 == b0);
    EXPECT_TRUE(b0 != b1);
    EXPECT_TRUE(b1 == b1);
    EXPECT_TRUE(b1 != b2);
    EXPECT_TRUE(b2 != b3);

    // Check copy constructors and copy assignments:
    {
        Buffer x{b0};
        EXPECT_EQ(x, b0);
        EXPECT_TRUE(sane(x));
        Buffer y{b1};
        EXPECT_EQ(y, b1);
        EXPECT_TRUE(sane(y));
        x = b2;
        EXPECT_EQ(x, b2);
        EXPECT_TRUE(sane(x));
        x = y;
        EXPECT_EQ(x, y);
        EXPECT_TRUE(sane(x));
        y = b3;
        EXPECT_EQ(y, b3);
        EXPECT_TRUE(sane(y));
        x = b0;
        EXPECT_EQ(x, b0);
        EXPECT_TRUE(sane(x));
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-assign-overloaded"
#endif

        x = x;
        EXPECT_EQ(x, b0);
        EXPECT_TRUE(sane(x));
        y = y;
        EXPECT_EQ(y, b3);
        EXPECT_TRUE(sane(y));

#ifdef __clang__
#pragma clang diagnostic pop
#endif
    }

    // Check move constructor & move assignments:
    {
        static_assert(std::is_nothrow_move_constructible_v<Buffer>);
        static_assert(std::is_nothrow_move_assignable_v<Buffer>);

        {  // Move-construct from empty buf
            Buffer x;
            Buffer const y{std::move(x)};
            EXPECT_TRUE(sane(x));    // NOLINT(bugprone-use-after-move)
            EXPECT_TRUE(x.empty());  // NOLINT(bugprone-use-after-move)
            EXPECT_TRUE(sane(y));
            EXPECT_TRUE(y.empty());
            EXPECT_EQ(x, y);  // NOLINT(bugprone-use-after-move)
        }

        {  // Move-construct from non-empty buf
            Buffer x{b1};
            Buffer const y{std::move(x)};
            EXPECT_TRUE(sane(x));    // NOLINT(bugprone-use-after-move)
            EXPECT_TRUE(x.empty());  // NOLINT(bugprone-use-after-move)
            EXPECT_TRUE(sane(y));
            EXPECT_EQ(y, b1);
        }

        {  // Move assign empty buf to empty buf
            Buffer x;
            Buffer y;

            x = std::move(y);
            EXPECT_TRUE(sane(x));
            EXPECT_TRUE(x.empty());
            EXPECT_TRUE(sane(y));    // NOLINT(bugprone-use-after-move)
            EXPECT_TRUE(y.empty());  // NOLINT(bugprone-use-after-move)
        }

        {  // Move assign non-empty buf to empty buf
            Buffer x;
            Buffer y{b1};

            x = std::move(y);
            EXPECT_TRUE(sane(x));
            EXPECT_EQ(x, b1);
            EXPECT_TRUE(sane(y));    // NOLINT(bugprone-use-after-move)
            EXPECT_TRUE(y.empty());  // NOLINT(bugprone-use-after-move)
        }

        {  // Move assign empty buf to non-empty buf
            Buffer x{b1};
            Buffer y;

            x = std::move(y);
            EXPECT_TRUE(sane(x));
            EXPECT_TRUE(x.empty());
            EXPECT_TRUE(sane(y));    // NOLINT(bugprone-use-after-move)
            EXPECT_TRUE(y.empty());  // NOLINT(bugprone-use-after-move)
        }

        {  // Move assign non-empty buf to non-empty buf
            Buffer x{b1};
            Buffer y{b2};
            Buffer z{b3};

            x = std::move(y);
            EXPECT_TRUE(sane(x));
            EXPECT_FALSE(x.empty());
            EXPECT_TRUE(sane(y));    // NOLINT(bugprone-use-after-move)
            EXPECT_TRUE(y.empty());  // NOLINT(bugprone-use-after-move)

            x = std::move(z);
            EXPECT_TRUE(sane(x));
            EXPECT_FALSE(x.empty());
            EXPECT_TRUE(sane(z));    // NOLINT(bugprone-use-after-move)
            EXPECT_TRUE(z.empty());  // NOLINT(bugprone-use-after-move)
        }
    }

    {
        Buffer w{static_cast<Slice>(b0)};
        EXPECT_TRUE(sane(w));
        EXPECT_EQ(w, b0);

        Buffer x{static_cast<Slice>(b1)};
        EXPECT_TRUE(sane(x));
        EXPECT_EQ(x, b1);

        Buffer y{static_cast<Slice>(b2)};
        EXPECT_TRUE(sane(y));
        EXPECT_EQ(y, b2);

        Buffer z{static_cast<Slice>(b3)};
        EXPECT_TRUE(sane(z));
        EXPECT_EQ(z, b3);

        // Assign empty slice to empty buffer
        w = static_cast<Slice>(b0);
        EXPECT_TRUE(sane(w));
        EXPECT_EQ(w, b0);

        // Assign non-empty slice to empty buffer
        w = static_cast<Slice>(b1);
        EXPECT_TRUE(sane(w));
        EXPECT_EQ(w, b1);

        // Assign non-empty slice to non-empty buffer
        x = static_cast<Slice>(b2);
        EXPECT_TRUE(sane(x));
        EXPECT_EQ(x, b2);

        // Assign non-empty slice to non-empty buffer
        y = static_cast<Slice>(z);
        EXPECT_TRUE(sane(y));
        EXPECT_EQ(y, z);

        // Assign empty slice to non-empty buffer:
        z = static_cast<Slice>(b0);
        EXPECT_TRUE(sane(z));
        EXPECT_EQ(z, b0);
    }

    {
        auto test = [](Buffer const& b, std::size_t i) {
            Buffer x{b};

            // Try to allocate some number of bytes, possibly
            // zero (which means clear) and sanity check
            x(i);
            EXPECT_TRUE(sane(x));
            EXPECT_EQ(x.size(), i);
            EXPECT_EQ((x.data() == nullptr), (i == 0));

            // Try to allocate some more data (always non-zero)
            x(i + 1);
            EXPECT_TRUE(sane(x));
            EXPECT_EQ(x.size(), i + 1);
            EXPECT_NE(x.data(), nullptr);

            // Try to clear:
            x.clear();
            EXPECT_TRUE(sane(x));
            EXPECT_TRUE(x.empty());
            EXPECT_EQ(x.data(), nullptr);

            // Try to clear again:
            x.clear();
            EXPECT_TRUE(sane(x));
            EXPECT_TRUE(x.empty());
            EXPECT_EQ(x.data(), nullptr);
        };

        for (std::size_t i = 0; i < 16; ++i)
        {
            test(b0, i);
            test(b1, i);
        }
    }
}

}  // namespace xrpl::test
