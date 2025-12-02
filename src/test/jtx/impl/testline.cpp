#include <test/jtx/testline.h>

namespace ripple {
namespace test {
namespace jtx {

void
testline::operator()(Env&, JTx& jt) const
{
    jt.testLine = line_;
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
