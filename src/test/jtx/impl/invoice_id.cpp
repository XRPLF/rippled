#include <test/jtx/invoice_id.h>

#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>

#include <xrpl/basics/strHex.h>

namespace xrpl {
namespace test {
namespace jtx {

void
invoice_id::operator()(Env&, JTx& jt) const
{
    if (!hash_.isZero())
        jt["InvoiceID"] = strHex(hash_);
}

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
