#ifndef HOOK_INCLUDED
#define HOOK_INCLUDED 1

#include <ripple/protocol/TxFormats.h>

namespace ripple {
class STTx;
}

namespace hook {

struct HookContext;
struct HookResult;

bool
isEmittedTxn(ripple::STTx const& tx);

bool
canHook(ripple::TxType txType, uint64_t hookOn);

}  // namespace hook

#endif