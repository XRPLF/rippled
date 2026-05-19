#include <test/jtx/sendmax.h>

#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>

#include <xrpl/protocol/jss.h>

namespace xrpl::test::jtx {

void
sendmax::operator()(Env& env, JTx& jt) const
{
    jt.jv[jss::SendMax] = amount_.getJson(JsonOptions::none);
}

}  // namespace xrpl::test::jtx
