#include <xrpl/beast/unit_test/suite.h>
// DO NOT REMOVE
#include <test/jtx.h>
#include <test/jtx/Account.h>
#include <test/jtx/amount.h>
#include <test/jtx/mpt.h>

#include <xrpld/app/misc/LendingHelpers.h>
#include <xrpld/app/misc/LoadFeeTrack.h>
#include <xrpld/app/tx/detail/Batch.h>
#include <xrpld/app/tx/detail/LoanSet.h>

#include <xrpl/beast/xor_shift_engine.h>
#include <xrpl/protocol/SField.h>

#include <string>
#include <vector>

namespace ripple {
namespace test {

class LendingHelpers_test : public beast::unit_test::suite
{
    void
    testComputeRaisedRate()
    {
        using namespace jtx;
        using namespace ripple::detail;
        struct TestCase
        {
            std::string name;
            Number periodicRate;
            std::uint32_t paymentsRemaining;
            Number expectedRaisedRate;
        };

        auto const testCases = std::vector<TestCase>{
            {
                .name = "Zero payments remaining",
                .periodicRate = Number{5, -2},
                .paymentsRemaining = 0,
                .expectedRaisedRate = Number{1},  // (1 + r)^0 = 1
            },
            {
                .name = "One payment remaining",
                .periodicRate = Number{5, -2},
                .paymentsRemaining = 1,
                .expectedRaisedRate = Number{105, -2},
            },  // 1.05^1
            {
                .name = "Multiple payments remaining",
                .periodicRate = Number{5, -2},
                .paymentsRemaining = 3,
                .expectedRaisedRate = Number{1157625, -6},
            },  // 1.05^3
            {
                .name = "Zero periodic rate",
                .periodicRate = Number{0},
                .paymentsRemaining = 5,
                .expectedRaisedRate = Number{1},  // (1 + 0)^5 = 1
            }};

        for (auto const& tc : testCases)
        {
            testcase("computeRaisedRate: " + tc.name);

            auto const computedRaisedRate =
                computeRaisedRate(tc.periodicRate, tc.paymentsRemaining);
            BEAST_EXPECTS(
                computedRaisedRate == tc.expectedRaisedRate,
                "Raised rate mismatch: expected " +
                    to_string(tc.expectedRaisedRate) + ", got " +
                    to_string(computedRaisedRate));
        }
    }

    void
    testComputePaymentFactor()
    {
        using namespace jtx;
        using namespace ripple::detail;
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
                computePaymentFactor(tc.periodicRate, tc.paymentsRemaining);
            BEAST_EXPECTS(
                computedPaymentFactor == tc.expectedPaymentFactor,
                "Payment factor mismatch: expected " +
                    to_string(tc.expectedPaymentFactor) + ", got " +
                    to_string(computedPaymentFactor));
        }
    }

    void
    testLoanPeriodicPayment()
    {
        using namespace jtx;
        using namespace ripple::detail;

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
                .periodicRate =
                    loanPeriodicRate(TenthBips32(100'000), 30 * 24 * 60 * 60),
                .paymentsRemaining = 3,
                .expectedPeriodicPayment =
                    Number{389569066396123265, -15},  // from calc
            },
        };

        for (auto const& tc : testCases)
        {
            testcase("loanPeriodicPayment: " + tc.name);

            auto const computedPeriodicPayment = loanPeriodicPayment(
                tc.principalOutstanding, tc.periodicRate, tc.paymentsRemaining);
            BEAST_EXPECTS(
                computedPeriodicPayment == tc.expectedPeriodicPayment,
                "Periodic payment mismatch: expected " +
                    to_string(tc.expectedPeriodicPayment) + ", got " +
                    to_string(computedPeriodicPayment));
        }
    }

    void
    testLoanPrincipalFromPeriodicPayment()
    {
        using namespace jtx;
        using namespace ripple::detail;

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
                .periodicPayment =
                    Number{389569066396123265, -15},  // from calc
                .periodicRate =
                    loanPeriodicRate(TenthBips32(100'000), 30 * 24 * 60 * 60),
                .paymentsRemaining = 3,
                .expectedPrincipalOutstanding = Number{1'000},
            },
        };

        for (auto const& tc : testCases)
        {
            testcase("loanPrincipalFromPeriodicPayment: " + tc.name);

            auto const computedPrincipalOutstanding =
                loanPrincipalFromPeriodicPayment(
                    tc.periodicPayment, tc.periodicRate, tc.paymentsRemaining);
            BEAST_EXPECTS(
                computedPrincipalOutstanding == tc.expectedPrincipalOutstanding,
                "Principal outstanding mismatch: expected " +
                    to_string(tc.expectedPrincipalOutstanding) + ", got " +
                    to_string(computedPrincipalOutstanding));
        }
    }

    void
    testComputeOverpaymentComponents()
    {
        testcase("computeOverpaymentComponents");
        using namespace jtx;
        using namespace ripple::detail;

        Account const issuer{"issuer"};
        PrettyAsset const IOU = issuer["IOU"];
        int32_t const loanScale = 1;
        auto const overpayment = Number{1'000};
        auto const overpaymentInterestRate = TenthBips32{10'000};  // 10%
        auto const overpaymentFeeRate = TenthBips32{50'000};       // 50%
        auto const managementFeeRate = TenthBips16{10'000};        // 10%

        auto const expectedOverpaymentFee = Number{500};  // 50% of 1,000
        auto const expectedOverpaymentInterestGross =
            Number{100};  // 10% of 1,000
        auto const expectedOverpaymentInterestNet =
            Number{90};  // 100 - 10% of 100
        auto const expectedOverpaymentManagementFee = Number{10};  // 10% of 100
        auto const expectedPrincipalPortion = Number{400};  // 1,000 - 100 - 500

        auto const components = detail::computeOverpaymentComponents(
            IOU,
            loanScale,
            overpayment,
            overpaymentInterestRate,
            overpaymentFeeRate,
            managementFeeRate);

        BEAST_EXPECT(
            components.untrackedManagementFee == expectedOverpaymentFee);

        BEAST_EXPECT(
            components.untrackedInterest == expectedOverpaymentInterestNet);

        BEAST_EXPECT(
            components.trackedInterestPart() == expectedOverpaymentInterestNet);

        BEAST_EXPECT(
            components.trackedManagementFeeDelta ==
            expectedOverpaymentManagementFee);
        BEAST_EXPECT(
            components.trackedPrincipalDelta == expectedPrincipalPortion);
        BEAST_EXPECT(
            components.trackedManagementFeeDelta +
                components.untrackedInterest ==
            expectedOverpaymentInterestGross);

        BEAST_EXPECT(
            components.trackedManagementFeeDelta +
                components.untrackedInterest +
                components.trackedPrincipalDelta +
                components.untrackedManagementFee ==
            overpayment);
    }

    void
    testComputeInterestAndFeeParts()
    {
        using namespace jtx;
        using namespace ripple::detail;

        struct TestCase
        {
            std::string name;
            Number interest;
            TenthBips16 managementFeeRate;
            Number expectedInterestPart;
            Number expectedFeePart;
        };

        Account const issuer{"issuer"};
        PrettyAsset const IOU = issuer["IOU"];
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
                computeInterestAndFeeParts(
                    IOU, tc.interest, tc.managementFeeRate, loanScale);
            BEAST_EXPECTS(
                computedInterestPart == tc.expectedInterestPart,
                "Interest part mismatch: expected " +
                    to_string(tc.expectedInterestPart) + ", got " +
                    to_string(computedInterestPart));
            BEAST_EXPECTS(
                computedFeePart == tc.expectedFeePart,
                "Fee part mismatch: expected " + to_string(tc.expectedFeePart) +
                    ", got " + to_string(computedFeePart));
        }
    }

    void
    testLoanLatePaymentInterest()
    {
        using namespace jtx;
        using namespace ripple::detail;
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
                .parentCloseTime =
                    NetClock::time_point{NetClock::duration{3'000}},
                .nextPaymentDueDate = 3'000,
                .expectedLateInterest = Number{0},
            },
            {
                .name = "Early payment",
                .principalOutstanding = Number{1'000},
                .lateInterestRate = TenthBips32{10'000},  // 10%
                .parentCloseTime =
                    NetClock::time_point{NetClock::duration{3'000}},
                .nextPaymentDueDate = 4'000,
                .expectedLateInterest = Number{0},
            },
            {
                .name = "No principal outstanding",
                .principalOutstanding = Number{0},
                .lateInterestRate = TenthBips32{10'000},  // 10%
                .parentCloseTime =
                    NetClock::time_point{NetClock::duration{3'000}},
                .nextPaymentDueDate = 2'000,
                .expectedLateInterest = Number{0},
            },
            {
                .name = "No late interest rate",
                .principalOutstanding = Number{1'000},
                .lateInterestRate = TenthBips32{0},  // 0%
                .parentCloseTime =
                    NetClock::time_point{NetClock::duration{3'000}},
                .nextPaymentDueDate = 2'000,
                .expectedLateInterest = Number{0},
            },
            {
                .name = "Late payment",
                .principalOutstanding = Number{1'000},
                .lateInterestRate = TenthBips32{100'000},  // 100%
                .parentCloseTime =
                    NetClock::time_point{NetClock::duration{3'000}},
                .nextPaymentDueDate = 2'000,
                .expectedLateInterest =
                    Number{317097919837645865, -19},  // from calc
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
                "Late interest mismatch: expected " +
                    to_string(tc.expectedLateInterest) + ", got " +
                    to_string(computedLateInterest));
        }
    }

    void
    testLoanAccruedInterest()
    {
        using namespace jtx;
        using namespace ripple::detail;
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
                .parentCloseTime =
                    NetClock::time_point{NetClock::duration{3'000}},
                .startDate = 2'000,
                .prevPaymentDate = 2'500,
                .paymentInterval = 30 * 24 * 60 * 60,
                .expectedAccruedInterest = Number{0},
            },
            {
                .name = "Before start date",
                .principalOutstanding = Number{1'000},
                .periodicRate = Number{5, -2},
                .parentCloseTime =
                    NetClock::time_point{NetClock::duration{1'000}},
                .startDate = 2'000,
                .prevPaymentDate = 1'500,
                .paymentInterval = 30 * 24 * 60 * 60,
                .expectedAccruedInterest = Number{0},
            },
            {
                .name = "Zero periodic rate",
                .principalOutstanding = Number{1'000},
                .periodicRate = Number{0},
                .parentCloseTime =
                    NetClock::time_point{NetClock::duration{3'000}},
                .startDate = 2'000,
                .prevPaymentDate = 2'500,
                .paymentInterval = 30 * 24 * 60 * 60,
                .expectedAccruedInterest = Number{0},
            },
            {
                .name = "Zero payment interval",
                .principalOutstanding = Number{1'000},
                .periodicRate = Number{5, -2},
                .parentCloseTime =
                    NetClock::time_point{NetClock::duration{3'000}},
                .startDate = 2'000,
                .prevPaymentDate = 2'500,
                .paymentInterval = 0,
                .expectedAccruedInterest = Number{0},
            },
            {
                .name = "Standard case",
                .principalOutstanding = Number{1'000},
                .periodicRate = Number{5, -2},
                .parentCloseTime =
                    NetClock::time_point{NetClock::duration{3'000}},
                .startDate = 1'000,
                .prevPaymentDate = 2'000,
                .paymentInterval = 30 * 24 * 60 * 60,
                .expectedAccruedInterest =
                    Number{1929012345679012346, -20},  // from calc
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
                "Accrued interest mismatch: expected " +
                    to_string(tc.expectedAccruedInterest) + ", got " +
                    to_string(computedAccruedInterest));
        }
    }

    // This test overlaps with testLoanAccruedInterest, the test cases only
    // exercise the computeFullPaymentInterest parts unique to it.
    void
    testComputeFullPaymentInterest()
    {
        using namespace jtx;
        using namespace ripple::detail;

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
                .parentCloseTime =
                    NetClock::time_point{NetClock::duration{3'000}},
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
                .parentCloseTime =
                    NetClock::time_point{NetClock::duration{3'000}},
                .paymentInterval = 30 * 24 * 60 * 60,
                .prevPaymentDate = 2'000,
                .startDate = 1'000,
                .closeInterestRate = TenthBips32{0},
                .expectedFullPaymentInterest =
                    Number{1929012345679012346, -20},  // from calc
            },
            {
                .name = "Standard case",
                .rawPrincipalOutstanding = Number{1'000},
                .periodicRate = Number{5, -2},
                .parentCloseTime =
                    NetClock::time_point{NetClock::duration{3'000}},
                .paymentInterval = 30 * 24 * 60 * 60,
                .prevPaymentDate = 2'000,
                .startDate = 1'000,
                .closeInterestRate = TenthBips32{10'000},
                .expectedFullPaymentInterest =
                    Number{1000192901234567901, -16},  // from calc
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
        using namespace ripple::detail;

        Env env{*this};
        Account const issuer{"issuer"};
        PrettyAsset const asset = issuer["USD"];
        std::int32_t const loanScale = -5;
        TenthBips16 const managementFeeRate{0};  // 0%
        TenthBips32 const loanInterestRate{0};   // 0%
        Number const loanPrincipal{1'000};
        std::uint32_t const paymentInterval = 30 * 24 * 60 * 60;
        std::uint32_t const paymentsRemaining = 10;
        auto const periodicRate =
            loanPeriodicRate(loanInterestRate, paymentInterval);
        Number const overpaymentAmount{50};

        ExtendedPaymentComponents const overpaymentComponents =
            computeOverpaymentComponents(
                asset,
                loanScale,
                overpaymentAmount,
                TenthBips32(0),
                TenthBips32(0),
                managementFeeRate);

        auto const loanProperites = computeLoanProperties(
            asset,
            loanPrincipal,
            loanInterestRate,
            paymentInterval,
            paymentsRemaining,
            managementFeeRate,
            loanScale);

        Number const periodicPayment = loanProperites.periodicPayment;

        auto const ret = tryOverpayment(
            asset,
            loanScale,
            overpaymentComponents,
            loanProperites.loanState,
            periodicPayment,
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
            " valueChange mismatch: expected 0, got " +
                to_string(actualPaymentParts.valueChange));

        BEAST_EXPECTS(
            actualPaymentParts.feePaid == 0,
            " feePaid mismatch: expected 0, got " +
                to_string(actualPaymentParts.feePaid));

        BEAST_EXPECTS(
            actualPaymentParts.interestPaid == 0,
            " interestPaid mismatch: expected 0, got " +
                to_string(actualPaymentParts.interestPaid));

        BEAST_EXPECTS(
            actualPaymentParts.principalPaid == overpaymentAmount,
            " principalPaid mismatch: expected " +
                to_string(overpaymentAmount) + ", got " +
                to_string(actualPaymentParts.principalPaid));

        // =========== VALIDATE STATE CHANGES ===========
        BEAST_EXPECTS(
            loanProperites.loanState.interestDue - newState.interestDue == 0,
            " interest change mismatch: expected 0, got " +
                to_string(
                    loanProperites.loanState.interestDue -
                    newState.interestDue));

        BEAST_EXPECTS(
            loanProperites.loanState.managementFeeDue -
                    newState.managementFeeDue ==
                0,
            " management fee change mismatch: expected 0, got " +
                to_string(
                    loanProperites.loanState.managementFeeDue -
                    newState.managementFeeDue));

        BEAST_EXPECTS(
            actualPaymentParts.principalPaid ==
                loanProperites.loanState.principalOutstanding -
                    newState.principalOutstanding,
            " principalPaid mismatch: expected " +
                to_string(
                    loanProperites.loanState.principalOutstanding -
                    newState.principalOutstanding) +
                ", got " + to_string(actualPaymentParts.principalPaid));
    }

    void
    testTryOverpaymentNoInterestOverpaymentFee()
    {
        testcase("tryOverpayment - No Interest With Overpayment Fee");

        using namespace jtx;
        using namespace ripple::detail;

        Env env{*this};
        Account const issuer{"issuer"};
        PrettyAsset const asset = issuer["USD"];
        std::int32_t const loanScale = -5;
        TenthBips16 const managementFeeRate{0};  // 0%
        TenthBips32 const loanInterestRate{0};   // 0%
        Number const loanPrincipal{1'000};
        std::uint32_t const paymentInterval = 30 * 24 * 60 * 60;
        std::uint32_t const paymentsRemaining = 10;
        auto const periodicRate =
            loanPeriodicRate(loanInterestRate, paymentInterval);

        ExtendedPaymentComponents const overpaymentComponents =
            computeOverpaymentComponents(
                asset,
                loanScale,
                Number{50, 0},
                TenthBips32(0),
                TenthBips32(10'000),  // 10% overpayment fee
                managementFeeRate);

        auto const loanProperites = computeLoanProperties(
            asset,
            loanPrincipal,
            loanInterestRate,
            paymentInterval,
            paymentsRemaining,
            managementFeeRate,
            loanScale);

        Number const periodicPayment = loanProperites.periodicPayment;

        auto const ret = tryOverpayment(
            asset,
            loanScale,
            overpaymentComponents,
            loanProperites.loanState,
            periodicPayment,
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
            " valueChange mismatch: expected 0, got " +
                to_string(actualPaymentParts.valueChange));

        BEAST_EXPECTS(
            actualPaymentParts.feePaid == 5,
            " feePaid mismatch: expected 5, got " +
                to_string(actualPaymentParts.feePaid));

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
            loanProperites.loanState.interestDue - newState.interestDue == 0,
            " interest change mismatch: expected 0, got " +
                to_string(
                    loanProperites.loanState.interestDue -
                    newState.interestDue));

        // With no Loan management fee, management fee due should not change
        BEAST_EXPECTS(
            loanProperites.loanState.managementFeeDue -
                    newState.managementFeeDue ==
                0,
            " management fee change mismatch: expected 0, got " +
                to_string(
                    loanProperites.loanState.managementFeeDue -
                    newState.managementFeeDue));

        BEAST_EXPECTS(
            actualPaymentParts.principalPaid ==
                loanProperites.loanState.principalOutstanding -
                    newState.principalOutstanding,
            " principalPaid mismatch: expected " +
                to_string(
                    loanProperites.loanState.principalOutstanding -
                    newState.principalOutstanding) +
                ", got " + to_string(actualPaymentParts.principalPaid));
    }

    void
    testTryOverpaymentLoanInterestNoOverpaymentFees()
    {
        testcase("tryOverpayment - Loan Interest, No Overpayment Fees");

        using namespace jtx;
        using namespace ripple::detail;

        Env env{*this};
        Account const issuer{"issuer"};
        PrettyAsset const asset = issuer["USD"];
        std::int32_t const loanScale = -5;
        TenthBips16 const managementFeeRate{0};      // 0%
        TenthBips32 const loanInterestRate{10'000};  // 10%
        Number const loanPrincipal{1'000};
        std::uint32_t const paymentInterval = 30 * 24 * 60 * 60;
        std::uint32_t const paymentsRemaining = 10;
        auto const periodicRate =
            loanPeriodicRate(loanInterestRate, paymentInterval);

        ExtendedPaymentComponents const overpaymentComponents =
            computeOverpaymentComponents(
                asset,
                loanScale,
                Number{50, 0},
                TenthBips32(0),  // no overpayment interest
                TenthBips32(0),  // 0% overpayment fee
                managementFeeRate);

        auto const loanProperites = computeLoanProperties(
            asset,
            loanPrincipal,
            loanInterestRate,
            paymentInterval,
            paymentsRemaining,
            managementFeeRate,
            loanScale);

        Number const periodicPayment = loanProperites.periodicPayment;

        auto const ret = tryOverpayment(
            asset,
            loanScale,
            overpaymentComponents,
            loanProperites.loanState,
            periodicPayment,
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
            " valueChange mismatch: expected " +
                to_string(Number{-228802, -5}) + ", got " +
                to_string(actualPaymentParts.valueChange));

        // with no fee portion, fee paid should be zero
        BEAST_EXPECTS(
            actualPaymentParts.feePaid == 0,
            " feePaid mismatch: expected 0, got " +
                to_string(actualPaymentParts.feePaid));

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
                loanProperites.loanState.principalOutstanding -
                    newState.principalOutstanding,
            " principalPaid mismatch: expected " +
                to_string(
                    loanProperites.loanState.principalOutstanding -
                    newState.principalOutstanding) +
                ", got " + to_string(actualPaymentParts.principalPaid));

        BEAST_EXPECTS(
            actualPaymentParts.valueChange ==
                newState.interestDue - loanProperites.loanState.interestDue,
            " valueChange mismatch: expected " +
                to_string(
                    newState.interestDue -
                    loanProperites.loanState.interestDue) +
                ", got " + to_string(actualPaymentParts.valueChange));

        // With no Loan management fee, management fee due should not change
        BEAST_EXPECTS(
            loanProperites.loanState.managementFeeDue -
                    newState.managementFeeDue ==
                0,
            " management fee change mismatch: expected 0, got " +
                to_string(
                    loanProperites.loanState.managementFeeDue -
                    newState.managementFeeDue));
    }

    void
    testTryOverpaymentLoanInterestOverpaymentInterest()
    {
        testcase(
            "tryOverpayment - Loan Interest, Overpayment Interest, No Fee");

        using namespace jtx;
        using namespace ripple::detail;

        Env env{*this};
        Account const issuer{"issuer"};
        PrettyAsset const asset = issuer["USD"];
        std::int32_t const loanScale = -5;
        TenthBips16 const managementFeeRate{0};      // 0%
        TenthBips32 const loanInterestRate{10'000};  // 10%
        Number const loanPrincipal{1'000};
        std::uint32_t const paymentInterval = 30 * 24 * 60 * 60;
        std::uint32_t const paymentsRemaining = 10;
        auto const periodicRate =
            loanPeriodicRate(loanInterestRate, paymentInterval);

        ExtendedPaymentComponents const overpaymentComponents =
            computeOverpaymentComponents(
                asset,
                loanScale,
                Number{50, 0},
                TenthBips32(10'000),  // 10% overpayment interest
                TenthBips32(0),       // 0% overpayment fee
                managementFeeRate);

        auto const loanProperites = computeLoanProperties(
            asset,
            loanPrincipal,
            loanInterestRate,
            paymentInterval,
            paymentsRemaining,
            managementFeeRate,
            loanScale);

        Number const periodicPayment = loanProperites.periodicPayment;

        auto const ret = tryOverpayment(
            asset,
            loanScale,
            overpaymentComponents,
            loanProperites.loanState,
            periodicPayment,
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
                to_string(
                    actualPaymentParts.valueChange -
                    actualPaymentParts.interestPaid) +
                ", got " + to_string(actualPaymentParts.valueChange));

        // with no fee portion, fee paid should be zero
        BEAST_EXPECTS(
            actualPaymentParts.feePaid == 0,
            " feePaid mismatch: expected 0, got " +
                to_string(actualPaymentParts.feePaid));

        BEAST_EXPECTS(
            actualPaymentParts.principalPaid == 45,
            " principalPaid mismatch: expected 45, got `" +
                to_string(actualPaymentParts.principalPaid));

        // =========== VALIDATE STATE CHANGES ===========
        BEAST_EXPECTS(
            actualPaymentParts.principalPaid ==
                loanProperites.loanState.principalOutstanding -
                    newState.principalOutstanding,
            " principalPaid mismatch: expected " +
                to_string(
                    loanProperites.loanState.principalOutstanding -
                    newState.principalOutstanding) +
                ", got " + to_string(actualPaymentParts.principalPaid));

        // The change in interest is equal to the value change sans the
        // overpayment interest
        BEAST_EXPECTS(
            actualPaymentParts.valueChange - actualPaymentParts.interestPaid ==
                newState.interestDue - loanProperites.loanState.interestDue,
            " valueChange mismatch: expected " +
                to_string(
                    newState.interestDue -
                    loanProperites.loanState.interestDue +
                    actualPaymentParts.interestPaid) +
                ", got " + to_string(actualPaymentParts.valueChange));

        // With no Loan management fee, management fee due should not change
        BEAST_EXPECTS(
            loanProperites.loanState.managementFeeDue -
                    newState.managementFeeDue ==
                0,
            " management fee change mismatch: expected 0, got " +
                to_string(
                    loanProperites.loanState.managementFeeDue -
                    newState.managementFeeDue));
    }

    void
    testTryOverpaymentLoanInterestFeeOverpaymentInterestNoFee()
    {
        testcase(
            "tryOverpayment - Loan Interest and Fee, Overpayment Interest, No "
            "Fee");

        using namespace jtx;
        using namespace ripple::detail;

        Env env{*this};
        Account const issuer{"issuer"};
        PrettyAsset const asset = issuer["USD"];
        std::int32_t const loanScale = -5;
        TenthBips16 const managementFeeRate{10'000};  // 10%
        TenthBips32 const loanInterestRate{10'000};   // 10%
        Number const loanPrincipal{1'000};
        std::uint32_t const paymentInterval = 30 * 24 * 60 * 60;
        std::uint32_t const paymentsRemaining = 10;
        auto const periodicRate =
            loanPeriodicRate(loanInterestRate, paymentInterval);

        ExtendedPaymentComponents const overpaymentComponents =
            computeOverpaymentComponents(
                asset,
                loanScale,
                Number{50, 0},
                TenthBips32(10'000),  // 10% overpayment interest
                TenthBips32(0),       // 0% overpayment fee
                managementFeeRate);

        auto const loanProperites = computeLoanProperties(
            asset,
            loanPrincipal,
            loanInterestRate,
            paymentInterval,
            paymentsRemaining,
            managementFeeRate,
            loanScale);

        Number const periodicPayment = loanProperites.periodicPayment;

        auto const ret = tryOverpayment(
            asset,
            loanScale,
            overpaymentComponents,
            loanProperites.loanState,
            periodicPayment,
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
                to_string(
                    Number{-18533, -4} + actualPaymentParts.interestPaid) +
                ", got " + to_string(actualPaymentParts.valueChange));

        // While there is no overpayment fee, fee paid should equal the
        // management fee charged against the overpayment interest portion
        BEAST_EXPECTS(
            (actualPaymentParts.feePaid == Number{5, -1}),
            " feePaid mismatch: expected 0.5, got " +
                to_string(actualPaymentParts.feePaid));

        BEAST_EXPECTS(
            actualPaymentParts.principalPaid == 45,
            " principalPaid mismatch: expected 45, got `" +
                to_string(actualPaymentParts.principalPaid));

        // =========== VALIDATE STATE CHANGES ===========
        BEAST_EXPECTS(
            actualPaymentParts.principalPaid ==
                loanProperites.loanState.principalOutstanding -
                    newState.principalOutstanding,
            " principalPaid mismatch: expected " +
                to_string(
                    loanProperites.loanState.principalOutstanding -
                    newState.principalOutstanding) +
                ", got " + to_string(actualPaymentParts.principalPaid));

        // Note that the management fee value change is not captured, as this
        // value is not needed to correctly update the Vault state.
        BEAST_EXPECTS(
            (newState.managementFeeDue -
                 loanProperites.loanState.managementFeeDue ==
             Number{-20592, -5}),
            " management fee change mismatch: expected " +
                to_string(Number{-20592, -5}) + ", got " +
                to_string(
                    newState.managementFeeDue -
                    loanProperites.loanState.managementFeeDue));

        BEAST_EXPECTS(
            actualPaymentParts.valueChange - actualPaymentParts.interestPaid ==
                newState.interestDue - loanProperites.loanState.interestDue,
            " valueChange mismatch: expected " +
                to_string(
                    newState.interestDue -
                    loanProperites.loanState.interestDue) +
                ", got " +
                to_string(
                    actualPaymentParts.valueChange -
                    actualPaymentParts.interestPaid));
    }

    void
    testTryOverpaymentLoanInterestFeeOverpaymentInterestFee()
    {
        testcase(
            "tryOverpayment - Loan Interest, Fee, Overpayment Interest, Fee");

        using namespace jtx;
        using namespace ripple::detail;

        Env env{*this};
        Account const issuer{"issuer"};
        PrettyAsset const asset = issuer["USD"];
        std::int32_t const loanScale = -5;
        TenthBips16 const managementFeeRate{10'000};  // 10%
        TenthBips32 const loanInterestRate{10'000};   // 10%
        Number const loanPrincipal{1'000};
        std::uint32_t const paymentInterval = 30 * 24 * 60 * 60;
        std::uint32_t const paymentsRemaining = 10;
        auto const periodicRate =
            loanPeriodicRate(loanInterestRate, paymentInterval);

        ExtendedPaymentComponents const overpaymentComponents =
            computeOverpaymentComponents(
                asset,
                loanScale,
                Number{50, 0},
                TenthBips32(10'000),  // 10% overpayment interest
                TenthBips32(10'000),  // 10% overpayment fee
                managementFeeRate);

        auto const loanProperites = computeLoanProperties(
            asset,
            loanPrincipal,
            loanInterestRate,
            paymentInterval,
            paymentsRemaining,
            managementFeeRate,
            loanScale);

        Number const periodicPayment = loanProperites.periodicPayment;

        auto const ret = tryOverpayment(
            asset,
            loanScale,
            overpaymentComponents,
            loanProperites.loanState,
            periodicPayment,
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
             Number{-164737, -5} + actualPaymentParts.interestPaid),
            " valueChange mismatch: expected " +
                to_string(
                    Number{-164737, -5} + actualPaymentParts.interestPaid) +
                ", got " + to_string(actualPaymentParts.valueChange));

        // While there is no overpayment fee, fee paid should equal the
        // management fee charged against the overpayment interest portion
        BEAST_EXPECTS(
            (actualPaymentParts.feePaid == Number{55, -1}),
            " feePaid mismatch: expected 5.5, got " +
                to_string(actualPaymentParts.feePaid));

        BEAST_EXPECTS(
            actualPaymentParts.principalPaid == 40,
            " principalPaid mismatch: expected 40, got `" +
                to_string(actualPaymentParts.principalPaid));

        // =========== VALIDATE STATE CHANGES ===========

        BEAST_EXPECTS(
            actualPaymentParts.principalPaid ==
                loanProperites.loanState.principalOutstanding -
                    newState.principalOutstanding,
            " principalPaid mismatch: expected " +
                to_string(
                    loanProperites.loanState.principalOutstanding -
                    newState.principalOutstanding) +
                ", got " + to_string(actualPaymentParts.principalPaid));

        // Note that the management fee value change is not captured, as this
        // value is not needed to correctly update the Vault state.
        BEAST_EXPECTS(
            (newState.managementFeeDue -
                 loanProperites.loanState.managementFeeDue ==
             Number{-18304, -5}),
            " management fee change mismatch: expected " +
                to_string(Number{-18304, -5}) + ", got " +
                to_string(
                    newState.managementFeeDue -
                    loanProperites.loanState.managementFeeDue));

        BEAST_EXPECTS(
            actualPaymentParts.valueChange - actualPaymentParts.interestPaid ==
                newState.interestDue - loanProperites.loanState.interestDue,
            " valueChange mismatch: expected " +
                to_string(
                    newState.interestDue -
                    loanProperites.loanState.interestDue) +
                ", got " +
                to_string(
                    actualPaymentParts.valueChange -
                    actualPaymentParts.interestPaid));
    }

public:
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
        testComputeRaisedRate();
        testComputePaymentFactor();
        testComputeOverpaymentComponents();
        testComputeInterestAndFeeParts();
    }
};

BEAST_DEFINE_TESTSUITE(LendingHelpers, app, ripple);

}  // namespace test
}  // namespace ripple
