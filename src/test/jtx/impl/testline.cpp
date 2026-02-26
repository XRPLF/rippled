#include <test/jtx/testline.h>

namespace xrpl::test::jtx {

void
testline::operator()(Env&, JTx& jt) const
{
    jt.testLine = line_;
}

}  // namespace xrpl::test::jtx
