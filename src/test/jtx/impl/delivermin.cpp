#include <test/jtx/delivermin.h>

#include <xrpl/protocol/jss.h>

namespace xrpl::test::jtx {

void
deliver_min::operator()(Env& env, JTx& jt) const
{
    jt.jv[jss::DeliverMin] = amount_.getJson(JsonOptions::none);
}

}  // namespace xrpl::test::jtx
