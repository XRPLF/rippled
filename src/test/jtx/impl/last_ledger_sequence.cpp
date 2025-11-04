#include <test/jtx/last_ledger_sequence.h>

namespace ripple {
namespace test {
namespace jtx {

void
last_ledger_seq::operator()(Env&, JTx& jt) const
{
    jt["LastLedgerSequence"] = num_;
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
