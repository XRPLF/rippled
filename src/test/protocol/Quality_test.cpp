#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstdint>
#include <type_traits>

namespace xrpl {

class Quality_test : public beast::unit_test::Suite
{
public:
    // Create a raw, non-integral amount from mantissa and exponent
    STAmount static raw(std::uint64_t mantissa, int exponent)
    {
        return STAmount(Issue{Currency(3), AccountID(3)}, mantissa, exponent);
    }

    template <class Integer>
    static STAmount
    amount(Integer integer, std::enable_if_t<std::is_signed_v<Integer>>* = 0)
    {
        static_assert(std::is_integral_v<Integer>, "");
        return STAmount(integer, false);
    }

    template <class Integer>
    static STAmount
    amount(Integer integer, std::enable_if_t<!std::is_signed_v<Integer>>* = 0)
    {
        static_assert(std::is_integral_v<Integer>, "");
        if (integer < 0)
            return STAmount(-integer, true);
        return STAmount(integer, false);
    }

    template <class In, class Out>
    static Amounts
    amounts(In in, Out out)
    {
        return Amounts(amount(in), amount(out));
    }

    template <class In1, class Out1, class Int, class In2, class Out2>
    void
    ceilIn(Quality const& q, In1 in, Out1 out, Int limit, In2 inExpected, Out2 outExpected)
    {
        auto expectResult(amounts(inExpected, outExpected));
        auto actualResult(q.ceilIn(amounts(in, out), amount(limit)));

        BEAST_EXPECT(actualResult == expectResult);
    }

    template <class In1, class Out1, class Int, class In2, class Out2>
    void
    ceilOut(Quality const& q, In1 in, Out1 out, Int limit, In2 inExpected, Out2 outExpected)
    {
        auto const expectResult(amounts(inExpected, outExpected));
        auto const actualResult(q.ceilOut(amounts(in, out), amount(limit)));

        BEAST_EXPECT(actualResult == expectResult);
    }

    void
    testCeilIn()
    {
        testcase("ceil_in");

        {
            // 1 in, 1 out:
            Quality const q(Amounts(amount(1), amount(1)));

            ceilIn(
                q,
                1,
                1,  // 1 in, 1 out
                1,  // limit: 1
                1,
                1);  // 1 in, 1 out

            ceilIn(
                q,
                10,
                10,  // 10 in, 10 out
                5,   // limit: 5
                5,
                5);  // 5 in, 5 out

            ceilIn(
                q,
                5,
                5,   // 5 in, 5 out
                10,  // limit: 10
                5,
                5);  // 5 in, 5 out
        }

        {
            // 1 in, 2 out:
            Quality const q(Amounts(amount(1), amount(2)));

            ceilIn(
                q,
                40,
                80,  // 40 in, 80 out
                40,  // limit: 40
                40,
                80);  // 40 in, 20 out

            ceilIn(
                q,
                40,
                80,  // 40 in, 80 out
                20,  // limit: 20
                20,
                40);  // 20 in, 40 out

            ceilIn(
                q,
                40,
                80,  // 40 in, 80 out
                60,  // limit: 60
                40,
                80);  // 40 in, 80 out
        }

        {
            // 2 in, 1 out:
            Quality const q(Amounts(amount(2), amount(1)));

            ceilIn(
                q,
                40,
                20,  // 40 in, 20 out
                20,  // limit: 20
                20,
                10);  // 20 in, 10 out

            ceilIn(
                q,
                40,
                20,  // 40 in, 20 out
                40,  // limit: 40
                40,
                20);  // 40 in, 20 out

            ceilIn(
                q,
                40,
                20,  // 40 in, 20 out
                50,  // limit: 40
                40,
                20);  // 40 in, 20 out
        }
    }

    void
    testCeilOut()
    {
        testcase("ceil_out");

        {
            // 1 in, 1 out:
            Quality const q(Amounts(amount(1), amount(1)));

            ceilOut(
                q,
                1,
                1,  // 1 in, 1 out
                1,  // limit 1
                1,
                1);  // 1 in, 1 out

            ceilOut(
                q,
                10,
                10,  // 10 in, 10 out
                5,   // limit 5
                5,
                5);  // 5 in, 5 out

            ceilOut(
                q,
                10,
                10,  // 10 in, 10 out
                20,  // limit 20
                10,
                10);  // 10 in, 10 out
        }

        {
            // 1 in, 2 out:
            Quality const q(Amounts(amount(1), amount(2)));

            ceilOut(
                q,
                40,
                80,  // 40 in, 80 out
                40,  // limit 40
                20,
                40);  // 20 in, 40 out

            ceilOut(
                q,
                40,
                80,  // 40 in, 80 out
                80,  // limit 80
                40,
                80);  // 40 in, 80 out

            ceilOut(
                q,
                40,
                80,   // 40 in, 80 out
                100,  // limit 100
                40,
                80);  // 40 in, 80 out
        }

        {
            // 2 in, 1 out:
            Quality const q(Amounts(amount(2), amount(1)));

            ceilOut(
                q,
                40,
                20,  // 40 in, 20 out
                20,  // limit 20
                40,
                20);  // 40 in, 20 out

            ceilOut(
                q,
                40,
                20,  // 40 in, 20 out
                40,  // limit 40
                40,
                20);  // 40 in, 20 out

            ceilOut(
                q,
                40,
                20,  // 40 in, 20 out
                10,  // limit 10
                20,
                10);  // 20 in, 10 out
        }
    }

    void
    testRaw()
    {
        testcase("raw");

        {
            Quality const q(0x5d048191fb9130daull);  // 126836389.7680090
            Amounts const value(
                amount(349469768),                             // 349.469768 XRP
                raw(2755280000000000ull, -15));                // 2.75528
            STAmount const limit(raw(4131113916555555, -16));  // .4131113916555555
            Amounts const result(q.ceilOut(value, limit));
            BEAST_EXPECT(result.in != beast::kZero);
        }
    }

    void
    testRound()
    {
        testcase("round");

        Quality const q(0x59148191fb913522ull);  // 57719.63525051682
        BEAST_EXPECT(q.round(3).rate().getText() == "57800");
        BEAST_EXPECT(q.round(4).rate().getText() == "57720");
        BEAST_EXPECT(q.round(5).rate().getText() == "57720");
        BEAST_EXPECT(q.round(6).rate().getText() == "57719.7");
        BEAST_EXPECT(q.round(7).rate().getText() == "57719.64");
        BEAST_EXPECT(q.round(8).rate().getText() == "57719.636");
        BEAST_EXPECT(q.round(9).rate().getText() == "57719.6353");
        BEAST_EXPECT(q.round(10).rate().getText() == "57719.63526");
        BEAST_EXPECT(q.round(11).rate().getText() == "57719.635251");
        BEAST_EXPECT(q.round(12).rate().getText() == "57719.6352506");
        BEAST_EXPECT(q.round(13).rate().getText() == "57719.63525052");
        BEAST_EXPECT(q.round(14).rate().getText() == "57719.635250517");
        BEAST_EXPECT(q.round(15).rate().getText() == "57719.6352505169");
        BEAST_EXPECT(q.round(16).rate().getText() == "57719.63525051682");
    }

    void
    testComparisons()
    {
        testcase("comparisons");

        STAmount const amount1(noIssue(), 231);
        STAmount const amount2(noIssue(), 462);
        STAmount const amount3(noIssue(), 924);

        Quality const q11(Amounts(amount1, amount1));
        Quality const q12(Amounts(amount1, amount2));
        Quality const q13(Amounts(amount1, amount3));
        Quality const q21(Amounts(amount2, amount1));
        Quality const q31(Amounts(amount3, amount1));

        BEAST_EXPECT(q11 == q11);
        BEAST_EXPECT(q11 < q12);
        BEAST_EXPECT(q12 < q13);
        BEAST_EXPECT(q31 < q21);
        BEAST_EXPECT(q21 < q11);
        BEAST_EXPECT(q11 >= q11);
        BEAST_EXPECT(q12 >= q11);
        BEAST_EXPECT(q13 >= q12);
        BEAST_EXPECT(q21 >= q31);
        BEAST_EXPECT(q11 >= q21);
        BEAST_EXPECT(q12 > q11);
        BEAST_EXPECT(q13 > q12);
        BEAST_EXPECT(q21 > q31);
        BEAST_EXPECT(q11 > q21);
        BEAST_EXPECT(q11 <= q11);
        BEAST_EXPECT(q11 <= q12);
        BEAST_EXPECT(q12 <= q13);
        BEAST_EXPECT(q31 <= q21);
        BEAST_EXPECT(q21 <= q11);
        BEAST_EXPECT(q31 != q21);
    }

    void
    testComposition()
    {
        testcase("composition");

        STAmount const amount1(noIssue(), 231);
        STAmount const amount2(noIssue(), 462);
        STAmount const amount3(noIssue(), 924);

        Quality const q11(Amounts(amount1, amount1));
        Quality const q12(Amounts(amount1, amount2));
        Quality const q13(Amounts(amount1, amount3));
        Quality const q21(Amounts(amount2, amount1));
        Quality const q31(Amounts(amount3, amount1));

        BEAST_EXPECT(composedQuality(q12, q21) == q11);

        Quality const q1331(composedQuality(q13, q31));
        Quality const q3113(composedQuality(q31, q13));

        BEAST_EXPECT(q1331 == q3113);
        BEAST_EXPECT(q1331 == q11);
    }

    void
    testOperations()
    {
        testcase("operations");

        Quality const q11(Amounts(STAmount(noIssue(), 731), STAmount(noIssue(), 731)));

        Quality qa(q11);
        Quality qb(q11);

        BEAST_EXPECT(qa == qb);
        BEAST_EXPECT(++qa != q11);
        BEAST_EXPECT(qa != qb);
        BEAST_EXPECT(--qb != q11);
        BEAST_EXPECT(qa != qb);
        BEAST_EXPECT(qb < qa);
        BEAST_EXPECT(qb++ < qa);
        BEAST_EXPECT(qb++ < qa);
        BEAST_EXPECT(qb++ == qa);
        BEAST_EXPECT(qa < qb);
    }
    void
    run() override
    {
        testComparisons();
        testComposition();
        testOperations();
        testCeilIn();
        testCeilOut();
        testRaw();
        testRound();
    }
};

BEAST_DEFINE_TESTSUITE(Quality, protocol, xrpl);

}  // namespace xrpl
