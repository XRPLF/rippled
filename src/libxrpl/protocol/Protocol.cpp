#include <xrpl/protocol/Protocol.h>

namespace xrpl {
bool
isVotingLedger(LedgerIndex seq)
{
    return seq % kFLAG_LEDGER_INTERVAL == 0;
}

bool
isFlagLedger(LedgerIndex seq)
{
    return seq % kFLAG_LEDGER_INTERVAL == 0;
}
}  // namespace xrpl
