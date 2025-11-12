#include <test/jtx/seq.h>

#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {
namespace jtx {

void
seq::operator()(Env&, JTx& jt) const
{
    if (!manual_)
        return;
    jt.fill_seq = false;
    if (num_)
        jt[jss::Sequence] = *num_;
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
