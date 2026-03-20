#include <test/jtx/fee.h>

#include <xrpl/protocol/jss.h>

namespace xrpl {
namespace test {
namespace jtx {

void
Fee::operator()(Env& env, JTx& jt) const
{
    if (!manual_)
        return;
    jt.fillFee = false;
    assert(!increment_ || !amount_);
    if (increment_)
    {
        jt[sfFee] = STAmount(env.current()->fees().increment).getJson();
    }
    else if (amount_)
    {
        jt[sfFee] = amount_->getJson(JsonOptions::kNONE);
    }
}

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
