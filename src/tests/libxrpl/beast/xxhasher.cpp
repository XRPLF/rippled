#include <xrpl/beast/hash/xxhasher.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace beast {
namespace {

constexpr std::string_view kInput{"Hello, xxHash!"};
constexpr std::uint32_t kSeed{102};
constexpr std::uint32_t kOtherSeed{103};

// The multiple-update and one-update cases hash the same kRepeatCount copies of
// kInput, so their expected digests must agree.
constexpr std::size_t kRepeatCount{100};
constexpr std::size_t kBigObjectRepeat{20};

// Returns text repeated the given number of times.
//
// Used by the cases that hash a large object in one update, so that their
// expected digest can be compared against the cases that feed the same bytes
// in as many small updates.
std::string
repeat(std::string_view text, std::size_t times)
{
    std::string out;
    out.reserve(text.size() * times);
    for (std::size_t i = 0; i < times; ++i)
        out += text;
    return out;
}

}  // namespace

TEST(XXHasher, without_seed)
{
    Xxhasher hasher{};
    hasher(kInput.data(), kInput.size());

    EXPECT_EQ(static_cast<Xxhasher::result_type>(hasher), 16042857369214894119ULL);
}

TEST(XXHasher, with_seed)
{
    Xxhasher hasher{kSeed};
    hasher(kInput.data(), kInput.size());

    EXPECT_EQ(static_cast<Xxhasher::result_type>(hasher), 14440132435660934800ULL);
}

TEST(XXHasher, with_two_seeds)
{
    Xxhasher hasher{kSeed, kOtherSeed};
    hasher(kInput.data(), kInput.size());

    // The second seed is ignored, so this matches the single-seed result.
    EXPECT_EQ(static_cast<Xxhasher::result_type>(hasher), 14440132435660934800ULL);
}

// Feeds the bytes in as kRepeatCount small updates, never materializing the full object.
// big_object_with_one_update_without_seed below hashes the same bytes in a single
// update and must agree, which is what exercises xxHash's internal buffering.
TEST(XXHasher, big_object_with_multiple_small_updates_without_seed)
{
    Xxhasher hasher{};
    for (std::size_t i = 0; i < kRepeatCount; ++i)
        hasher(kInput.data(), kInput.size());

    EXPECT_EQ(static_cast<Xxhasher::result_type>(hasher), 15296278154063476002ULL);
}

TEST(XXHasher, big_object_with_multiple_small_updates_with_seed)
{
    Xxhasher hasher{kOtherSeed};
    for (std::size_t i = 0; i < kRepeatCount; ++i)
        hasher(kInput.data(), kInput.size());

    EXPECT_EQ(static_cast<Xxhasher::result_type>(hasher), 17285302196561698791ULL);
}

TEST(XXHasher, big_object_with_small_and_big_updates_without_seed)
{
    Xxhasher hasher{};
    std::string const bigObject = repeat(kInput, kBigObjectRepeat);

    hasher(kInput.data(), kInput.size());
    hasher(bigObject.data(), bigObject.size());
    hasher(kInput.data(), kInput.size());

    EXPECT_EQ(static_cast<Xxhasher::result_type>(hasher), 1865045178324729219ULL);
}

TEST(XXHasher, big_object_with_small_and_big_updates_with_seed)
{
    Xxhasher hasher{kOtherSeed};
    std::string const bigObject = repeat(kInput, kBigObjectRepeat);

    hasher(kInput.data(), kInput.size());
    hasher(bigObject.data(), bigObject.size());
    hasher(kInput.data(), kInput.size());

    EXPECT_EQ(static_cast<Xxhasher::result_type>(hasher), 16189862915636005281ULL);
}

TEST(XXHasher, big_object_with_one_update_without_seed)
{
    Xxhasher hasher{};
    std::string const object = repeat(kInput, kRepeatCount);
    hasher(object.data(), object.size());

    // Hashing the whole object at once must match hashing it in kRepeatCount pieces.
    EXPECT_EQ(static_cast<Xxhasher::result_type>(hasher), 15296278154063476002ULL);
}

TEST(XXHasher, big_object_with_one_update_with_seed)
{
    Xxhasher hasher{kOtherSeed};
    std::string const object = repeat(kInput, kRepeatCount);
    hasher(object.data(), object.size());

    EXPECT_EQ(static_cast<Xxhasher::result_type>(hasher), 17285302196561698791ULL);
}

TEST(XXHasher, operator_result_type_does_not_change_internal_state)
{
    {
        Xxhasher hasher;
        std::string const object{"Hello xxhash"};
        hasher(object.data(), object.size());

        EXPECT_EQ(
            static_cast<Xxhasher::result_type>(hasher), static_cast<Xxhasher::result_type>(hasher));
    }
    {
        Xxhasher hasher;
        std::string const object = repeat(kInput, kRepeatCount);
        hasher(object.data(), object.size());

        EXPECT_EQ(hasher.operator Xxhasher::result_type(), hasher.operator Xxhasher::result_type());
    }
}

}  // namespace beast
