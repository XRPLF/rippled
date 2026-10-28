#include <test/app/lending/LoanTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/ter.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/TER.h>

namespace xrpl::test {

class LoanTransfer_test : public LoanTestBase
{
    void
    testDisabled()
    {
        testcase("disabled");
        using namespace jtx;

        Env env(*this, all_ - featureStructuredCredit);
        Account const lender{"lender"};
        env.fund(XRP(10'000), lender);
        env.close();

        env(loan::transfer(lender, uint256{1}, uint256{2}), Ter(temDISABLED));
    }

public:
    void
    run() override
    {
        testDisabled();
    }
};

BEAST_DEFINE_TESTSUITE(LoanTransfer, tx, xrpl);

}  // namespace xrpl::test
