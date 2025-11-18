#include <test/jtx/txflags.h>

#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {
namespace jtx {

void
txflags::operator()(Env&, JTx& jt) const
{
    jt[jss::Flags] = v_ /*| tfFullyCanonicalSig*/;
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
