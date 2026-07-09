#include <test/jtx/Account.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/OwnerCounts.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>

namespace xrpl::test {

class OwnerCounts_test : public beast::unit_test::Suite
{
    static OwnerCounts
    makeCounts(std::uint32_t owner, std::uint32_t sponsored, std::uint32_t sponsoring)
    {
        OwnerCounts counts;
        counts.owner = owner;
        counts.sponsored = sponsored;
        counts.sponsoring = sponsoring;
        return counts;
    }

    void
    testCount()
    {
        // count() == owner - sponsored + sponsoring, saturating at the
        // uint32 maximum.
        testcase("count");

        constexpr auto maxU32 = std::numeric_limits<std::uint32_t>::max();

        {
            BEAST_EXPECT(OwnerCounts().count() == 0);
            // 5 - 2 + 3 == 6
            BEAST_EXPECT(makeCounts(5, 2, 3).count() == 6);
            // owner only
            BEAST_EXPECT(makeCounts(7, 0, 0).count() == 7);
            // fully sponsored: 4 - 4 + 0 == 0
            BEAST_EXPECT(makeCounts(4, 4, 0).count() == 0);
        }

        // count() saturation: totals above uint32 max clamp to uint32 max
        {
            // maxU32 - 0 + 1 overflows uint32 and clamps
            BEAST_EXPECT(makeCounts(maxU32, 0, 1).count() == maxU32);
            // clamp applies no matter how far past the max
            BEAST_EXPECT(makeCounts(maxU32, 0, maxU32).count() == maxU32);
        }
    }

    void
    testOrdering()
    {
        // The operator<=> tie-break chain decides what DeferredCredits
        // stores via std::max when PaymentSandbox layers merge.
        testcase("ordering");

        constexpr auto maxU32 = std::numeric_limits<std::uint32_t>::max();

        // count() dominates the comparison
        {
            auto const a = makeCounts(1, 0, 0);  // count() == 1
            auto const b = makeCounts(3, 0, 0);  // count() == 3
            BEAST_EXPECT(a < b);
            BEAST_EXPECT(b > a);
            BEAST_EXPECT(std::max(a, b) == b);
        }

        // Tie-break 1: equal count(), higher owner wins
        // a: 2 - 1 + 0 == 1, b: 1 - 0 + 0 == 1
        {
            auto const a = makeCounts(2, 1, 0);
            auto const b = makeCounts(1, 0, 0);
            BEAST_EXPECT(a.count() == b.count());
            BEAST_EXPECT(a > b);
            // Both argument orders agree on which one std::max keeps
            BEAST_EXPECT(std::max(a, b) == a);
            BEAST_EXPECT(std::max(b, a) == a);
        }

        // Tie-break 2: equal count() and owner, higher sponsored wins
        // a: 5 - 2 + 1 == 4, b: 5 - 3 + 2 == 4
        {
            auto const a = makeCounts(5, 2, 1);
            auto const b = makeCounts(5, 3, 2);
            BEAST_EXPECT(a.count() == b.count());
            BEAST_EXPECT(a.owner == b.owner);
            BEAST_EXPECT(a < b);
            BEAST_EXPECT(std::max(a, b) == b);
        }

        // Tie-break 3: equal count(), owner, and sponsored, higher
        // sponsoring wins. With exact arithmetic, equal count(), owner, and
        // sponsored force equal sponsoring, so this arm is only reachable
        // when count() saturates: both clamp to uint32 max.
        {
            auto const a = makeCounts(maxU32, 0, 1);
            auto const b = makeCounts(maxU32, 0, 2);
            BEAST_EXPECT(a.count() == b.count());
            BEAST_EXPECT(a < b);
            BEAST_EXPECT(std::max(a, b) == b);
        }
    }

    void
    testEquality()
    {
        testcase("equality");

        auto const a = makeCounts(3, 1, 2);
        auto const& self = a;
        BEAST_EXPECT(a == self);                 // self-compare
        BEAST_EXPECT(a == makeCounts(3, 1, 2));  // all fields equal
        BEAST_EXPECT(a != makeCounts(4, 1, 2));  // differing owner
        BEAST_EXPECT(a != makeCounts(3, 2, 2));  // differing sponsored
        BEAST_EXPECT(a != makeCounts(3, 1, 3));  // differing sponsoring
        // Equal count() (3 - 1 + 2 == 4 - 0 + 0) is not enough for
        // equality; the fields themselves must match
        BEAST_EXPECT(a.count() == makeCounts(4, 0, 0).count());
        BEAST_EXPECT(a != makeCounts(4, 0, 0));
    }

    void
    testSleConstruction()
    {
        // Construction from an AccountRoot SLE reads sfOwnerCount and treats
        // the soeDEFAULT sponsorship fields as 0 when absent.
        testcase("sleConstruction");

        auto const alice = jtx::Account("alice");
        auto const makeAccountRoot = [&alice]() {
            return std::make_shared<SLE>(keylet::account(alice.id()));
        };

        // All sponsorship fields absent: only sfOwnerCount contributes.
        {
            auto const sle = makeAccountRoot();
            sle->setFieldU32(sfOwnerCount, 7);

            OwnerCounts const counts(sle);
            BEAST_EXPECT(counts.owner == 7);
            BEAST_EXPECT(counts.sponsored == 0);
            BEAST_EXPECT(counts.sponsoring == 0);
            BEAST_EXPECT(counts.count() == 7);
        }

        // All three fields present.
        {
            auto const sle = makeAccountRoot();
            sle->setFieldU32(sfOwnerCount, 5);
            sle->setFieldU32(sfSponsoredOwnerCount, 2);
            sle->setFieldU32(sfSponsoringOwnerCount, 3);

            OwnerCounts const counts(sle);
            BEAST_EXPECT(counts.owner == 5);
            BEAST_EXPECT(counts.sponsored == 2);
            BEAST_EXPECT(counts.sponsoring == 3);
            BEAST_EXPECT(counts.count() == 6);
            BEAST_EXPECT(counts == makeCounts(5, 2, 3));
        }

        // A default AccountRoot (required sfOwnerCount defaults to 0) is the
        // zero value.
        {
            auto const sle = makeAccountRoot();
            BEAST_EXPECT(OwnerCounts(sle) == OwnerCounts());
        }
    }

public:
    void
    run() override
    {
        testCount();
        testOrdering();
        testEquality();
        testSleConstruction();
    }
};

BEAST_DEFINE_TESTSUITE(OwnerCounts, ledger, xrpl);

}  // namespace xrpl::test
