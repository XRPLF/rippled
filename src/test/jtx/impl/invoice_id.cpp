#include <test/jtx/invoice_id.h>

namespace xrpl::test::jtx {

void
invoice_id::operator()(Env&, JTx& jt) const
{
    if (!hash_.isZero())
        jt["InvoiceID"] = strHex(hash_);
}

}  // namespace xrpl::test::jtx
