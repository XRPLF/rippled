#include <xrpl/beast/hash/xxhasher.h>
#include <xrpl/beast/unit_test.h>

namespace beast {

class XXHasher_test : public unit_test::suite
{
public:
    void
    testWithoutSeed()
    {
        testcase("Without seed");

        xxhasher hasher{};

        std::string objectToHash{"Hello, xxHash!"};
        hasher(objectToHash.data(), objectToHash.size());

        BEAST_EXPECT(
            static_cast<xxhasher::result_type>(hasher) ==
            16042857369214894119ULL);
    }

    void
    testWithSeed()
    {
        testcase("With seed");

        xxhasher hasher{static_cast<std::uint32_t>(102)};

        std::string objectToHash{"Hello, xxHash!"};
        hasher(objectToHash.data(), objectToHash.size());

        BEAST_EXPECT(
            static_cast<xxhasher::result_type>(hasher) ==
            14440132435660934800ULL);
    }

    void
    testWithTwoSeeds()
    {
        testcase("With two seeds");
        xxhasher hasher{
            static_cast<std::uint32_t>(102), static_cast<std::uint32_t>(103)};

        std::string objectToHash{"Hello, xxHash!"};
        hasher(objectToHash.data(), objectToHash.size());

        BEAST_EXPECT(
            static_cast<xxhasher::result_type>(hasher) ==
            14440132435660934800ULL);
    }

    void
    testBigObjectWithMultiupleSmallUpdatesWithoutSeed()
    {
        testcase("Big object with multiple small updates without seed");
        xxhasher hasher{};

        std::string objectToHash{"Hello, xxHash!"};
        for (int i = 0; i < 100; i++)
        {
            hasher(objectToHash.data(), objectToHash.size());
        }

        BEAST_EXPECT(
            static_cast<xxhasher::result_type>(hasher) ==
            15296278154063476002ULL);
    }

    void
    testBigObjectWithMultiupleSmallUpdatesWithSeed()
    {
        testcase("Big object with multiple small updates with seed");
        xxhasher hasher{static_cast<std::uint32_t>(103)};

        std::string objectToHash{"Hello, xxHash!"};
        for (int i = 0; i < 100; i++)
        {
            hasher(objectToHash.data(), objectToHash.size());
        }

        BEAST_EXPECT(
            static_cast<xxhasher::result_type>(hasher) ==
            17285302196561698791ULL);
    }

    void
    testBigObjectWithSmallAndBigUpdatesWithoutSeed()
    {
        testcase("Big object with small and big updates without seed");
        xxhasher hasher{};

        std::string objectToHash{"Hello, xxHash!"};
        std::string bigObject;
        for (int i = 0; i < 20; i++)
        {
            bigObject += "Hello, xxHash!";
        }
        hasher(objectToHash.data(), objectToHash.size());
        hasher(bigObject.data(), bigObject.size());
        hasher(objectToHash.data(), objectToHash.size());

        BEAST_EXPECT(
            static_cast<xxhasher::result_type>(hasher) ==
            1865045178324729219ULL);
    }

    void
    testBigObjectWithSmallAndBigUpdatesWithSeed()
    {
        testcase("Big object with small and big updates with seed");
        xxhasher hasher{static_cast<std::uint32_t>(103)};

        std::string objectToHash{"Hello, xxHash!"};
        std::string bigObject;
        for (int i = 0; i < 20; i++)
        {
            bigObject += "Hello, xxHash!";
        }
        hasher(objectToHash.data(), objectToHash.size());
        hasher(bigObject.data(), bigObject.size());
        hasher(objectToHash.data(), objectToHash.size());

        BEAST_EXPECT(
            static_cast<xxhasher::result_type>(hasher) ==
            16189862915636005281ULL);
    }

    void
    testBigObjectWithOneUpdateWithoutSeed()
    {
        testcase("Big object with one update without seed");
        xxhasher hasher{};

        std::string objectToHash;
        for (int i = 0; i < 100; i++)
        {
            objectToHash += "Hello, xxHash!";
        }
        hasher(objectToHash.data(), objectToHash.size());

        BEAST_EXPECT(
            static_cast<xxhasher::result_type>(hasher) ==
            15296278154063476002ULL);
    }

    void
    testBigObjectWithOneUpdateWithSeed()
    {
        testcase("Big object with one update with seed");
        xxhasher hasher{static_cast<std::uint32_t>(103)};

        std::string objectToHash;
        for (int i = 0; i < 100; i++)
        {
            objectToHash += "Hello, xxHash!";
        }
        hasher(objectToHash.data(), objectToHash.size());

        BEAST_EXPECT(
            static_cast<xxhasher::result_type>(hasher) ==
            17285302196561698791ULL);
    }

    void
    testOperatorResultTypeDoesNotChangeInternalState()
    {
        testcase("Operator result type doesn't change the internal state");
        {
            xxhasher hasher;

            std::string object{"Hello xxhash"};
            hasher(object.data(), object.size());
            auto xxhashResult1 = static_cast<xxhasher::result_type>(hasher);
            auto xxhashResult2 = static_cast<xxhasher::result_type>(hasher);

            BEAST_EXPECT(xxhashResult1 == xxhashResult2);
        }
        {
            xxhasher hasher;

            std::string object;
            for (int i = 0; i < 100; i++)
            {
                object += "Hello, xxHash!";
            }
            hasher(object.data(), object.size());
            auto xxhashResult1 = hasher.operator xxhasher::result_type();
            auto xxhashResult2 = hasher.operator xxhasher::result_type();

            BEAST_EXPECT(xxhashResult1 == xxhashResult2);
        }
    }

    void
    run() override
    {
        testWithoutSeed();
        testWithSeed();
        testWithTwoSeeds();
        testBigObjectWithMultiupleSmallUpdatesWithoutSeed();
        testBigObjectWithMultiupleSmallUpdatesWithSeed();
        testBigObjectWithSmallAndBigUpdatesWithoutSeed();
        testBigObjectWithSmallAndBigUpdatesWithSeed();
        testBigObjectWithOneUpdateWithoutSeed();
        testBigObjectWithOneUpdateWithSeed();
        testOperatorResultTypeDoesNotChangeInternalState();
    }
};

BEAST_DEFINE_TESTSUITE(XXHasher, beast_core, beast);
}  // namespace beast
