#include <xrpl/protocol/Protocol.h>

namespace xrpl {
bool
isVotingLedger(LedgerIndex seq)
{
    return seq % kFlagLedgerInterval == 0;
}

bool
isFlagLedger(LedgerIndex seq)
{
    return seq % kFlagLedgerInterval == 0;
}
}  // namespace xrpl
