#include <test/jtx/tag.h>

#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>

namespace xrpl::test::jtx {

void
Dtag::operator()(Env&, JTx& jt) const
{
    jt.jv["DestinationTag"] = value_;
}

void
Stag::operator()(Env&, JTx& jt) const
{
    jt.jv["SourceTag"] = value_;
}

}  // namespace xrpl::test::jtx
