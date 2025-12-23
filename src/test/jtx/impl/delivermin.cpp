#include <test/jtx/delivermin.h>

#include <xrpl/protocol/jss.h>

namespace xrpl {
namespace test {
namespace jtx {

void
deliver_min::operator()(Env& env, JTx& jt) const
{
    jt.jv[jss::DeliverMin] = amount_.getJson(JsonOptions::none);
}

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
