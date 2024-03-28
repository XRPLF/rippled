#include <ripple/basics/Blob.h>
#include <ripple/basics/base_uint.h>
#include <ripple/protocol/AccountID.h>

#include <map>

namespace hook {
// This map type acts as both a read and write cache for hook execution
// and is preserved across the execution of the set of hook chains
// being executed in the current transaction. It is committed to lgr
// only upon tesSuccess for the otxn.
using HookStateMap = std::map<
    ripple::AccountID,  // account that owns the state
    std::pair<
        int64_t,  // remaining available ownercount
        std::map<
            ripple::uint256,  // namespace
            std::map<
                ripple::uint256,  // key
                std::pair<
                    bool,               // is modified from ledger value
                    ripple::Blob>>>>>;  // the value

struct HookResult;

}  // namespace hook
