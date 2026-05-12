#include <test/jtx/delivermin.h>

#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>

#include <xrpl/protocol/jss.h>

namespace xrpl::test::jtx {

void
DeliverMin::operator()(Env& env, JTx& jt) const
{
    jt.jv[jss::DeliverMin] = amount_.getJson(JsonOptions::Values::None);
}

}  // namespace xrpl::test::jtx
