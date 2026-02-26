#include <test/jtx/account_txn_id.h>

namespace xrpl::test::jtx {

void
account_txn_id::operator()(Env&, JTx& jt) const
{
    if (!hash_.isZero())
        jt["AccountTxnID"] = strHex(hash_);
}

}  // namespace xrpl::test::jtx
