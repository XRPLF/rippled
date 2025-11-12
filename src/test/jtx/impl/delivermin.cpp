#include <test/jtx/delivermin.h>

#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {
namespace jtx {

void
delivermin::operator()(Env& env, JTx& jt) const
{
    jt.jv[jss::DeliverMin] = amount_.getJson(JsonOptions::none);
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
