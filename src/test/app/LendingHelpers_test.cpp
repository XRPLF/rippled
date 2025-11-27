#include <xrpl/beast/unit_test/suite.h>
//
#include <test/jtx.h>
#include <test/jtx/mpt.h>

#include <xrpld/app/misc/LendingHelpers.h>
#include <xrpld/app/misc/LoadFeeTrack.h>
#include <xrpld/app/tx/detail/Batch.h>
#include <xrpld/app/tx/detail/LoanSet.h>

#include <xrpl/beast/xor_shift_engine.h>
#include <xrpl/protocol/SField.h>

#include "test/jtx/amount.h"

namespace ripple {
namespace test {

class LendingHelpers_test : public beast::unit_test::suite
{
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
        auto const overpaymentFeeRate = TenthBips32{10'000};       // 10%
        auto const managementFeeRate = TenthBips16{10'000};        // 10%

        auto const expectedOverpaymentFee = Number{100};  // 10% of 1,000
        auto const expectedOverpaymentInterestGross =
            Number{100};  // 10% of 1,000
        auto const expectedOverpaymentInterestNet =
            Number{90};  // 100 - 10% of 100
        auto const expectedOverpaymentManagementFee = Number{10};  // 10% of
        auto const expectedPrincipalPortion = Number{800};  // 1,000 - 100 - 100

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

public:
    void
    run() override
    {
        testComputeOverpaymentComponents();
    }
};

BEAST_DEFINE_TESTSUITE(LendingHelpers, app, ripple);

}  // namespace test
}  // namespace ripple
