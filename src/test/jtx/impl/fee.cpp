#include <test/jtx/fee.h>

#include <xrpl/protocol/jss.h>

namespace xrpl::test::jtx {

void
fee::operator()(Env& env, JTx& jt) const
{
    if (!manual_)
        return;
    jt.fill_fee = false;
    assert(!increment_ || !amount_);
    if (increment_)
    {
        jt[sfFee] = STAmount(env.current()->fees().increment).getJson();
    }
    else if (amount_)
    {
        jt[sfFee] = amount_->getJson(JsonOptions::none);
    }
}

}  // namespace xrpl::test::jtx
