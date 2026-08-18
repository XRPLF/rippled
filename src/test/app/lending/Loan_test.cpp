#include <xrpl/beast/unit_test/global_suites.h>
#include <xrpl/beast/unit_test/suite.h>

#include <algorithm>
#include <array>
#include <string_view>

namespace xrpl::test {

/**
 * Aggregator: running this suite ("Loan") reruns every topical Loan/Lending
 * suite in one invocation. Each member suite below remains independently
 * runnable under its own name. Declared manual so an unfiltered full test
 * run doesn't execute every case twice.
 */
class Loan_test : public beast::unit_test::Suite
{
    void
    run() override
    {
        static constexpr std::array<std::string_view, 13> kMembers{
            "LendingHelpers",
            "LoanBroker",
            "LoanCashBasis",
            "LoanCoverFreezeAuth",
            "LoanInvariants",
            "LoanLifecycle",
            "LoanMisc",
            "LoanPay",
            "LoanRounding",
            "LoanSecurity",
            "LoanSet",
            "LoanTwoStep",
            "LoanValidation",
        };

        for (auto const& info : beast::unit_test::globalSuites())
        {
            if (std::ranges::find(kMembers, info.name()) != kMembers.end())
                info.run(runner());
        }
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(Loan, tx, xrpl);

}  // namespace xrpl::test
