#include <xrpl/beast/unit_test/suite.h>
// DO NOT REMOVE
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/Units.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace xrpl::test {

class LendingHelpers_test : public beast::unit_test::Suite
{
    void
    testComputePaymentFactor()
    {
        using namespace jtx;
        using namespace xrpl::detail;
        Env const env{*this};
        auto const& rules = env.current()->rules();
        struct TestCase
        {
            std::string name;
            Number periodicRate;
            std::uint32_t paymentsRemaining;
            Number expectedPaymentFactor;
        };

        auto const testCases = std::vector<TestCase>{
            {
                .name = "Zero periodic rate",
                .periodicRate = Number{0},
                .paymentsRemaining = 4,
                .expectedPaymentFactor = Number{25, -2},
            },  // 1/4 = 0.25
            {
                .name = "One payment remaining",
                .periodicRate = Number{5, -2},
                .paymentsRemaining = 1,
                .expectedPaymentFactor = Number{105, -2},
            },  // 0.05/1 = 1.05
            {
                .name = "Multiple payments remaining",
                .periodicRate = Number{5, -2},
                .paymentsRemaining = 3,
                .expectedPaymentFactor = Number{3672085646312450436, -19},
            },  // from calc
            {
                .name = "Zero payments remaining",
                .periodicRate = Number{5, -2},
                .paymentsRemaining = 0,
                .expectedPaymentFactor = Number{0},
            }  // edge case
        };

        for (auto const& tc : testCases)
        {
            testcase("computePaymentFactor: " + tc.name);

            auto const computedPaymentFactor =
                computePaymentFactor(rules, tc.periodicRate, tc.paymentsRemaining);
            BEAST_EXPECTS(
                computedPaymentFactor == tc.expectedPaymentFactor,
                "Payment factor mismatch: expected " + to_string(tc.expectedPaymentFactor) +
                    ", got " + to_string(computedPaymentFactor));
        }
    }

    void
    testLoanPeriodicPayment()
    {
        using namespace jtx;
        using namespace xrpl::detail;
        Env const env{*this};
        auto const& rules = env.current()->rules();

        struct TestCase
        {
            std::string name;
            Number principalOutstanding;
            Number periodicRate;
            std::uint32_t paymentsRemaining;
            Number expectedPeriodicPayment;
        };

        auto const testCases = std::vector<TestCase>{
            {
                .name = "Zero principal outstanding",
                .principalOutstanding = Number{0},
                .periodicRate = Number{5, -2},
                .paymentsRemaining = 5,
                .expectedPeriodicPayment = Number{0},
            },
            {
                .name = "Zero payments remaining",
                .principalOutstanding = Number{1'000},
                .periodicRate = Number{5, -2},
                .paymentsRemaining = 0,
                .expectedPeriodicPayment = Number{0},
            },
            {
                .name = "Zero periodic rate",
                .principalOutstanding = Number{1'000},
                .periodicRate = Number{0},
                .paymentsRemaining = 4,
                .expectedPeriodicPayment = Number{250},
            },
            {
                .name = "Standard case",
                .principalOutstanding = Number{1'000},
                .periodicRate = loanPeriodicRate(TenthBips32(100'000), 30 * 24 * 60 * 60),
                .paymentsRemaining = 3,
                .expectedPeriodicPayment = Number{389569066396123265, -15},  // from calc
            },
        };

        for (auto const& tc : testCases)
        {
            testcase("loanPeriodicPayment: " + tc.name);

            auto const computedPeriodicPayment = loanPeriodicPayment(
                rules, tc.principalOutstanding, tc.periodicRate, tc.paymentsRemaining);
            BEAST_EXPECTS(
                computedPeriodicPayment == tc.expectedPeriodicPayment,
                "Periodic payment mismatch: expected " + to_string(tc.expectedPeriodicPayment) +
                    ", got " + to_string(computedPeriodicPayment));
        }
    }

    void
    testLoanPrincipalFromPeriodicPayment()
    {
        using namespace jtx;
        using namespace xrpl::detail;
        Env const env{*this};
        auto const& rules = env.current()->rules();

        struct TestCase
        {
            std::string name;
            Number periodicPayment;
            Number periodicRate;
            std::uint32_t paymentsRemaining;
            Number expectedPrincipalOutstanding;
        };

        auto const testCases = std::vector<TestCase>{
            {
                .name = "Zero periodic payment",
                .periodicPayment = Number{0},
                .periodicRate = Number{5, -2},
                .paymentsRemaining = 5,
                .expectedPrincipalOutstanding = Number{0},
            },
            {
                .name = "Zero payments remaining",
                .periodicPayment = Number{1'000},
                .periodicRate = Number{5, -2},
                .paymentsRemaining = 0,
                .expectedPrincipalOutstanding = Number{0},
            },
            {
                .name = "Zero periodic rate",
                .periodicPayment = Number{250},
                .periodicRate = Number{0},
                .paymentsRemaining = 4,
                .expectedPrincipalOutstanding = Number{1'000},
            },
            {
                .name = "Standard case",
                .periodicPayment = Number{389569066396123265, -15},  // from calc
                .periodicRate = loanPeriodicRate(TenthBips32(100'000), 30 * 24 * 60 * 60),
                .paymentsRemaining = 3,
                .expectedPrincipalOutstanding = Number{1'000},
            },
        };

        for (auto const& tc : testCases)
        {
            testcase("loanPrincipalFromPeriodicPayment: " + tc.name);

            auto const computedPrincipalOutstanding = loanPrincipalFromPeriodicPayment(
                rules, tc.periodicPayment, tc.periodicRate, tc.paymentsRemaining);
            BEAST_EXPECTS(
                computedPrincipalOutstanding == tc.expectedPrincipalOutstanding,
                "Principal outstanding mismatch: expected " +
                    to_string(tc.expectedPrincipalOutstanding) + ", got " +
                    to_string(computedPrincipalOutstanding));
        }
    }

    void
    testComputePowerMinusOne()
    {
        using namespace jtx;
        using namespace xrpl::detail;

        // Edge cases.
        {
            testcase("computePowerMinusOne: zero rate returns zero");
            BEAST_EXPECT(computePowerMinusOne(0, 5) == 0);
        }
        {
            testcase("computePowerMinusOne: zero paymentsRemaining returns zero");
            Number const fivePercent{5, -2};
            BEAST_EXPECT(computePowerMinusOne(fivePercent, 0) == 0);
        }
        // (1.05)^3 - 1 = 0.157625, computed independently by hand.
        {
            testcase("computePowerMinusOne: standard case (1.05)^3 - 1 = 0.157625");
            Number const r{5, -2};
            Number const expected{157625, -6};
            BEAST_EXPECT(computePowerMinusOne(r, 3) == expected);
        }
        // (1+1)^1 - 1 = 1.
        {
            testcase("computePowerMinusOne: r=1, n=1");
            BEAST_EXPECT(computePowerMinusOne(1, 1) == 1);
        }

        // Property check at near-zero rate (the bug regime): for n=2 the
        // mathematical identity is `(1+r)^2 - 1 = 2r + r^2`. We compute
        // `2r + r^2` by direct multiplication in Number arithmetic — a
        // path that doesn't share any code with the binomial loop — and
        // assert the two paths agree.
        {
            testcase("computePowerMinusOne: near-zero rate matches independent 2r + r^2");
            // r = 1 TenthBips32 over 600s payment interval, computed
            // independently below using xrpl::detail::loanPeriodicRate.
            Number const r = loanPeriodicRate(TenthBips32{1}, 600);
            Number const independentExpected = 2 * r + r * r;  // (1+r)^2 - 1
            BEAST_EXPECT(computePowerMinusOne(r, 2) == independentExpected);
        }
        // Same property at n=3: (1+r)^3 - 1 = 3r + 3r^2 + r^3.
        {
            testcase("computePowerMinusOne: near-zero rate matches independent 3r + 3r^2 + r^3");
            Number const r = loanPeriodicRate(TenthBips32{1}, 600);
            Number const independentExpected = 3 * r + 3 * r * r + r * r * r;
            BEAST_EXPECT(computePowerMinusOne(r, 3) == independentExpected);
        }

        // Larger-n stress test for the loop's early-termination logic.
        // At very small r the binomial terms decrease by a factor of
        // ~r*(n-k)/(k+1) per step, so even at n=1000 the loop should
        // terminate in a small handful of iterations. Cross-check the
        // result against the hybrid (which dispatches to this same
        // binomial path when r*n < 1e-9).
        {
            testcase("computePowerMinusOne: large n, early termination matches hybrid output");
            // r*n = 1e-10 and 1e-12 — both clearly below the 1e-9 threshold.
            Number const r1{1, -13};
            std::uint32_t const n1 = 1'000;
            Number const r2{1, -15};
            std::uint32_t const n2 = 1'000;
            BEAST_EXPECT(computePowerMinusOne(r1, n1) == computePowerMinusOneHybrid(r1, n1));
            BEAST_EXPECT(computePowerMinusOne(r2, n2) == computePowerMinusOneHybrid(r2, n2));
            BEAST_EXPECT(computePowerMinusOne(r1, n1) > 0);
            BEAST_EXPECT(computePowerMinusOne(r2, n2) > 0);
        }
    }

    // Direct tests of `computePowerMinusOneHybrid`. Verifies the dispatcher
    // picks the right branch and produces the right result on each side
    // of the threshold.
    void
    testComputePowerMinusOneHybrid()
    {
        using namespace jtx;
        using namespace xrpl::detail;

        // Above threshold (r * n >= 1e-9): hybrid must agree with the closed
        // form `power(1+r, n) - 1` exactly (it is the closed form).
        {
            testcase("computePowerMinusOneHybrid: r*n >= 1e-9 uses closed form (bit-exact match)");

            struct AboveThreshold
            {
                std::string name;
                Number r;
                std::uint32_t n;
            };
            auto const cases = std::vector<AboveThreshold>{
                {.name = "r=5%, n=3", .r = Number{5, -2}, .n = 3},
                {.name = "r=0.1%, n=1000", .r = Number{1, -3}, .n = 1'000},
                {.name = "r=1e-7, n=100 (above threshold by 10x)", .r = Number{1, -7}, .n = 100},
            };
            for (auto const& tc : cases)
            {
                Number const closed = power(1 + tc.r, tc.n) - 1;
                Number const hybrid = computePowerMinusOneHybrid(tc.r, tc.n);
                BEAST_EXPECTS(
                    hybrid == closed,
                    tc.name + ": closed=" + to_string(closed) + ", hybrid=" + to_string(hybrid));
            }
        }

        // Below threshold (r * n < 1e-9): hybrid must agree with
        // `computePowerMinusOne` (the binomial expansion). At this regime
        // the closed form is provably wrong (cancellation); we verify the
        // dispatcher routes to the binomial path.
        {
            testcase(
                "computePowerMinusOneHybrid: r*n < 1e-9 uses binomial expansion (bit-exact match)");

            struct BelowThreshold
            {
                std::string name;
                Number r;
                std::uint32_t n;
            };
            auto const cases = std::vector<BelowThreshold>{
                // bug regime: r = 1 TenthBips32 over 600s payment interval
                // → r ≈ 1.9e-10, r*n ≈ 3.8e-10 < 1e-9.
                {.name = "bug regime: r~1.9e-10, n=2",
                 .r = loanPeriodicRate(TenthBips32{1}, 600),
                 .n = 2},
                {.name = "r=1e-12, n=100", .r = Number{1, -12}, .n = 100},
            };
            for (auto const& tc : cases)
            {
                Number const binom = computePowerMinusOne(tc.r, tc.n);
                Number const hybrid = computePowerMinusOneHybrid(tc.r, tc.n);
                BEAST_EXPECTS(
                    hybrid == binom,
                    tc.name + ": binom=" + to_string(binom) + ", hybrid=" + to_string(hybrid));
            }
        }

        // Edge cases.
        {
            testcase("computePowerMinusOneHybrid: edge cases");
            Number const fivePercent{5, -2};
            BEAST_EXPECT(computePowerMinusOneHybrid(0, 100) == 0);
            BEAST_EXPECT(computePowerMinusOneHybrid(fivePercent, 0) == 0);
            BEAST_EXPECT(computePowerMinusOneHybrid(0, 0) == 0);
        }

        // Threshold boundary: r*n = 1e-9 exactly. Hybrid uses `>=` against
        // the threshold, so this case must take the closed-form branch.
        // We also verify that the binomial path agrees with the closed
        // form to high precision at this crossover — confirming the
        // threshold is placed where both paths give "adequate" answers.
        {
            testcase("computePowerMinusOneHybrid: threshold boundary r*n = 1e-9");

            // Construct exactly r*n = 1e-9 with two distinct (r, n) pairs.
            struct Boundary
            {
                std::string name;
                Number r;
                std::uint32_t n;
            };
            auto const cases = std::vector<Boundary>{
                {.name = "r=1e-9, n=1", .r = Number{1, -9}, .n = 1},
                {.name = "r=1e-12, n=1000", .r = Number{1, -12}, .n = 1'000},
            };

            for (auto const& tc : cases)
            {
                Number const closed = power(1 + tc.r, tc.n) - 1;
                Number const hybrid = computePowerMinusOneHybrid(tc.r, tc.n);
                Number const binom = computePowerMinusOne(tc.r, tc.n);

                // At exact threshold, hybrid must take closed-form path:
                // bit-exact match with closed.
                BEAST_EXPECTS(
                    hybrid == closed,
                    tc.name + ": hybrid should equal closed at threshold; got hybrid=" +
                        to_string(hybrid) + ", closed=" + to_string(closed));

                // Closed-form and binomial must agree at the threshold to
                // within Number's post-subtraction precision (~10 sig
                // digits of `r*n = 1e-9`, i.e. ~1e-19 absolute error).
                Number const tolerance{1, -18};
                Number const diff = abs(closed - binom);
                BEAST_EXPECTS(
                    diff < tolerance,
                    tc.name + ": closed and binomial diverge at threshold by " + to_string(diff));
            }
        }
    }

    // Regression: at near-zero rate, `loanPrincipalFromPeriodicPayment`
    // must satisfy `principal <= periodicPayment * paymentsRemaining` for
    // any non-negative rate. The naive closed-form path violated this
    // bound due to catastrophic cancellation in `(1+r)^n - 1`.
    void
    testLoanPrincipalFromPeriodicPaymentNearZeroRate()
    {
        testcase("loanPrincipalFromPeriodicPayment: principal <= payment*n at near-zero rate");
        using namespace jtx;
        using namespace xrpl::detail;
        Env const env{*this};
        auto const& rules = env.current()->rules();

        // Inputs from the bug reproduction in Loan_test.cpp:
        //   InterestRate = 1 TenthBips32 (0.001 % per year),
        //   PaymentInterval = 600 s, principal = 100, 3 payments.
        // periodicRate is ~1.9e-10.
        auto const periodicRate = loanPeriodicRate(TenthBips32{1}, 600);
        auto const periodicPayment = loanPeriodicPayment(rules, 100, periodicRate, 3);

        for (auto const n : {3u, 2u, 1u})
        {
            auto const computed =
                loanPrincipalFromPeriodicPayment(rules, periodicPayment, periodicRate, n);
            auto const upperBound = periodicPayment * Number{n};
            BEAST_EXPECTS(
                computed <= upperBound,
                "n=" + std::to_string(n) + ": payment*n=" + to_string(upperBound) +
                    ", principal=" + to_string(computed));
        }
    }

    // Regression: `computeTheoreticalLoanState` must produce a non-negative
    // `interestDue` for any non-negative rate. Pre-fix, near-zero rates
    // produced a negative `interestDue` because `(1+r)^n - 1` lost most of
    // its precision to cancellation.
    void
    testComputeTheoreticalLoanStateNearZeroRate()
    {
        testcase("computeTheoreticalLoanState: non-negative interestDue at near-zero rate");
        using namespace jtx;
        using namespace xrpl::detail;
        Env const env{*this};
        auto const& rules = env.current()->rules();

        auto const periodicRate = loanPeriodicRate(TenthBips32{1}, 600);
        auto const periodicPayment = loanPeriodicPayment(rules, 100, periodicRate, 3);

        auto const state =
            computeTheoreticalLoanState(rules, periodicPayment, periodicRate, 2, TenthBips32{0});

        BEAST_EXPECT(state.principalOutstanding <= state.valueOutstanding);
        BEAST_EXPECT(state.interestDue >= 0);
        BEAST_EXPECT(state.managementFeeDue == 0);
    }

    // Direct gating proof: at near-zero rate, `computePaymentFactor` must
    // return different values with `fixCleanup3_2_0` disabled vs enabled.
    // The enabled path agrees with an independent polynomial reference;
    // the disabled path diverges by a measurable amount due to the
    // catastrophic cancellation in `(1+r)^n - 1`.
    void
    testComputePaymentFactorNearZeroRate()
    {
        testcase("computePaymentFactor: near-zero rate, amendment disabled vs enabled");
        using namespace jtx;
        using namespace xrpl::detail;

        Number const r = loanPeriodicRate(TenthBips32{1}, 600);
        std::uint32_t const n = 3;

        // Independent reference: expand F(r,3) = r*(1+r)^3/((1+r)^3-1)
        // algebraically for n=3, dividing numerator and denominator by r:
        //   F(r,3) = (1 + 3r + 3r^2 + r^3) / (3 + 3r + r^2)
        // No power(), no binomial series — pure polynomial arithmetic in
        // Number.
        Number const reference = (1 + 3 * r + 3 * r * r + r * r * r) / (3 + 3 * r + r * r);

        // Pre-fix: closed form power(1+r, n) - 1 suffers catastrophic
        // cancellation when r*n ~ 5.7e-10.
        Env const envBug{*this, testableAmendments() - fixCleanup3_2_0};
        Number const buggyFactor = computePaymentFactor(envBug.current()->rules(), r, n);

        // Post-fix: hybrid binomial path avoids cancellation.
        Env const envFix{*this};
        Number const correctFactor = computePaymentFactor(envFix.current()->rules(), r, n);

        // The amendment must change the computed factor in this regime.
        BEAST_EXPECT(buggyFactor != correctFactor);

        // The fixed factor must agree with the polynomial reference to
        // within a few ULPs of Number's 19-digit precision.
        BEAST_EXPECT(abs(correctFactor - reference) < Number(1, -15));

        // The buggy factor must diverge from the reference by a measurable
        // amount — empirically ~1e-10 in this regime.
        BEAST_EXPECT(abs(buggyFactor - reference) > Number(1, -12));
    }

    void
    testComputeOverpaymentComponents()
    {
        testcase("computeOverpaymentComponents");
        using namespace jtx;
        using namespace xrpl::detail;

        Account const issuer{"issuer"};
        PrettyAsset const iou = issuer["IOU"];
        int32_t const loanScale = 1;
        auto const overpayment = Number{1'000};
        auto const overpaymentInterestRate = TenthBips32{10'000};  // 10%
        auto const overpaymentFeeRate = TenthBips32{50'000};       // 50%
        auto const managementFeeRate = TenthBips16{10'000};        // 10%

        auto const expectedOverpaymentFee = Number{500};            // 50% of 1,000
        auto const expectedOverpaymentInterestGross = Number{100};  // 10% of 1,000
        auto const expectedOverpaymentInterestNet = Number{90};     // 100 - 10% of 100
        auto const expectedOverpaymentManagementFee = Number{10};   // 10% of 100
        auto const expectedPrincipalPortion = Number{400};          // 1,000 - 100 - 500

        Env const env{*this};
        auto const components = xrpl::detail::computeOverpaymentComponents(
            env.current()->rules(),
            iou,
            loanScale,
            overpayment,
            overpaymentInterestRate,
            overpaymentFeeRate,
            managementFeeRate);

        BEAST_EXPECT(components.untrackedManagementFee == expectedOverpaymentFee);

        BEAST_EXPECT(components.untrackedInterest == expectedOverpaymentInterestNet);

        BEAST_EXPECT(components.trackedInterestPart() == expectedOverpaymentInterestNet);

        BEAST_EXPECT(components.trackedManagementFeeDelta == expectedOverpaymentManagementFee);
        BEAST_EXPECT(components.trackedPrincipalDelta == expectedPrincipalPortion);
        BEAST_EXPECT(
            components.trackedManagementFeeDelta + components.untrackedInterest ==
            expectedOverpaymentInterestGross);

        BEAST_EXPECT(
            components.trackedManagementFeeDelta + components.untrackedInterest +
                components.trackedPrincipalDelta + components.untrackedManagementFee ==
            overpayment);
    }

    void
    testComputeInterestAndFeeParts()
    {
        using namespace jtx;
        using namespace xrpl::detail;

        struct TestCase
        {
            std::string name;
            Number interest;
            TenthBips16 managementFeeRate;
            Number expectedInterestPart;
            Number expectedFeePart;
        };

        Account const issuer{"issuer"};
        PrettyAsset const iou = issuer["IOU"];
        std::int32_t const loanScale = 1;

        auto const testCases = std::vector<TestCase>{
            {.name = "Zero interest",
             .interest = Number{0},
             .managementFeeRate = TenthBips16{10'000},
             .expectedInterestPart = Number{0},
             .expectedFeePart = Number{0}},
            {.name = "Zero fee rate",
             .interest = Number{1'000},
             .managementFeeRate = TenthBips16{0},
             .expectedInterestPart = Number{1'000},
             .expectedFeePart = Number{0}},
            {.name = "10% fee rate",
             .interest = Number{1'000},
             .managementFeeRate = TenthBips16{10'000},
             .expectedInterestPart = Number{900},
             .expectedFeePart = Number{100}},
        };

        for (auto const& tc : testCases)
        {
            testcase("computeInterestAndFeeParts: " + tc.name);

            auto const [computedInterestPart, computedFeePart] =
                computeInterestAndFeeParts(iou, tc.interest, tc.managementFeeRate, loanScale);
            BEAST_EXPECTS(
                computedInterestPart == tc.expectedInterestPart,
                "Interest part mismatch: expected " + to_string(tc.expectedInterestPart) +
                    ", got " + to_string(computedInterestPart));
            BEAST_EXPECTS(
                computedFeePart == tc.expectedFeePart,
                "Fee part mismatch: expected " + to_string(tc.expectedFeePart) + ", got " +
                    to_string(computedFeePart));
        }
    }

    void
    testLoanLatePaymentInterest()
    {
        using namespace jtx;
        using namespace xrpl::detail;
        struct TestCase
        {
            std::string name;
            Number principalOutstanding;
            TenthBips32 lateInterestRate;
            NetClock::time_point parentCloseTime;
            std::uint32_t nextPaymentDueDate;
            Number expectedLateInterest;
        };

        auto const testCases = std::vector<TestCase>{
            {
                .name = "On-time payment",
                .principalOutstanding = Number{1'000},
                .lateInterestRate = TenthBips32{10'000},  // 10%
                .parentCloseTime = NetClock::time_point{NetClock::duration{3'000}},
                .nextPaymentDueDate = 3'000,
                .expectedLateInterest = Number{0},
            },
            {
                .name = "Early payment",
                .principalOutstanding = Number{1'000},
                .lateInterestRate = TenthBips32{10'000},  // 10%
                .parentCloseTime = NetClock::time_point{NetClock::duration{3'000}},
                .nextPaymentDueDate = 4'000,
                .expectedLateInterest = Number{0},
            },
            {
                .name = "No principal outstanding",
                .principalOutstanding = Number{0},
                .lateInterestRate = TenthBips32{10'000},  // 10%
                .parentCloseTime = NetClock::time_point{NetClock::duration{3'000}},
                .nextPaymentDueDate = 2'000,
                .expectedLateInterest = Number{0},
            },
            {
                .name = "No late interest rate",
                .principalOutstanding = Number{1'000},
                .lateInterestRate = TenthBips32{0},  // 0%
                .parentCloseTime = NetClock::time_point{NetClock::duration{3'000}},
                .nextPaymentDueDate = 2'000,
                .expectedLateInterest = Number{0},
            },
            {
                .name = "Late payment",
                .principalOutstanding = Number{1'000},
                .lateInterestRate = TenthBips32{100'000},  // 100%
                .parentCloseTime = NetClock::time_point{NetClock::duration{3'000}},
                .nextPaymentDueDate = 2'000,
                .expectedLateInterest = Number{317097919837645865, -19},  // from calc
            },
        };

        for (auto const& tc : testCases)
        {
            testcase("loanLatePaymentInterest: " + tc.name);

            auto const computedLateInterest = loanLatePaymentInterest(
                tc.principalOutstanding,
                tc.lateInterestRate,
                tc.parentCloseTime,
                tc.nextPaymentDueDate);
            BEAST_EXPECTS(
                computedLateInterest == tc.expectedLateInterest,
                "Late interest mismatch: expected " + to_string(tc.expectedLateInterest) +
                    ", got " + to_string(computedLateInterest));
        }
    }

    void
    testLoanAccruedInterest()
    {
        using namespace jtx;
        using namespace xrpl::detail;
        struct TestCase
        {
            std::string name;
            Number principalOutstanding;
            Number periodicRate;
            NetClock::time_point parentCloseTime;
            std::uint32_t startDate;
            std::uint32_t prevPaymentDate;
            std::uint32_t paymentInterval;
            Number expectedAccruedInterest;
        };

        auto const testCases = std::vector<TestCase>{
            {
                .name = "Zero principal outstanding",
                .principalOutstanding = Number{0},
                .periodicRate = Number{5, -2},
                .parentCloseTime = NetClock::time_point{NetClock::duration{3'000}},
                .startDate = 2'000,
                .prevPaymentDate = 2'500,
                .paymentInterval = 30 * 24 * 60 * 60,
                .expectedAccruedInterest = Number{0},
            },
            {
                .name = "Before start date",
                .principalOutstanding = Number{1'000},
                .periodicRate = Number{5, -2},
                .parentCloseTime = NetClock::time_point{NetClock::duration{1'000}},
                .startDate = 2'000,
                .prevPaymentDate = 1'500,
                .paymentInterval = 30 * 24 * 60 * 60,
                .expectedAccruedInterest = Number{0},
            },
            {
                .name = "Zero periodic rate",
                .principalOutstanding = Number{1'000},
                .periodicRate = Number{0},
                .parentCloseTime = NetClock::time_point{NetClock::duration{3'000}},
                .startDate = 2'000,
                .prevPaymentDate = 2'500,
                .paymentInterval = 30 * 24 * 60 * 60,
                .expectedAccruedInterest = Number{0},
            },
            {
                .name = "Zero payment interval",
                .principalOutstanding = Number{1'000},
                .periodicRate = Number{5, -2},
                .parentCloseTime = NetClock::time_point{NetClock::duration{3'000}},
                .startDate = 2'000,
                .prevPaymentDate = 2'500,
                .paymentInterval = 0,
                .expectedAccruedInterest = Number{0},
            },
            {
                .name = "Standard case",
                .principalOutstanding = Number{1'000},
                .periodicRate = Number{5, -2},
                .parentCloseTime = NetClock::time_point{NetClock::duration{3'000}},
                .startDate = 1'000,
                .prevPaymentDate = 2'000,
                .paymentInterval = 30 * 24 * 60 * 60,
                .expectedAccruedInterest = Number{1929012345679012346, -20},  // from calc
            },
        };

        for (auto const& tc : testCases)
        {
            testcase("loanAccruedInterest: " + tc.name);

            auto const computedAccruedInterest = loanAccruedInterest(
                tc.principalOutstanding,
                tc.periodicRate,
                tc.parentCloseTime,
                tc.startDate,
                tc.prevPaymentDate,
                tc.paymentInterval);
            BEAST_EXPECTS(
                computedAccruedInterest == tc.expectedAccruedInterest,
                "Accrued interest mismatch: expected " + to_string(tc.expectedAccruedInterest) +
                    ", got " + to_string(computedAccruedInterest));
        }
    }

    // This test overlaps with testLoanAccruedInterest, the test cases only
    // exercise the computeFullPaymentInterest parts unique to it.
    void
    testComputeFullPaymentInterest()
    {
        using namespace jtx;
        using namespace xrpl::detail;

        struct TestCase
        {
            std::string name;
            Number rawPrincipalOutstanding;
            Number periodicRate;
            NetClock::time_point parentCloseTime;
            std::uint32_t paymentInterval;
            std::uint32_t prevPaymentDate;
            std::uint32_t startDate;
            TenthBips32 closeInterestRate;
            Number expectedFullPaymentInterest;
        };

        auto const testCases = std::vector<TestCase>{
            {
                .name = "Zero principal outstanding",
                .rawPrincipalOutstanding = Number{0},
                .periodicRate = Number{5, -2},
                .parentCloseTime = NetClock::time_point{NetClock::duration{3'000}},
                .paymentInterval = 30 * 24 * 60 * 60,
                .prevPaymentDate = 2'000,
                .startDate = 1'000,
                .closeInterestRate = TenthBips32{10'000},
                .expectedFullPaymentInterest = Number{0},
            },
            {
                .name = "Zero close interest rate",
                .rawPrincipalOutstanding = Number{1'000},
                .periodicRate = Number{5, -2},
                .parentCloseTime = NetClock::time_point{NetClock::duration{3'000}},
                .paymentInterval = 30 * 24 * 60 * 60,
                .prevPaymentDate = 2'000,
                .startDate = 1'000,
                .closeInterestRate = TenthBips32{0},
                .expectedFullPaymentInterest = Number{1929012345679012346, -20},  // from calc
            },
            {
                .name = "Standard case",
                .rawPrincipalOutstanding = Number{1'000},
                .periodicRate = Number{5, -2},
                .parentCloseTime = NetClock::time_point{NetClock::duration{3'000}},
                .paymentInterval = 30 * 24 * 60 * 60,
                .prevPaymentDate = 2'000,
                .startDate = 1'000,
                .closeInterestRate = TenthBips32{10'000},
                .expectedFullPaymentInterest = Number{1000192901234567901, -16},  // from calc
            },
        };

        for (auto const& tc : testCases)
        {
            testcase("computeFullPaymentInterest: " + tc.name);

            auto const computedFullPaymentInterest = computeFullPaymentInterest(
                tc.rawPrincipalOutstanding,
                tc.periodicRate,
                tc.parentCloseTime,
                tc.paymentInterval,
                tc.prevPaymentDate,
                tc.startDate,
                tc.closeInterestRate);
            BEAST_EXPECTS(
                computedFullPaymentInterest == tc.expectedFullPaymentInterest,
                "Full payment interest mismatch: expected " +
                    to_string(tc.expectedFullPaymentInterest) + ", got " +
                    to_string(computedFullPaymentInterest));
        }
    }

    void
    testTryOverpaymentNoInterestNoFee()
    {
        // This test ensures that overpayment with no interest works correctly.
        testcase("tryOverpayment - No Interest No Fee");

        using namespace jtx;
        using namespace xrpl::detail;

        Env const env{*this};
        Account const issuer{"issuer"};
        PrettyAsset const asset = issuer["USD"];
        std::int32_t const loanScale = -5;
        TenthBips16 const managementFeeRate{0};  // 0%
        TenthBips32 const loanInterestRate{0};   // 0%
        Number const loanPrincipal{1'000};
        std::uint32_t const paymentInterval = 30 * 24 * 60 * 60;
        std::uint32_t const paymentsRemaining = 10;
        auto const periodicRate = loanPeriodicRate(loanInterestRate, paymentInterval);
        Number const overpaymentAmount{50};

        auto const overpaymentComponents = computeOverpaymentComponents(
            env.current()->rules(),
            asset,
            loanScale,
            overpaymentAmount,
            TenthBips32(0),
            TenthBips32(0),
            managementFeeRate);

        auto const loanProperties = computeLoanProperties(
            env.current()->rules(),
            asset,
            loanPrincipal,
            loanInterestRate,
            paymentInterval,
            paymentsRemaining,
            managementFeeRate,
            loanScale);

        auto const ret = tryOverpayment(
            env.current()->rules(),
            asset,
            loanScale,
            overpaymentComponents,
            loanProperties.loanState,
            loanProperties.periodicPayment,
            periodicRate,
            paymentsRemaining,
            managementFeeRate,
            env.journal);

        BEAST_EXPECT(ret);

        auto const& [actualPaymentParts, newLoanProperties] = *ret;
        auto const& newState = newLoanProperties.loanState;

        // =========== VALIDATE PAYMENT PARTS ===========
        BEAST_EXPECTS(
            actualPaymentParts.valueChange == 0,
            " valueChange mismatch: expected 0, got " + to_string(actualPaymentParts.valueChange));

        BEAST_EXPECTS(
            actualPaymentParts.feePaid == 0,
            " feePaid mismatch: expected 0, got " + to_string(actualPaymentParts.feePaid));

        BEAST_EXPECTS(
            actualPaymentParts.interestPaid == 0,
            " interestPaid mismatch: expected 0, got " +
                to_string(actualPaymentParts.interestPaid));

        BEAST_EXPECTS(
            actualPaymentParts.principalPaid == overpaymentAmount,
            " principalPaid mismatch: expected " + to_string(overpaymentAmount) + ", got " +
                to_string(actualPaymentParts.principalPaid));

        // =========== VALIDATE STATE CHANGES ===========
        BEAST_EXPECTS(
            loanProperties.loanState.interestDue - newState.interestDue == 0,
            " interest change mismatch: expected 0, got " +
                to_string(loanProperties.loanState.interestDue - newState.interestDue));

        BEAST_EXPECTS(
            loanProperties.loanState.managementFeeDue - newState.managementFeeDue == 0,
            " management fee change mismatch: expected 0, got " +
                to_string(loanProperties.loanState.managementFeeDue - newState.managementFeeDue));

        BEAST_EXPECTS(
            actualPaymentParts.principalPaid ==
                loanProperties.loanState.principalOutstanding - newState.principalOutstanding,
            " principalPaid mismatch: expected " +
                to_string(
                    loanProperties.loanState.principalOutstanding - newState.principalOutstanding) +
                ", got " + to_string(actualPaymentParts.principalPaid));
    }

    void
    testTryOverpaymentNoInterestOverpaymentFee()
    {
        testcase("tryOverpayment - No Interest With Overpayment Fee");

        using namespace jtx;
        using namespace xrpl::detail;

        Env const env{*this};
        Account const issuer{"issuer"};
        PrettyAsset const asset = issuer["USD"];
        std::int32_t const loanScale = -5;
        TenthBips16 const managementFeeRate{0};  // 0%
        TenthBips32 const loanInterestRate{0};   // 0%
        Number const loanPrincipal{1'000};
        std::uint32_t const paymentInterval = 30 * 24 * 60 * 60;
        std::uint32_t const paymentsRemaining = 10;
        auto const periodicRate = loanPeriodicRate(loanInterestRate, paymentInterval);

        auto const overpaymentComponents = computeOverpaymentComponents(
            env.current()->rules(),
            asset,
            loanScale,
            Number{50, 0},
            TenthBips32(0),
            TenthBips32(10'000),  // 10% overpayment fee
            managementFeeRate);

        auto const loanProperties = computeLoanProperties(
            env.current()->rules(),
            asset,
            loanPrincipal,
            loanInterestRate,
            paymentInterval,
            paymentsRemaining,
            managementFeeRate,
            loanScale);

        auto const ret = tryOverpayment(
            env.current()->rules(),
            asset,
            loanScale,
            overpaymentComponents,
            loanProperties.loanState,
            loanProperties.periodicPayment,
            periodicRate,
            paymentsRemaining,
            managementFeeRate,
            env.journal);

        BEAST_EXPECT(ret);

        auto const& [actualPaymentParts, newLoanProperties] = *ret;
        auto const& newState = newLoanProperties.loanState;

        // =========== VALIDATE PAYMENT PARTS ===========
        BEAST_EXPECTS(
            actualPaymentParts.valueChange == 0,
            " valueChange mismatch: expected 0, got " + to_string(actualPaymentParts.valueChange));

        BEAST_EXPECTS(
            actualPaymentParts.feePaid == 5,
            " feePaid mismatch: expected 5, got " + to_string(actualPaymentParts.feePaid));

        BEAST_EXPECTS(
            actualPaymentParts.principalPaid == 45,
            " principalPaid mismatch: expected 45, got `" +
                to_string(actualPaymentParts.principalPaid));

        BEAST_EXPECTS(
            actualPaymentParts.interestPaid == 0,
            " interestPaid mismatch: expected 0, got " +
                to_string(actualPaymentParts.interestPaid));

        // =========== VALIDATE STATE CHANGES ===========
        // With no Loan interest, interest outstanding should not change
        BEAST_EXPECTS(
            loanProperties.loanState.interestDue - newState.interestDue == 0,
            " interest change mismatch: expected 0, got " +
                to_string(loanProperties.loanState.interestDue - newState.interestDue));

        // With no Loan management fee, management fee due should not change
        BEAST_EXPECTS(
            loanProperties.loanState.managementFeeDue - newState.managementFeeDue == 0,
            " management fee change mismatch: expected 0, got " +
                to_string(loanProperties.loanState.managementFeeDue - newState.managementFeeDue));

        BEAST_EXPECTS(
            actualPaymentParts.principalPaid ==
                loanProperties.loanState.principalOutstanding - newState.principalOutstanding,
            " principalPaid mismatch: expected " +
                to_string(
                    loanProperties.loanState.principalOutstanding - newState.principalOutstanding) +
                ", got " + to_string(actualPaymentParts.principalPaid));
    }

    void
    testTryOverpaymentLoanInterestNoOverpaymentFees()
    {
        testcase("tryOverpayment - Loan Interest, No Overpayment Fees");

        using namespace jtx;
        using namespace xrpl::detail;

        Env const env{*this};
        Account const issuer{"issuer"};
        PrettyAsset const asset = issuer["USD"];
        std::int32_t const loanScale = -5;
        TenthBips16 const managementFeeRate{0};      // 0%
        TenthBips32 const loanInterestRate{10'000};  // 10%
        Number const loanPrincipal{1'000};
        std::uint32_t const paymentInterval = 30 * 24 * 60 * 60;
        std::uint32_t const paymentsRemaining = 10;
        auto const periodicRate = loanPeriodicRate(loanInterestRate, paymentInterval);

        auto const overpaymentComponents = computeOverpaymentComponents(
            env.current()->rules(),
            asset,
            loanScale,
            Number{50, 0},
            TenthBips32(0),  // no overpayment interest
            TenthBips32(0),  // 0% overpayment fee
            managementFeeRate);

        auto const loanProperties = computeLoanProperties(
            env.current()->rules(),
            asset,
            loanPrincipal,
            loanInterestRate,
            paymentInterval,
            paymentsRemaining,
            managementFeeRate,
            loanScale);

        auto const ret = tryOverpayment(
            env.current()->rules(),
            asset,
            loanScale,
            overpaymentComponents,
            loanProperties.loanState,
            loanProperties.periodicPayment,
            periodicRate,
            paymentsRemaining,
            managementFeeRate,
            env.journal);

        BEAST_EXPECT(ret);

        auto const& [actualPaymentParts, newLoanProperties] = *ret;
        auto const& newState = newLoanProperties.loanState;

        // =========== VALIDATE PAYMENT PARTS ===========
        // with no overpayment interest portion, value change should equal
        // interest decrease
        BEAST_EXPECTS(
            (actualPaymentParts.valueChange == Number{-228802, -5}),
            " valueChange mismatch: expected " + to_string(Number{-228802, -5}) + ", got " +
                to_string(actualPaymentParts.valueChange));

        // with no fee portion, fee paid should be zero
        BEAST_EXPECTS(
            actualPaymentParts.feePaid == 0,
            " feePaid mismatch: expected 0, got " + to_string(actualPaymentParts.feePaid));

        BEAST_EXPECTS(
            actualPaymentParts.principalPaid == 50,
            " principalPaid mismatch: expected 50, got `" +
                to_string(actualPaymentParts.principalPaid));

        // with no interest portion, interest paid should be zero
        BEAST_EXPECTS(
            actualPaymentParts.interestPaid == 0,
            " interestPaid mismatch: expected 0, got " +
                to_string(actualPaymentParts.interestPaid));

        // =========== VALIDATE STATE CHANGES ===========
        BEAST_EXPECTS(
            actualPaymentParts.principalPaid ==
                loanProperties.loanState.principalOutstanding - newState.principalOutstanding,
            " principalPaid mismatch: expected " +
                to_string(
                    loanProperties.loanState.principalOutstanding - newState.principalOutstanding) +
                ", got " + to_string(actualPaymentParts.principalPaid));

        BEAST_EXPECTS(
            actualPaymentParts.valueChange ==
                newState.interestDue - loanProperties.loanState.interestDue,
            " valueChange mismatch: expected " +
                to_string(newState.interestDue - loanProperties.loanState.interestDue) + ", got " +
                to_string(actualPaymentParts.valueChange));

        // With no Loan management fee, management fee due should not change
        BEAST_EXPECTS(
            loanProperties.loanState.managementFeeDue - newState.managementFeeDue == 0,
            " management fee change mismatch: expected 0, got " +
                to_string(loanProperties.loanState.managementFeeDue - newState.managementFeeDue));
    }

    void
    testTryOverpaymentLoanInterestOverpaymentInterest()
    {
        testcase("tryOverpayment - Loan Interest, Overpayment Interest, No Fee");

        using namespace jtx;
        using namespace xrpl::detail;

        Env const env{*this};
        Account const issuer{"issuer"};
        PrettyAsset const asset = issuer["USD"];
        std::int32_t const loanScale = -5;
        TenthBips16 const managementFeeRate{0};      // 0%
        TenthBips32 const loanInterestRate{10'000};  // 10%
        Number const loanPrincipal{1'000};
        std::uint32_t const paymentInterval = 30 * 24 * 60 * 60;
        std::uint32_t const paymentsRemaining = 10;
        auto const periodicRate = loanPeriodicRate(loanInterestRate, paymentInterval);

        auto const overpaymentComponents = computeOverpaymentComponents(
            env.current()->rules(),
            asset,
            loanScale,
            Number{50, 0},
            TenthBips32(10'000),  // 10% overpayment interest
            TenthBips32(0),       // 0% overpayment fee
            managementFeeRate);

        auto const loanProperties = computeLoanProperties(
            env.current()->rules(),
            asset,
            loanPrincipal,
            loanInterestRate,
            paymentInterval,
            paymentsRemaining,
            managementFeeRate,
            loanScale);

        auto const ret = tryOverpayment(
            env.current()->rules(),
            asset,
            loanScale,
            overpaymentComponents,
            loanProperties.loanState,
            loanProperties.periodicPayment,
            periodicRate,
            paymentsRemaining,
            managementFeeRate,
            env.journal);

        BEAST_EXPECT(ret);

        auto const& [actualPaymentParts, newLoanProperties] = *ret;
        auto const& newState = newLoanProperties.loanState;

        // =========== VALIDATE PAYMENT PARTS ===========
        // with overpayment interest portion, interest paid should be 5
        BEAST_EXPECTS(
            actualPaymentParts.interestPaid == 5,
            " interestPaid mismatch: expected 5, got " +
                to_string(actualPaymentParts.interestPaid));

        // With overpayment interest portion, value change should equal the
        // interest decrease plus overpayment interest portion
        BEAST_EXPECTS(
            (actualPaymentParts.valueChange ==
             Number{-205922, -5} + actualPaymentParts.interestPaid),
            " valueChange mismatch: expected " +
                to_string(actualPaymentParts.valueChange - actualPaymentParts.interestPaid) +
                ", got " + to_string(actualPaymentParts.valueChange));

        // with no fee portion, fee paid should be zero
        BEAST_EXPECTS(
            actualPaymentParts.feePaid == 0,
            " feePaid mismatch: expected 0, got " + to_string(actualPaymentParts.feePaid));

        BEAST_EXPECTS(
            actualPaymentParts.principalPaid == 45,
            " principalPaid mismatch: expected 45, got `" +
                to_string(actualPaymentParts.principalPaid));

        // =========== VALIDATE STATE CHANGES ===========
        BEAST_EXPECTS(
            actualPaymentParts.principalPaid ==
                loanProperties.loanState.principalOutstanding - newState.principalOutstanding,
            " principalPaid mismatch: expected " +
                to_string(
                    loanProperties.loanState.principalOutstanding - newState.principalOutstanding) +
                ", got " + to_string(actualPaymentParts.principalPaid));

        // The change in interest is equal to the value change sans the
        // overpayment interest
        BEAST_EXPECTS(
            actualPaymentParts.valueChange - actualPaymentParts.interestPaid ==
                newState.interestDue - loanProperties.loanState.interestDue,
            " valueChange mismatch: expected " +
                to_string(
                    newState.interestDue - loanProperties.loanState.interestDue +
                    actualPaymentParts.interestPaid) +
                ", got " + to_string(actualPaymentParts.valueChange));

        // With no Loan management fee, management fee due should not change
        BEAST_EXPECTS(
            loanProperties.loanState.managementFeeDue - newState.managementFeeDue == 0,
            " management fee change mismatch: expected 0, got " +
                to_string(loanProperties.loanState.managementFeeDue - newState.managementFeeDue));
    }

    void
    testTryOverpaymentLoanInterestFeeOverpaymentInterestNoFee()
    {
        testcase(
            "tryOverpayment - Loan Interest and Fee, Overpayment Interest, No "
            "Fee");

        using namespace jtx;
        using namespace xrpl::detail;

        Env const env{*this};
        Account const issuer{"issuer"};
        PrettyAsset const asset = issuer["USD"];
        std::int32_t const loanScale = -5;
        TenthBips16 const managementFeeRate{10'000};  // 10%
        TenthBips32 const loanInterestRate{10'000};   // 10%
        Number const loanPrincipal{1'000};
        std::uint32_t const paymentInterval = 30 * 24 * 60 * 60;
        std::uint32_t const paymentsRemaining = 10;
        auto const periodicRate = loanPeriodicRate(loanInterestRate, paymentInterval);

        auto const overpaymentComponents = computeOverpaymentComponents(
            env.current()->rules(),
            asset,
            loanScale,
            Number{50, 0},
            TenthBips32(10'000),  // 10% overpayment interest
            TenthBips32(0),       // 0% overpayment fee
            managementFeeRate);

        auto const loanProperties = computeLoanProperties(
            env.current()->rules(),
            asset,
            loanPrincipal,
            loanInterestRate,
            paymentInterval,
            paymentsRemaining,
            managementFeeRate,
            loanScale);

        auto const ret = tryOverpayment(
            env.current()->rules(),
            asset,
            loanScale,
            overpaymentComponents,
            loanProperties.loanState,
            loanProperties.periodicPayment,
            periodicRate,
            paymentsRemaining,
            managementFeeRate,
            env.journal);

        BEAST_EXPECT(ret);

        auto const& [actualPaymentParts, newLoanProperties] = *ret;
        auto const& newState = newLoanProperties.loanState;

        // =========== VALIDATE PAYMENT PARTS ===========

        // Since there is loan management fee, the fee is charged against
        // overpayment interest portion first, so interest paid remains 4.5
        BEAST_EXPECTS(
            (actualPaymentParts.interestPaid == Number{45, -1}),
            " interestPaid mismatch: expected 4.5, got " +
                to_string(actualPaymentParts.interestPaid));

        // With overpayment interest portion, value change should equal the
        // interest decrease plus overpayment interest portion
        BEAST_EXPECTS(
            (actualPaymentParts.valueChange ==
             Number{-18533, -4} + actualPaymentParts.interestPaid),
            " valueChange mismatch: expected " +
                to_string(Number{-18533, -4} + actualPaymentParts.interestPaid) + ", got " +
                to_string(actualPaymentParts.valueChange));

        // While there is no overpayment fee, fee paid should equal the
        // management fee charged against the overpayment interest portion
        BEAST_EXPECTS(
            (actualPaymentParts.feePaid == Number{5, -1}),
            " feePaid mismatch: expected 0.5, got " + to_string(actualPaymentParts.feePaid));

        BEAST_EXPECTS(
            actualPaymentParts.principalPaid == 45,
            " principalPaid mismatch: expected 45, got `" +
                to_string(actualPaymentParts.principalPaid));

        // =========== VALIDATE STATE CHANGES ===========
        BEAST_EXPECTS(
            actualPaymentParts.principalPaid ==
                loanProperties.loanState.principalOutstanding - newState.principalOutstanding,
            " principalPaid mismatch: expected " +
                to_string(
                    loanProperties.loanState.principalOutstanding - newState.principalOutstanding) +
                ", got " + to_string(actualPaymentParts.principalPaid));

        // Note that the management fee value change is not captured, as this
        // value is not needed to correctly update the Vault state.
        BEAST_EXPECTS(
            (newState.managementFeeDue - loanProperties.loanState.managementFeeDue ==
             Number{-20592, -5}),
            " management fee change mismatch: expected " + to_string(Number{-20592, -5}) +
                ", got " +
                to_string(newState.managementFeeDue - loanProperties.loanState.managementFeeDue));

        BEAST_EXPECTS(
            actualPaymentParts.valueChange - actualPaymentParts.interestPaid ==
                newState.interestDue - loanProperties.loanState.interestDue,
            " valueChange mismatch: expected " +
                to_string(newState.interestDue - loanProperties.loanState.interestDue) + ", got " +
                to_string(actualPaymentParts.valueChange - actualPaymentParts.interestPaid));
    }

    void
    testTryOverpaymentLoanInterestFeeOverpaymentInterestFee()
    {
        testcase("tryOverpayment - Loan Interest, Fee, Overpayment Interest, Fee");

        using namespace jtx;
        using namespace xrpl::detail;

        Account const issuer{"issuer"};
        PrettyAsset const asset = issuer["USD"];
        std::int32_t const loanScale = -5;
        TenthBips16 const managementFeeRate{10'000};  // 10%
        TenthBips32 const loanInterestRate{10'000};   // 10%
        Number const loanPrincipal{1'000};
        std::uint32_t const paymentInterval = 30 * 24 * 60 * 60;
        std::uint32_t const paymentsRemaining = 10;
        auto const periodicRate = loanPeriodicRate(loanInterestRate, paymentInterval);

        Env const env{*this};
        auto const overpaymentComponents = computeOverpaymentComponents(
            env.current()->rules(),
            asset,
            loanScale,
            Number{50, 0},
            TenthBips32(10'000),  // 10% overpayment interest
            TenthBips32(10'000),  // 10% overpayment fee
            managementFeeRate);

        struct Outcome
        {
            LoanPaymentParts parts;
            LoanState oldState;
            LoanState newState;
        };

        // Run tryOverpayment under a given amendment set. At this (non-near-zero)
        // rate computeLoanProperties is amendment-independent, so the loan state
        // is identical across the amendment; only tryOverpayment's fixCleanup3_2_0
        // behaviour (the exact-principal pin and the management-fee re-derivation
        // from that principal) differs.
        auto run = [&](FeatureBitset features) -> std::optional<Outcome> {
            Env const env{*this, features};
            auto const loanProperties = computeLoanProperties(
                env.current()->rules(),
                asset,
                loanPrincipal,
                loanInterestRate,
                paymentInterval,
                paymentsRemaining,
                managementFeeRate,
                loanScale);
            auto const ret = tryOverpayment(
                env.current()->rules(),
                asset,
                loanScale,
                overpaymentComponents,
                loanProperties.loanState,
                loanProperties.periodicPayment,
                periodicRate,
                paymentsRemaining,
                managementFeeRate,
                env.journal);
            if (!BEAST_EXPECT(ret))
                return std::nullopt;
            return Outcome{
                .parts = ret->first,
                .oldState = loanProperties.loanState,
                .newState = ret->second.loanState};
        };

        auto const fixedOpt = run(testableAmendments());
        auto const legacyOpt = run(testableAmendments() - fixCleanup3_2_0);
        if (!fixedOpt || !legacyOpt)
        {
            BEAST_EXPECT(fixedOpt.has_value());
            BEAST_EXPECT(legacyOpt.has_value());
            return;
        }
        Outcome const& fixed = *fixedOpt;
        Outcome const& legacy = *legacyOpt;

        // Components that the amendment does not change. The management fee is
        // charged against the overpayment interest portion first, so interest
        // paid stays 4.5 and fee paid 5.5; the principal repaid is 40 in both.
        auto checkCommon = [&](Outcome const& o, char const* tag) {
            BEAST_EXPECTS(
                (o.parts.interestPaid == Number{45, -1}),
                std::string(tag) + " interestPaid " + to_string(o.parts.interestPaid));
            BEAST_EXPECTS(
                (o.parts.feePaid == Number{55, -1}),
                std::string(tag) + " feePaid " + to_string(o.parts.feePaid));
            BEAST_EXPECTS(
                o.parts.principalPaid == 40,
                std::string(tag) + " principalPaid " + to_string(o.parts.principalPaid));
            BEAST_EXPECT(
                o.parts.principalPaid ==
                o.oldState.principalOutstanding - o.newState.principalOutstanding);
            // v = p + i + m identity: the non-interest part of valueChange equals
            // the interest-due change.
            BEAST_EXPECT(
                o.parts.valueChange - o.parts.interestPaid ==
                o.newState.interestDue - o.oldState.interestDue);
        };
        checkCommon(fixed, "fixed");
        checkCommon(legacy, "legacy");

        // With fixCleanup3_2_0 the management fee is re-derived from the exact
        // principal; without it, from the one-scale-unit-high round-trip
        // principal. So the management fee outstanding (and hence the value
        // change, via v = p + i + m) differ by exactly one scale-unit (1e-5 at
        // loanScale -5) between the two paths.
        BEAST_EXPECT((fixed.parts.valueChange == Number{-164738, -5} + fixed.parts.interestPaid));
        BEAST_EXPECT(
            (fixed.newState.managementFeeDue - fixed.oldState.managementFeeDue ==
             Number{-18303, -5}));
        BEAST_EXPECT((legacy.parts.valueChange == Number{-164737, -5} + legacy.parts.interestPaid));
        BEAST_EXPECT(
            (legacy.newState.managementFeeDue - legacy.oldState.managementFeeDue ==
             Number{-18304, -5}));
    }

public:
    void
    testCanApplyToBrokerCover()
    {
        using namespace jtx;

        Account const issuer{"issuer"};
        PrettyAsset const iou = issuer["IOU"];

        // sfCoverAvailable = Number{10} on an IOU → STAmount exponent = -14,
        // so coverScale = -14.  The ULP boundary is 5e-15; anything below
        // that rounds to zero at cover scale.  Number{1,-16} = 1e-16 is our
        // representative sub-ULP probe.
        struct TestCase
        {
            std::string name;
            Number coverAvailable;
            STAmount amount;
            TER expected;
        };

        auto const testCases = std::vector<TestCase>{
            {
                .name = "Zero amount",
                .coverAvailable = Number{10},
                .amount = STAmount{iou, Number{0}},
                .expected = tecPRECISION_LOSS,
            },
            {
                .name = "Rounds to zero at cover scale",
                .coverAvailable = Number{10},
                .amount = STAmount{iou, Number{1, -16}},
                .expected = tecPRECISION_LOSS,
            },
            {
                .name = "Zero coverAvailable, whole-unit amount",
                // coverScale = 0 (zero STAmount exponent); 1 IOU is not
                // zero at integer scale → tesSUCCESS.
                .coverAvailable = Number{0},
                .amount = STAmount{iou, Number{1}},
                .expected = tesSUCCESS,
            },
            {
                .name = "Supra-ULP amount",
                .coverAvailable = Number{10},
                .amount = STAmount{iou, Number{1, -13}},
                .expected = tesSUCCESS,
            },
        };

        Env const env{*this};

        for (auto const& tc : testCases)
        {
            testcase("canApplyToBrokerCover: " + tc.name);
            auto sle = std::make_shared<SLE>(ltLOAN_BROKER, uint256{1u});
            sle->at(sfCoverAvailable) = tc.coverAvailable;
            BEAST_EXPECT(
                canApplyToBrokerCover(*env.current(), sle, iou, tc.amount, env.journal, "test") ==
                tc.expected);
        }

        // Amendment off → guard is bypassed regardless of amount.
        {
            testcase("canApplyToBrokerCover: amendment disabled");
            Env const envOff{*this, testableAmendments() - fixCleanup3_2_0};
            auto sle = std::make_shared<SLE>(ltLOAN_BROKER, uint256{1u});
            sle->at(sfCoverAvailable) = Number{10};
            BEAST_EXPECT(
                canApplyToBrokerCover(
                    *envOff.current(),
                    sle,
                    iou,
                    STAmount{iou, Number{0}},
                    envOff.journal,
                    "test") == tesSUCCESS);
        }
    }

    void
    run() override
    {
        testTryOverpaymentNoInterestNoFee();
        testTryOverpaymentNoInterestOverpaymentFee();
        testTryOverpaymentLoanInterestNoOverpaymentFees();
        testTryOverpaymentLoanInterestOverpaymentInterest();
        testTryOverpaymentLoanInterestFeeOverpaymentInterestNoFee();
        testTryOverpaymentLoanInterestFeeOverpaymentInterestFee();

        testComputeFullPaymentInterest();
        testLoanAccruedInterest();
        testLoanLatePaymentInterest();
        testLoanPeriodicPayment();
        testLoanPrincipalFromPeriodicPayment();
        testLoanPrincipalFromPeriodicPaymentNearZeroRate();
        testComputePaymentFactor();
        testComputePowerMinusOne();
        testComputePowerMinusOneHybrid();
        testComputeTheoreticalLoanStateNearZeroRate();
        testComputePaymentFactorNearZeroRate();
        testComputeOverpaymentComponents();
        testComputeInterestAndFeeParts();
        testCanApplyToBrokerCover();
    }
};

BEAST_DEFINE_TESTSUITE(LendingHelpers, app, xrpl);

}  // namespace xrpl::test
