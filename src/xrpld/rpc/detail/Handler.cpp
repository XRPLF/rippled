/** @file
 *  Central dispatch registry mapping RPC method names to handler functions.
 *
 *  Defines the static handler table (`kHANDLER_ARRAY`), the `HandlerTable`
 *  singleton that owns the live multimap, and the two public lookup functions
 *  `getHandler()` and `getHandlerNames()`. Version overlap between same-named
 *  handlers is a fatal `LogicError` detected at startup.
 */

#include <xrpld/rpc/detail/Handler.h>

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/handlers/Handlers.h>
#include <xrpld/rpc/handlers/ledger/Ledger.h>
#include <xrpld/rpc/handlers/server_info/Version.h>

#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ApiVersion.h>

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace xrpl::RPC {
namespace {

/** Adapt an old-style free-function handler to the canonical by-reference signature.
 *
 *  Old-style handlers return `Json::Value` by value. The dispatch layer requires
 *  `Status(JsonContext&, Json::Value&)`. This shim calls `f`, assigns the result
 *  to `result`, and verifies that the return value is a JSON object. If it is not
 *  — which is a programming error, not a user error — `makeObjectValue()` wraps it
 *  defensively. That branch is excluded from coverage because correct handlers
 *  never reach it.
 *
 *  @tparam Function  An old-style handler callable: `Json::Value(JsonContext&)`.
 *  @param f  The old-style handler to wrap.
 *  @return A `Handler::Method<json::Value>` suitable for storage in `Handler::valueMethod`.
 */
template <typename Function>
Handler::Method<json::Value>
byRef(Function const& f)
{
    return [f](JsonContext& context, json::Value& result) {
        result = f(context);
        if (result.type() != json::ValueType::Object)
        {
            // LCOV_EXCL_START
            UNREACHABLE("xrpl::RPC::byRef : result is object");
            result = RPC::makeObjectValue(result);
            // LCOV_EXCL_STOP
        }

        return Status();
    };
}

/** Drive a new-style class-based handler through its two-phase dispatch.
 *
 *  Asserts that `context.apiVersion` is within `[HandlerImpl::minApiVer,
 *  HandlerImpl::maxApiVer]`, then constructs `HandlerImpl`, runs `check()`,
 *  and either injects the error status into `object` or calls `writeResult()`.
 *
 *  @tparam Object      The JSON output type (typically `json::Value`).
 *  @tparam HandlerImpl A class with static `minApiVer`/`maxApiVer`, a
 *      `check()` method returning `Status`, and a `writeResult(Object&)` method.
 *  @param context  The dispatched RPC context.
 *  @param object   Output parameter populated by `writeResult()` on success,
 *      or by `Status::inject()` on failure.
 *  @return The `Status` returned by `check()`, or `Status()` on success.
 */
template <class Object, class HandlerImpl>
Status
handle(JsonContext& context, Object& object)
{
    XRPL_ASSERT(
        context.apiVersion >= HandlerImpl::minApiVer &&
            context.apiVersion <= HandlerImpl::maxApiVer,
        "xrpl::RPC::handle : valid API version");
    HandlerImpl handler(context);

    auto status = handler.check();
    if (status)
    {
        status.inject(object);
    }
    else
    {
        handler.writeResult(object);
    }
    return status;
}

/** Construct a `Handler` value-struct from a new-style class-based handler.
 *
 *  Reads the static metadata fields (`name`, `role`, `condition`,
 *  `minApiVer`, `maxApiVer`) from `HandlerImpl` and binds `handle<>` as
 *  the callable, producing a `Handler` ready for insertion into the table.
 *
 *  @tparam HandlerImpl A new-style handler class (e.g. `LedgerHandler`,
 *      `VersionHandler`) with the required static metadata fields.
 *  @return A fully populated `Handler` struct.
 */
template <typename HandlerImpl>
Handler
handlerFrom()
{
    return {
        HandlerImpl::name,
        &handle<json::Value, HandlerImpl>,
        HandlerImpl::role,
        HandlerImpl::condition,
        HandlerImpl::minApiVer,
        HandlerImpl::maxApiVer};
}

/** Static registry of all old-style RPC handlers.
 *
 *  Each entry specifies the method name, the wrapped handler callable,
 *  the required role (`USER` or `ADMIN`), the network/ledger condition
 *  that must be satisfied before dispatch, and an optional API version
 *  range (defaults to `[kAPI_MINIMUM_SUPPORTED_VERSION, kAPI_MAXIMUM_VALID_VERSION]`).
 *
 *  Entries with explicit `minApiVer`/`maxApiVer` (e.g. `ledger_header`,
 *  `tx_history`) are hidden from clients using API versions outside that
 *  range. `LedgerHandler` and `VersionHandler` are new-style handlers
 *  registered separately via `HandlerTable::addHandler()` and are not
 *  listed here.
 *
 *  @note Adding a handler here without also ensuring no version overlap
 *      with an existing same-named entry causes `logicError()` at startup.
 */
Handler const kHANDLER_ARRAY[]{
    // Some handlers not specified here are added to the table via addHandler()
    // Request-response methods
    {.name = "account_info",
     .valueMethod = byRef(&doAccountInfo),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "account_currencies",
     .valueMethod = byRef(&doAccountCurrencies),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "account_lines",
     .valueMethod = byRef(&doAccountLines),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "account_channels",
     .valueMethod = byRef(&doAccountChannels),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "account_nfts",
     .valueMethod = byRef(&doAccountNFTs),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "account_objects",
     .valueMethod = byRef(&doAccountObjects),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "account_offers",
     .valueMethod = byRef(&doAccountOffers),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "account_tx",
     .valueMethod = byRef(&doAccountTx),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "amm_info",
     .valueMethod = byRef(&doAMMInfo),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "blacklist",
     .valueMethod = byRef(&doBlackList),
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = "book_changes",
     .valueMethod = byRef(&doBookChanges),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "book_offers",
     .valueMethod = byRef(&doBookOffers),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "can_delete",
     .valueMethod = byRef(&doCanDelete),
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = "channel_authorize",
     .valueMethod = byRef(&doChannelAuthorize),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "channel_verify",
     .valueMethod = byRef(&doChannelVerify),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "connect",
     .valueMethod = byRef(&doConnect),
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = "consensus_info",
     .valueMethod = byRef(&doConsensusInfo),
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = "deposit_authorized",
     .valueMethod = byRef(&doDepositAuthorized),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "feature",
     .valueMethod = byRef(&doFeature),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "fee",
     .valueMethod = byRef(&doFee),
     .role = Role::USER,
     .condition = Condition::NeedsCurrentLedger},
    {.name = "fetch_info",
     .valueMethod = byRef(&doFetchInfo),
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = "gateway_balances",
     .valueMethod = byRef(&doGatewayBalances),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "get_counts",
     .valueMethod = byRef(&doGetCounts),
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = "get_aggregate_price",
     .valueMethod = byRef(&doGetAggregatePrice),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "ledger_accept",
     .valueMethod = byRef(&doLedgerAccept),
     .role = Role::ADMIN,
     .condition = Condition::NeedsCurrentLedger},
    {.name = "ledger_cleaner",
     .valueMethod = byRef(&doLedgerCleaner),
     .role = Role::ADMIN,
     .condition = Condition::NeedsNetworkConnection},
    {.name = "ledger_closed",
     .valueMethod = byRef(&doLedgerClosed),
     .role = Role::USER,
     .condition = Condition::NeedsClosedLedger},
    {.name = "ledger_current",
     .valueMethod = byRef(&doLedgerCurrent),
     .role = Role::USER,
     .condition = Condition::NeedsCurrentLedger},
    {.name = "ledger_data",
     .valueMethod = byRef(&doLedgerData),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "ledger_entry",
     .valueMethod = byRef(&doLedgerEntry),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "ledger_header",
     .valueMethod = byRef(&doLedgerHeader),
     .role = Role::USER,
     .condition = Condition::NoCondition,
     .minApiVer = 1,
     .maxApiVer = 1},
    {.name = "ledger_request",
     .valueMethod = byRef(&doLedgerRequest),
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = "log_level",
     .valueMethod = byRef(&doLogLevel),
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = "logrotate",
     .valueMethod = byRef(&doLogRotate),
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = "manifest",
     .valueMethod = byRef(&doManifest),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "nft_buy_offers",
     .valueMethod = byRef(&doNFTBuyOffers),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "nft_sell_offers",
     .valueMethod = byRef(&doNFTSellOffers),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "noripple_check",
     .valueMethod = byRef(&doNoRippleCheck),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "owner_info",
     .valueMethod = byRef(&doOwnerInfo),
     .role = Role::USER,
     .condition = Condition::NeedsCurrentLedger},
    {.name = "peers",
     .valueMethod = byRef(&doPeers),
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = "path_find",
     .valueMethod = byRef(&doPathFind),
     .role = Role::USER,
     .condition = Condition::NeedsCurrentLedger},
    {.name = "ping",
     .valueMethod = byRef(&doPing),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "print",
     .valueMethod = byRef(&doPrint),
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    //      {   "profile",              byRef (&doProfile), Role::USER,
    //      NEEDS_CURRENT_LEDGER  },
    {.name = "random",
     .valueMethod = byRef(&doRandom),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "peer_reservations_add",
     .valueMethod = byRef(&doPeerReservationsAdd),
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = "peer_reservations_del",
     .valueMethod = byRef(&doPeerReservationsDel),
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = "peer_reservations_list",
     .valueMethod = byRef(&doPeerReservationsList),
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = "ripple_path_find",
     .valueMethod = byRef(&doRipplePathFind),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "server_definitions",
     .valueMethod = byRef(&doServerDefinitions),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "server_info",
     .valueMethod = byRef(&doServerInfo),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "server_state",
     .valueMethod = byRef(&doServerState),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "sign",
     .valueMethod = byRef(&doSign),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "sign_for",
     .valueMethod = byRef(&doSignFor),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "simulate",
     .valueMethod = byRef(&doSimulate),
     .role = Role::USER,
     .condition = Condition::NeedsCurrentLedger},
    {.name = "stop",
     .valueMethod = byRef(&doStop),
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = "submit",
     .valueMethod = byRef(&doSubmit),
     .role = Role::USER,
     .condition = Condition::NeedsCurrentLedger},
    {.name = "submit_multisigned",
     .valueMethod = byRef(&doSubmitMultiSigned),
     .role = Role::USER,
     .condition = Condition::NeedsCurrentLedger},
    {.name = "transaction_entry",
     .valueMethod = byRef(&doTransactionEntry),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "tx",
     .valueMethod = byRef(&doTxJson),
     .role = Role::USER,
     .condition = Condition::NeedsNetworkConnection},
    {.name = "tx_history",
     .valueMethod = byRef(&doTxHistory),
     .role = Role::USER,
     .condition = Condition::NoCondition,
     .minApiVer = 1,
     .maxApiVer = 1},
    {.name = "tx_reduce_relay",
     .valueMethod = byRef(&doTxReduceRelay),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "unl_list",
     .valueMethod = byRef(&doUnlList),
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = "validation_create",
     .valueMethod = byRef(&doValidationCreate),
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = "validators",
     .valueMethod = byRef(&doValidators),
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = "validator_list_sites",
     .valueMethod = byRef(&doValidatorListSites),
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = "validator_info",
     .valueMethod = byRef(&doValidatorInfo),
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = "vault_info",
     .valueMethod = byRef(&doVaultInfo),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "wallet_propose",
     .valueMethod = byRef(&doWalletPropose),
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    // Event methods
    {.name = "subscribe",
     .valueMethod = byRef(&doSubscribe),
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = "unsubscribe",
     .valueMethod = byRef(&doUnsubscribe),
     .role = Role::USER,
     .condition = Condition::NoCondition},
};

/** Immutable dispatch table mapping RPC method names to `Handler` entries.
 *
 *  Built once at startup from `kHANDLER_ARRAY` plus the two new-style
 *  handlers (`LedgerHandler`, `VersionHandler`). The backing store is a
 *  `std::multimap` so that a single method name may have multiple entries
 *  covering non-overlapping API version ranges. Any version overlap detected
 *  during construction causes `logicError()`, crashing the process before it
 *  accepts any requests.
 *
 *  Access the singleton via `HandlerTable::instance()`. The object is
 *  const after construction and requires no locking.
 */
class HandlerTable
{
private:
    using handler_table_t = std::multimap<std::string, Handler>;

    /** Check whether a candidate version range overlaps any existing entry for the same name.
     *
     *  Uses the standard interval-overlap test: two ranges `[a,b]` and `[c,d]` overlap
     *  iff `a <= d && b >= c`. Called during construction to enforce the invariant that
     *  each `(name, version)` pair maps to at most one handler.
     *
     *  @param range   The `equal_range` result for the method name being inserted.
     *  @param minVer  Lower bound of the candidate version range (inclusive).
     *  @param maxVer  Upper bound of the candidate version range (inclusive).
     *  @return `true` if any existing entry for the name overlaps `[minVer, maxVer]`.
     */
    [[nodiscard]] static bool
    overlappingApiVersion(
        std::pair<handler_table_t::iterator, handler_table_t::iterator> range,
        unsigned minVer,
        unsigned maxVer)
    {
        XRPL_ASSERT(minVer <= maxVer, "xrpl::RPC::HandlerTable : valid API version range");
        XRPL_ASSERT(
            maxVer <= RPC::kAPI_MAXIMUM_VALID_VERSION,
            "xrpl::RPC::HandlerTable : valid max API version");

        return std::any_of(
            range.first,
            range.second,  //
            [minVer, maxVer](auto const& item) {
                return item.second.minApiVer <= maxVer && item.second.maxApiVer >= minVer;
            });
    }

    /** Construct the table from a compile-time array of `Handler` entries.
     *
     *  Inserts every entry from `entries` into `table_`, calling `logicError()`
     *  on any version overlap. After processing the array, registers the two
     *  new-style handlers (`LedgerHandler`, `VersionHandler`) via `addHandler()`.
     *
     *  @tparam N  Size of the handler array (deduced).
     *  @param entries  Array of old-style `Handler` descriptors (typically `kHANDLER_ARRAY`).
     *  @throws LogicError if any two entries for the same name have overlapping version ranges.
     */
    template <std::size_t N>
    explicit HandlerTable(Handler const (&entries)[N])
    {
        for (auto const& entry : entries)
        {
            if (overlappingApiVersion(
                    table_.equal_range(entry.name), entry.minApiVer, entry.maxApiVer))
            {
                logicError(
                    std::string("Handler for ") + entry.name +
                    " overlaps with an existing handler");
            }

            table_.insert({entry.name, entry});
        }

        // This is where the new-style handlers are added.
        addHandler<LedgerHandler>();
        addHandler<VersionHandler>();
    }

public:
    /** Return the process-wide singleton instance, initializing it on first call.
     *
     *  Thread-safe by C++11 static-local initialization guarantees.
     *  The returned reference is valid for the lifetime of the process.
     *
     *  @return A const reference to the single `HandlerTable`.
     */
    static HandlerTable const&
    instance()
    {
        static HandlerTable const kHANDLER_TABLE(kHANDLER_ARRAY);
        return kHANDLER_TABLE;
    }

    /** Look up the handler for a given API version and method name.
     *
     *  Returns `nullptr` immediately if `version` falls outside the supported
     *  range: below `kAPI_MINIMUM_SUPPORTED_VERSION`, above
     *  `kAPI_MAXIMUM_SUPPORTED_VERSION` (or `kAPI_BETA_VERSION` when
     *  `betaEnabled` is true). This hides beta-only handlers from non-beta
     *  nodes at the lookup level rather than inside each handler.
     *
     *  @param version      The API version of the incoming request.
     *  @param betaEnabled  Whether to extend the upper version bound to
     *      `kAPI_BETA_VERSION` for this node.
     *  @param name         The RPC method name (e.g. `"account_info"`).
     *  @return Pointer to the matching `Handler` in the immutable table, or
     *      `nullptr` if the version is unsupported or no handler is registered
     *      for `name` at `version`.
     */
    [[nodiscard]] Handler const*
    getHandler(unsigned version, bool betaEnabled, std::string const& name) const
    {
        if (version < RPC::kAPI_MINIMUM_SUPPORTED_VERSION ||
            version > (betaEnabled ? RPC::kAPI_BETA_VERSION : RPC::kAPI_MAXIMUM_SUPPORTED_VERSION))
            return nullptr;

        auto const range = table_.equal_range(name);
        auto const i = std::find_if(range.first, range.second, [version](auto const& entry) {
            return entry.second.minApiVer <= version && version <= entry.second.maxApiVer;
        });

        return i == range.second ? nullptr : &i->second;
    }

    /** Return the names of all registered RPC methods.
     *
     *  Returns raw string pointers into the table entries. Callers must treat
     *  these as interned constants — they are valid for the lifetime of the
     *  process and must not be freed or compared by address.
     *
     *  @return A `std::set` of `char const*` pointing into the handler name fields.
     */
    [[nodiscard]] std::set<char const*>
    getHandlerNames() const
    {
        std::set<char const*> ret;
        for (auto const& i : table_)
            ret.insert(i.second.name);

        return ret;
    }

private:
    handler_table_t table_;

    /** Register a new-style class-based handler in the table.
     *
     *  Performs compile-time bounds checks on the handler's version range and
     *  a runtime overlap check against existing entries. Calls `logicError()`
     *  if any overlap is detected.
     *
     *  @tparam HandlerImpl  A new-style handler class with static metadata fields
     *      (`name`, `minApiVer`, `maxApiVer`, `role`, `condition`).
     *  @throws LogicError if `HandlerImpl`'s version range overlaps an existing entry.
     */
    template <class HandlerImpl>
    void
    addHandler()
    {
        static_assert(HandlerImpl::minApiVer <= HandlerImpl::maxApiVer);
        static_assert(HandlerImpl::maxApiVer <= RPC::kAPI_MAXIMUM_VALID_VERSION);
        static_assert(RPC::kAPI_MINIMUM_SUPPORTED_VERSION <= HandlerImpl::minApiVer);

        if (overlappingApiVersion(
                table_.equal_range(HandlerImpl::name),
                HandlerImpl::minApiVer,
                HandlerImpl::maxApiVer))
        {
            logicError(
                std::string("Handler for ") + HandlerImpl::name +
                " overlaps with an existing handler");
        }

        table_.insert({HandlerImpl::name, handlerFrom<HandlerImpl>()});
    }
};

}  // namespace

/** Look up the handler for an incoming RPC request.
 *
 *  Delegates to `HandlerTable::instance().getHandler()`. Returns `nullptr`
 *  when `version` is outside the supported range or no handler is registered
 *  for `name` at that version. The returned pointer is into the immutable
 *  singleton table and remains valid for the lifetime of the process.
 *
 *  @param version      The API version of the incoming request.
 *  @param betaEnabled  Whether beta API versions are enabled on this node.
 *  @param name         The RPC method name string.
 *  @return Pointer to the matching `Handler`, or `nullptr` if not found.
 *  @see HandlerTable::getHandler
 */
Handler const*
getHandler(unsigned version, bool betaEnabled, std::string const& name)
{
    return HandlerTable::instance().getHandler(version, betaEnabled, name);
}

/** Return the names of every registered RPC method.
 *
 *  Used by introspection paths (e.g. `server_info`, tests) to enumerate the
 *  full method surface. The returned pointers are interned into the singleton
 *  table and are valid for the process lifetime; do not free them.
 *
 *  @return A `std::set<char const*>` of method name strings.
 *  @see HandlerTable::getHandlerNames
 */
std::set<char const*>
getHandlerNames()
{
    return HandlerTable::instance().getHandlerNames();
}

}  // namespace xrpl::RPC
