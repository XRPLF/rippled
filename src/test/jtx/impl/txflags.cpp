#include <test/jtx/txflags.h>

#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>

#include <xrpl/protocol/jss.h>

namespace xrpl::test::jtx {

void
Txflags::operator()(Env&, JTx& jt) const
{
    jt[jss::Flags] = v_ /*| tfFullyCanonicalSig*/;
}

}  // namespace xrpl::test::jtx
