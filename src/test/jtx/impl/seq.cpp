#include <test/jtx/seq.h>

#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>

#include <xrpl/protocol/jss.h>

namespace xrpl::test::jtx {

void
Seq::operator()(Env&, JTx& jt) const
{
    if (!manual_)
        return;
    jt.fillSeq = false;
    if (num_)
        jt[jss::Sequence] = *num_;
}

}  // namespace xrpl::test::jtx
