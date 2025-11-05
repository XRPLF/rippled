#include <test/jtx/flags.h>

#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {
namespace jtx {

Json::Value
fset(Account const& account, std::uint32_t on, std::uint32_t off)
{
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::TransactionType] = jss::AccountSet;
    if (on != 0)
        jv[jss::SetFlag] = on;
    if (off != 0)
        jv[jss::ClearFlag] = off;
    return jv;
}

void
flags::operator()(Env& env) const
{
    auto const sle = env.le(account_);
    if (!sle)
        env.test.fail();
    else if (sle->isFieldPresent(sfFlags))
        env.test.expect((sle->getFieldU32(sfFlags) & mask_) == mask_);
    else
        env.test.expect(mask_ == 0);
}

void
nflags::operator()(Env& env) const
{
    auto const sle = env.le(account_);
    if (!sle)
        env.test.fail();
    else if (sle->isFieldPresent(sfFlags))
        env.test.expect((sle->getFieldU32(sfFlags) & mask_) == 0);
    else
        env.test.pass();
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
