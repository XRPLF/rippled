#include <test/jtx/invoice_id.h>

namespace ripple {
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
}  // namespace ripple
