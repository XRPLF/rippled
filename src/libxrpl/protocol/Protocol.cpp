#include <xrpl/protocol/Protocol.h>
#include <xrpl/basics/TraceLog.h>

namespace xrpl {
bool
isVotingLedger(LedgerIndex seq)
{
    TRACE_FUNC();
    return seq % kFLAG_LEDGER_INTERVAL == 0;
}

bool
isFlagLedger(LedgerIndex seq)
{
    TRACE_FUNC();
    return seq % kFLAG_LEDGER_INTERVAL == 0;
}
}  // namespace xrpl
