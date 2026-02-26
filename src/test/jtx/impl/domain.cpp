#include <test/jtx/domain.h>

#include <xrpl/protocol/jss.h>

namespace xrpl::test::jtx {

void
domain::operator()(Env&, JTx& jt) const
{
    jt[sfDomainID.jsonName] = to_string(v_);
}

}  // namespace xrpl::test::jtx
