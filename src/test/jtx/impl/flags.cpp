#include <test/jtx/flags.h>

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>

namespace xrpl::test::jtx {

json::Value
fset(Account const& account, std::uint32_t on, std::uint32_t off)
{
    json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::TransactionType] = jss::AccountSet;
    if (on != 0)
        jv[jss::SetFlag] = on;
    if (off != 0)
        jv[jss::ClearFlag] = off;
    return jv;
}

void
Flags::operator()(Env& env) const
{
    auto const sle = env.le(account_);
    if (!sle)
    {
        env.test.fail();
    }
    else if (sle->isFieldPresent(sfFlags))
    {
        env.test.expect(sle->isFlag(mask_));
    }
    else
    {
        env.test.expect(mask_ == 0);
    }
}

void
Nflags::operator()(Env& env) const
{
    auto const sle = env.le(account_);
    if (!sle)
    {
        env.test.fail();
    }
    else if (sle->isFieldPresent(sfFlags))
    {
        env.test.expect((sle->getFieldU32(sfFlags) & mask_) == 0);
    }
    else
    {
        env.test.pass();
    }
}

}  // namespace xrpl::test::jtx
