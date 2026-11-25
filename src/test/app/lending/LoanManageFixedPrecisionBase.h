#pragma once

#include <test/app/lending/LoanPayFixedPrecisionBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>

#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TxFlags.h>

#include <chrono>
#include <cstdint>

namespace xrpl {

// LoanManage-tier scaffolding on top of LoanPayFixedPrecisionBase: the
// default/impair helpers needed by LoanManageFixedPrecision only.
class LoanManageFixedPrecisionBase : public LoanPayFixedPrecisionBase
{
protected:
    // Advance the clock one second past the loan's due date (late, in grace) or
    // past its grace period (defaultable).
    void
    closePastDue(test::jtx::Env& env, Keylet const& loanKeylet)
    {
        auto const loanSle = env.le(loanKeylet);
        if (!BEAST_EXPECT(loanSle))
            return;
        std::uint32_t const dueDate = loanSle->at(sfNextPaymentDueDate);
        env.close(NetClock::time_point{NetClock::duration{dueDate}} + std::chrono::seconds{1});
    }

    void
    closePastGrace(test::jtx::Env& env, Keylet const& loanKeylet)
    {
        auto const loanSle = env.le(loanKeylet);
        if (!BEAST_EXPECT(loanSle))
            return;
        std::uint32_t const dueDate = loanSle->at(sfNextPaymentDueDate);
        std::uint32_t const grace = loanSle->at(sfGracePeriod);
        env.close(
            NetClock::time_point{NetClock::duration{dueDate + grace}} + std::chrono::seconds{1});
    }

    void
    impairWhenLate(test::jtx::Env& env, test::jtx::Account const& owner, Keylet const& loanKeylet)
    {
        closePastDue(env, loanKeylet);
        env(test::jtx::loan::manage(owner, loanKeylet.key, tfLoanImpair));
        env.close();
    }

    void
    defaultAfterGrace(
        test::jtx::Env& env,
        test::jtx::Account const& owner,
        Keylet const& loanKeylet)
    {
        closePastGrace(env, loanKeylet);
        env(test::jtx::loan::manage(owner, loanKeylet.key, tfLoanDefault));
        env.close();
    }
};

}  // namespace xrpl
