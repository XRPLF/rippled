#ifndef XRPL_RPC_RPCLEDGERHELPERS_H_INCLUDED
#define XRPL_RPC_RPCLEDGERHELPERS_H_INCLUDED

#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Status.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/beast/core/SemanticVersion.h>
#include <xrpl/proto/org/xrpl/rpc/v1/xrp_ledger.pb.h>
#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/SecretKey.h>

#include <optional>
#include <variant>

namespace Json {
class Value;
}

namespace ripple {

class ReadView;
class Transaction;

namespace RPC {

struct JsonContext;

/** Get ledger by hash
    If there is no error in the return value, the ledger pointer will have
    been filled
*/
template <class T>
Status
getLedger(T& ledger, uint256 const& ledgerHash, Context& context);

/** Get ledger by sequence
    If there is no error in the return value, the ledger pointer will have
    been filled
*/
template <class T>
Status
getLedger(T& ledger, uint32_t ledgerIndex, Context& context);

enum LedgerShortcut { CURRENT, CLOSED, VALIDATED };
/** Get ledger specified in shortcut.
    If there is no error in the return value, the ledger pointer will have
    been filled
*/
template <class T>
Status
getLedger(T& ledger, LedgerShortcut shortcut, Context& context);

/** Look up a ledger from a request and fill a Json::Result with either
    an error, or data representing a ledger.

    If there is no error in the return value, then the ledger pointer will have
    been filled.
*/
Json::Value
lookupLedger(std::shared_ptr<ReadView const>&, JsonContext&);

/** Look up a ledger from a request and fill a Json::Result with the data
    representing a ledger.

    If the returned Status is OK, the ledger pointer will have been filled.
*/
Status
lookupLedger(
    std::shared_ptr<ReadView const>&,
    JsonContext&,
    Json::Value& result);

template <class T, class R>
Status
ledgerFromRequest(T& ledger, GRPCContext<R>& context);

template <class T>
Status
ledgerFromSpecifier(
    T& ledger,
    org::xrpl::rpc::v1::LedgerSpecifier const& specifier,
    Context& context);

/** Return a ledger based on ledger_hash or ledger_index,
    or an RPC error */
std::variant<std::shared_ptr<Ledger const>, Json::Value>
getLedgerByContext(RPC::JsonContext& context);

}  // namespace RPC

}  // namespace ripple

#endif
