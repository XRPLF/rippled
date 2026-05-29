#include <test/jtx/domain.h>

#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/SField.h>

namespace xrpl::test::jtx {

void
Domain::operator()(Env&, JTx& jt) const
{
    jt[sfDomainID.jsonName] = to_string(v_);
}

}  // namespace xrpl::test::jtx
