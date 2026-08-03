#include <xrpl/basics/Buffer.h>

#include <xrpl/basics/Slice.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

namespace xrpl::test {

static_assert(std::is_nothrow_move_constructible_v<Buffer>);
static_assert(std::is_nothrow_move_assignable_v<Buffer>);

struct BufferTest : public ::testing::Test
{
    static constexpr auto kData = std::to_array<std::uint8_t>(
        {0xa8, 0xa1, 0x38, 0x45, 0x23, 0xec, 0xe4, 0x23, 0x71, 0x6d, 0x2a,
         0x18, 0xb4, 0x70, 0xcb, 0xf5, 0xac, 0x2d, 0x89, 0x4d, 0x19, 0x9c,
         0xf0, 0x2c, 0x15, 0xd1, 0xf9, 0x9b, 0x66, 0xd2, 0x30, 0xd3});

    static constexpr std::size_t kHalf = kData.size() / 2;

    static bool
    sane(Buffer const& b)
    {
        if (b.empty())
            return b.data() == nullptr;

        return b.data() != nullptr;
    }

    /**
     * Check the state Buffer documents for a moved-from buffer: "the other buffer is reset", i.e.
     * empty and sane.
     *
     * Zeroing the size is not incidental tidiness. Moving the member unique_ptr nulls the data
     * pointer whether Buffer wants it or not, so a moved-from buffer that kept its old size would
     * lie about itself everywhere: alloc() would take its `n == size_` early-out and hand back a
     * null pointer while still reporting the old size, fill() would run std::fill_n over a null
     * pointer, and the Slice conversion would publish {nullptr, oldSize} to callers. A moved-from
     * Buffer has to be a usable empty Buffer rather than a landmine, which is why the tests below
     * assert this state instead of treating a moved-from buffer as untouchable.
     */
    static void
    checkEmptyAfterMove(Buffer const& buf)
    {
        EXPECT_TRUE(sane(buf));
        EXPECT_TRUE(buf.empty());
    }

    Buffer const emptyBuffer;
    Buffer const firstHalf{kData.data(), kHalf};
    Buffer const secondHalf{kData.data() + kHalf, kHalf};
    Buffer const whole{kData.data(), kData.size()};
};

TEST_F(BufferTest, default_constructed_is_empty)
{
    Buffer const b;

    EXPECT_TRUE(sane(b));
    EXPECT_TRUE(b.empty());
    EXPECT_EQ(b.data(), nullptr);
}

TEST_F(BufferTest, zero_sized_construction_is_empty)
{
    Buffer const b{0};

    EXPECT_TRUE(sane(b));
    EXPECT_TRUE(b.empty());
}

TEST_F(BufferTest, alloc_grows_an_empty_buffer)
{
    Buffer b{0};
    std::memcpy(b.alloc(kHalf), kData.data(), kHalf);

    EXPECT_TRUE(sane(b));
    EXPECT_FALSE(b.empty());
    EXPECT_EQ(b.size(), kHalf);
    EXPECT_EQ(b, firstHalf);
}

TEST_F(BufferTest, sized_construction_reserves_without_filling)
{
    Buffer b{kHalf};

    EXPECT_TRUE(sane(b));
    EXPECT_FALSE(b.empty());
    EXPECT_EQ(b.size(), kHalf);

    std::memcpy(b.data(), kData.data() + kHalf, kHalf);
    EXPECT_EQ(b, secondHalf);
}

TEST_F(BufferTest, construction_copies_raw_memory)
{
    Buffer const b{kData.data(), kData.size()};

    EXPECT_TRUE(sane(b));
    EXPECT_FALSE(b.empty());
    EXPECT_EQ(b.size(), kData.size());
    EXPECT_EQ(std::memcmp(b.data(), kData.data(), b.size()), 0);
}

TEST_F(BufferTest, equality_compares_contents)
{
    // Uses EXPECT_TRUE rather than EXPECT_EQ/EXPECT_NE because the operators are what is under test
    // here.
    EXPECT_TRUE(emptyBuffer == emptyBuffer);
    EXPECT_TRUE(firstHalf == firstHalf);

    EXPECT_TRUE(emptyBuffer != firstHalf);
    EXPECT_TRUE(firstHalf != secondHalf);
    EXPECT_TRUE(secondHalf != whole);
}

TEST_F(BufferTest, copy_construction)
{
    Buffer const fromEmpty{emptyBuffer};
    EXPECT_TRUE(sane(fromEmpty));
    EXPECT_EQ(fromEmpty, emptyBuffer);

    Buffer const fromNonEmpty{firstHalf};
    EXPECT_TRUE(sane(fromNonEmpty));
    EXPECT_EQ(fromNonEmpty, firstHalf);
}

TEST_F(BufferTest, copy_assignment)
{
    Buffer b{emptyBuffer};

    // empty <- non-empty
    b = secondHalf;
    EXPECT_TRUE(sane(b));
    EXPECT_EQ(b, secondHalf);

    // non-empty <- non-empty of a different size
    b = whole;
    EXPECT_TRUE(sane(b));
    EXPECT_EQ(b, whole);

    // non-empty <- empty
    b = emptyBuffer;
    EXPECT_TRUE(sane(b));
    EXPECT_EQ(b, emptyBuffer);
}

TEST_F(BufferTest, self_assignment_preserves_contents)
{
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-assign-overloaded"
#endif

    Buffer emptyCopy{emptyBuffer};
    emptyCopy = emptyCopy;
    EXPECT_TRUE(sane(emptyCopy));
    EXPECT_EQ(emptyCopy, emptyBuffer);

    Buffer wholeCopy{whole};
    wholeCopy = wholeCopy;
    EXPECT_TRUE(sane(wholeCopy));
    EXPECT_EQ(wholeCopy, whole);

#ifdef __clang__
#pragma clang diagnostic pop
#endif
}

TEST_F(BufferTest, move_construct_from_empty)
{
    Buffer source;
    Buffer const moved{std::move(source)};

    checkEmptyAfterMove(source);
    EXPECT_TRUE(sane(moved));
    EXPECT_TRUE(moved.empty());
}

TEST_F(BufferTest, move_construct_from_non_empty)
{
    Buffer source{firstHalf};
    Buffer const moved{std::move(source)};

    checkEmptyAfterMove(source);
    EXPECT_TRUE(sane(moved));
    EXPECT_EQ(moved, firstHalf);
}

TEST_F(BufferTest, move_assign_empty_to_empty)
{
    Buffer target;
    Buffer source;

    target = std::move(source);

    EXPECT_TRUE(sane(target));
    EXPECT_TRUE(target.empty());
    checkEmptyAfterMove(source);
}

TEST_F(BufferTest, move_assign_non_empty_to_empty)
{
    Buffer target;
    Buffer source{firstHalf};

    target = std::move(source);

    EXPECT_TRUE(sane(target));
    EXPECT_EQ(target, firstHalf);
    checkEmptyAfterMove(source);
}

TEST_F(BufferTest, move_assign_empty_to_non_empty)
{
    Buffer target{firstHalf};
    Buffer source;

    target = std::move(source);

    EXPECT_TRUE(sane(target));
    EXPECT_TRUE(target.empty());
    checkEmptyAfterMove(source);
}

TEST_F(BufferTest, move_assign_non_empty_to_non_empty)
{
    Buffer target{firstHalf};
    Buffer sameSize{secondHalf};
    Buffer largerSize{whole};

    target = std::move(sameSize);
    EXPECT_TRUE(sane(target));
    EXPECT_EQ(target, secondHalf);
    checkEmptyAfterMove(sameSize);

    target = std::move(largerSize);
    EXPECT_TRUE(sane(target));
    EXPECT_EQ(target, whole);
    checkEmptyAfterMove(largerSize);
}

TEST_F(BufferTest, construction_from_slice)
{
    Buffer const fromEmpty{static_cast<Slice>(emptyBuffer)};
    EXPECT_TRUE(sane(fromEmpty));
    EXPECT_EQ(fromEmpty, emptyBuffer);

    Buffer const fromNonEmpty{static_cast<Slice>(whole)};
    EXPECT_TRUE(sane(fromNonEmpty));
    EXPECT_EQ(fromNonEmpty, whole);
}

TEST_F(BufferTest, assignment_from_slice)
{
    Buffer b;

    // empty <- empty slice
    b = static_cast<Slice>(emptyBuffer);
    EXPECT_TRUE(sane(b));
    EXPECT_EQ(b, emptyBuffer);

    // empty <- non-empty slice
    b = static_cast<Slice>(firstHalf);
    EXPECT_TRUE(sane(b));
    EXPECT_EQ(b, firstHalf);

    // non-empty <- non-empty slice
    b = static_cast<Slice>(secondHalf);
    EXPECT_TRUE(sane(b));
    EXPECT_EQ(b, secondHalf);

    // non-empty <- empty slice
    b = static_cast<Slice>(emptyBuffer);
    EXPECT_TRUE(sane(b));
    EXPECT_EQ(b, emptyBuffer);
}

TEST_F(BufferTest, resize_allocates_and_clear_releases)
{
    auto check = [](Buffer const& original, std::size_t size) {
        SCOPED_TRACE(::testing::Message() << "size: " << size);

        Buffer b{original};

        // Resizing to zero is equivalent to clearing.
        b(size);
        EXPECT_TRUE(sane(b));
        EXPECT_EQ(b.size(), size);
        EXPECT_EQ(b.data() == nullptr, size == 0);

        b(size + 1);
        EXPECT_TRUE(sane(b));
        EXPECT_EQ(b.size(), size + 1);
        EXPECT_NE(b.data(), nullptr);

        b.clear();
        EXPECT_TRUE(sane(b));
        EXPECT_TRUE(b.empty());
        EXPECT_EQ(b.data(), nullptr);

        // clear() is idempotent.
        b.clear();
        EXPECT_TRUE(sane(b));
        EXPECT_TRUE(b.empty());
        EXPECT_EQ(b.data(), nullptr);
    };

    for (auto size = 0uz; size < kHalf; ++size)
    {
        check(emptyBuffer, size);
        check(firstHalf, size);
    }
}

TEST_F(BufferTest, fill_sets_every_byte)
{
    Buffer b{4};
    b.fill(0xab);

    EXPECT_EQ(b.size(), 4);
    for (auto const byte : Slice{b})
        EXPECT_EQ(byte, 0xab);
}

TEST_F(BufferTest, fill_overwrites_and_keeps_size)
{
    Buffer b{4};
    b.fill(0xab);
    b.fill(0x00);

    EXPECT_EQ(b.size(), 4);
    for (auto const byte : Slice{b})
        EXPECT_EQ(byte, 0x00);
}

TEST_F(BufferTest, fill_on_empty_buffer_is_a_noop)
{
    Buffer empty;
    empty.fill(0xff);

    EXPECT_TRUE(empty.empty());
    EXPECT_EQ(empty.data(), nullptr);
}

}  // namespace xrpl::test
