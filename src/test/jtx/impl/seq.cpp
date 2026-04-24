#include <test/jtx/seq.h>

#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>

#include <xrpl/protocol/jss.h>

namespace xrpl::test::jtx {

void
seq::operator()(Env&, JTx& jt) const
{
    if (!manual_)
        return;
    jt.fill_seq = false;
    if (num_)
        jt[jss::Sequence] = *num_;
}

}  // namespace xrpl::test::jtx
