#include <test/jtx/domain.h>

#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/SField.h>

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
