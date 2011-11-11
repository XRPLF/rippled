#ifndef __ACCOUNTSTATE__
#define __ACCOUNTSTATE__

// An account's state in one or more accepted ledgers

class AccountState
{
private:
    int160 mAccountID;
    uint64 mBalance;
    uint32 mAccountSeq, mFirstValidLedger, mLastValidLedger;

public:
};

#endif
