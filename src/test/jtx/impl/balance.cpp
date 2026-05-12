#include <test/jtx/balance.h>

#include <test/jtx/Env.h>

#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>

#include <variant>

namespace xrpl::test::jtx {

#define TEST_EXPECT(cond) env.test.expect(cond, __FILE__, __LINE__)
#define TEST_EXPECTS(cond, reason) \
    ((cond) ? (env.test.pass(), true) : (env.test.fail((reason), __FILE__, __LINE__), false))

void
doBalance(Env& env, AccountID const& account, bool kNone, STAmount const& value, Issue const& issue)
{
    if (isXRP(issue))
    {
        auto const sle = env.le(keylet::account(account));
        if (kNone)
        {
            TEST_EXPECT(!sle);
        }
        else if (TEST_EXPECT(sle))
        {
            TEST_EXPECTS(
                sle->getFieldAmount(sfBalance) == value,
                sle->getFieldAmount(sfBalance).getText() + " / " + value.getText());
        }
    }
    else
    {
        auto const sle = env.le(keylet::line(account, issue));
        if (kNone)
        {
            TEST_EXPECT(!sle);
        }
        else if (TEST_EXPECT(sle))
        {
            auto amount = sle->getFieldAmount(sfBalance);
            amount.get<Issue>().account = value.getIssuer();
            if (account > value.getIssuer())
                amount.negate();
            TEST_EXPECTS(amount == value, amount.getText());
        }
    }
}

void
doBalance(
    Env& env,
    AccountID const& account,
    bool kNone,
    STAmount const& value,
    MPTIssue const& mptIssue)
{
    auto const sle = env.le(keylet::mptoken(mptIssue.getMptID(), account));
    if (kNone)
    {
        TEST_EXPECT(!sle);
    }
    else if (TEST_EXPECT(sle))
    {
        STAmount const amount{mptIssue, sle->getFieldU64(sfMPTAmount)};
        TEST_EXPECT(amount == value);
    }
}

void
Balance::operator()(Env& env) const
{
    std::visit(
        [&](auto const& issue) { doBalance(env, account_.id(), none_, value_, issue); },
        value_.asset().value());
}

}  // namespace xrpl::test::jtx
