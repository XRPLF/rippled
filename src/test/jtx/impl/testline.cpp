#include <test/jtx/testline.h>

namespace xrpl {
namespace test {
namespace jtx {

void
testline::operator()(Env&, JTx& jt) const
{
    jt.testLine = line_;
}

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
