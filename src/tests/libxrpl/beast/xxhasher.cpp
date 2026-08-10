#include <xrpl/beast/hash/xxhasher.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace beast {
namespace {

// Hashing the same input repeatedly builds up a larger object without holding
// it in memory; several cases below rely on that to exercise xxHash's
// buffered-update path.
std::string
repeat(std::string_view text, int times)
{
    std::string out;
    for (int i = 0; i < times; ++i)
        out += text;
    return out;
}

constexpr std::string_view kInput{"Hello, xxHash!"};

}  // namespace

TEST(XXHasher, withoutSeed)
{
    Xxhasher hasher{};
    hasher(kInput.data(), kInput.size());

    EXPECT_EQ(static_cast<Xxhasher::result_type>(hasher), 16042857369214894119ULL);
}

TEST(XXHasher, withSeed)
{
    Xxhasher hasher{static_cast<std::uint32_t>(102)};
    hasher(kInput.data(), kInput.size());

    EXPECT_EQ(static_cast<Xxhasher::result_type>(hasher), 14440132435660934800ULL);
}

TEST(XXHasher, withTwoSeeds)
{
    Xxhasher hasher{static_cast<std::uint32_t>(102), static_cast<std::uint32_t>(103)};
    hasher(kInput.data(), kInput.size());

    // The second seed is ignored, so this matches the single-seed result.
    EXPECT_EQ(static_cast<Xxhasher::result_type>(hasher), 14440132435660934800ULL);
}

TEST(XXHasher, bigObjectWithMultipleSmallUpdatesWithoutSeed)
{
    Xxhasher hasher{};
    for (int i = 0; i < 100; ++i)
        hasher(kInput.data(), kInput.size());

    EXPECT_EQ(static_cast<Xxhasher::result_type>(hasher), 15296278154063476002ULL);
}

TEST(XXHasher, bigObjectWithMultipleSmallUpdatesWithSeed)
{
    Xxhasher hasher{static_cast<std::uint32_t>(103)};
    for (int i = 0; i < 100; ++i)
        hasher(kInput.data(), kInput.size());

    EXPECT_EQ(static_cast<Xxhasher::result_type>(hasher), 17285302196561698791ULL);
}

TEST(XXHasher, bigObjectWithSmallAndBigUpdatesWithoutSeed)
{
    Xxhasher hasher{};
    std::string const bigObject = repeat(kInput, 20);

    hasher(kInput.data(), kInput.size());
    hasher(bigObject.data(), bigObject.size());
    hasher(kInput.data(), kInput.size());

    EXPECT_EQ(static_cast<Xxhasher::result_type>(hasher), 1865045178324729219ULL);
}

TEST(XXHasher, bigObjectWithSmallAndBigUpdatesWithSeed)
{
    Xxhasher hasher{static_cast<std::uint32_t>(103)};
    std::string const bigObject = repeat(kInput, 20);

    hasher(kInput.data(), kInput.size());
    hasher(bigObject.data(), bigObject.size());
    hasher(kInput.data(), kInput.size());

    EXPECT_EQ(static_cast<Xxhasher::result_type>(hasher), 16189862915636005281ULL);
}

TEST(XXHasher, bigObjectWithOneUpdateWithoutSeed)
{
    Xxhasher hasher{};
    std::string const object = repeat(kInput, 100);
    hasher(object.data(), object.size());

    // Hashing the whole object at once must match hashing it in 100 pieces.
    EXPECT_EQ(static_cast<Xxhasher::result_type>(hasher), 15296278154063476002ULL);
}

TEST(XXHasher, bigObjectWithOneUpdateWithSeed)
{
    Xxhasher hasher{static_cast<std::uint32_t>(103)};
    std::string const object = repeat(kInput, 100);
    hasher(object.data(), object.size());

    EXPECT_EQ(static_cast<Xxhasher::result_type>(hasher), 17285302196561698791ULL);
}

TEST(XXHasher, operatorResultTypeDoesNotChangeInternalState)
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
        std::string const object = repeat(kInput, 100);
        hasher(object.data(), object.size());

        EXPECT_EQ(hasher.operator Xxhasher::result_type(), hasher.operator Xxhasher::result_type());
    }
}

}  // namespace beast
