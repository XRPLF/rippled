#include <test/jtx/last_ledger_sequence.h>

namespace xrpl {
namespace test {
namespace jtx {

void
last_ledger_seq::operator()(Env&, JTx& jt) const
{
    jt["LastLedgerSequence"] = num_;
}

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
