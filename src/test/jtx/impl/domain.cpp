#include <test/jtx/domain.h>

#include <xrpl/protocol/jss.h>

namespace xrpl {
namespace test {
namespace jtx {

void
domain::operator()(Env&, JTx& jt) const
{
    jt[sfDomainID.jsonName] = to_string(v_);
}

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
