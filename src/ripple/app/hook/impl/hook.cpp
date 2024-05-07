#include <ripple/app/hook/applyHook.h>

//#include <ripple/app/ledger/LedgerMaster.h>
//#include <ripple/app/ledger/OpenLedger.h>
//#include <ripple/app/ledger/TransactionMaster.h>
//#include <ripple/app/misc/NetworkOPs.h>
//#include <ripple/app/misc/Transaction.h>
//#include <ripple/app/misc/TxQ.h>
//#include <ripple/app/tx/impl/Transactor.h>
//#include <ripple/app/tx/impl/details/NFTokenUtils.h>
//#include <ripple/basics/Log.h>
//#include <ripple/basics/Slice.h>
//#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/STTx.h>
//#include <ripple/protocol/tokens.h>
//#include <any>
//#include <memory>
//#include <optional>
//#include <string>
//#include <utility>
//#include <vector>

using namespace ripple;

namespace hook {

bool
isEmittedTxn(ripple::STTx const& tx)
{
    return tx.isFieldPresent(ripple::sfEmitDetails);
}

// Called by Transactor.cpp to determine if a transaction type can trigger a
// given hook... The HookOn field in the SetHook transaction determines which
// transaction types (tt's) trigger the hook. Every bit except ttHookSet is
// active low, so for example ttESCROW_FINISH = 2, so if the 2nd bit (counting
// from 0) from the right is 0 then the hook will trigger on ESCROW_FINISH. If
// it is 1 then ESCROW_FINISH will not trigger the hook. However ttHOOK_SET = 22
// is active high, so by default (HookOn == 0) ttHOOK_SET is not triggered by
// transactions. If you wish to set a hook that has control over ttHOOK_SET then
// set bit 1U<<22.
bool
canHook(ripple::TxType txType, uint64_t hookOn)
{
    // invert ttHOOK_SET bit
    hookOn ^= (1ULL << ttHOOK_SET);
    // invert entire field
    hookOn ^= 0xFFFFFFFFFFFFFFFFULL;
    return (hookOn >> txType) & 1;
}

}  // namespace hook