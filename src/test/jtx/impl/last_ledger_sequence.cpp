#include <test/jtx/last_ledger_sequence.h>

#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>

namespace xrpl::test::jtx {

void
LastLedgerSeq::operator()(Env&, JTx& jt) const
{
    jt["LastLedgerSequence"] = num_;
}

}  // namespace xrpl::test::jtx
