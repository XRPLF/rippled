#ifndef __ACCOUNTSTATE__
#define __ACCOUNTSTATE__

// An account's state in one or more accepted ledgers
#include "uint256.h"

class AccountState
{
public:
    typedef boost::shared_ptr<AccountState> pointer;

private:
    uint160 mAccountID;
    uint64 mBalance;
    uint32 mAccountSeq, mFirstValidLedger, mLastValidLedger;

public:
};

#endif
