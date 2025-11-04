#include <test/jtx/tag.h>

namespace ripple {
namespace test {
namespace jtx {

void
dtag::operator()(Env&, JTx& jt) const
{
    jt.jv["DestinationTag"] = value_;
}

void
stag::operator()(Env&, JTx& jt) const
{
    jt.jv["SourceTag"] = value_;
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
