#include <test/jtx/txflags.h>

#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>

#include <xrpl/protocol/jss.h>

namespace xrpl {
namespace test {
namespace jtx {

void
txflags::operator()(Env&, JTx& jt) const
{
    jt[jss::Flags] = v_ /*| tfFullyCanonicalSig*/;
}

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
