#include <xrpld/rpc/detail/Handler.h>

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/MethodNames.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/Status.h>
#include <xrpld/rpc/handlers/Handlers.h>
#include <xrpld/rpc/handlers/ledger/Ledger.h>
#include <xrpld/rpc/handlers/server_info/Version.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ApiVersion.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <span>
#include <string_view>

namespace xrpl::rpc {
namespace {

/**
 * Adjust an old-style handler to be call-by-reference.
 *
 * The handler is a template parameter rather than an argument, so that byRef
 * names a plain function instead of returning a closure over it.
 */
template <json::Value (*Function)(JsonContext&)>
Status
byRef(JsonContext& context, json::Value& result)
{
    result = Function(context);
    if (result.type() != json::ValueType::Object)
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::rpc::byRef : result is object");
        result = rpc::makeObjectValue(result);
        // LCOV_EXCL_STOP
    }

    return Status();
}

template <class HandlerImpl>
Status
handle(JsonContext& context, json::Value& object)
{
    XRPL_ASSERT(
        context.apiVersion >= HandlerImpl::minApiVer &&
            context.apiVersion <= HandlerImpl::maxApiVer,
        "xrpl::rpc::handle : valid API version");
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

template <typename HandlerImpl>
constexpr Handler
handlerFrom()
{
    static_assert(HandlerImpl::minApiVer <= HandlerImpl::maxApiVer);
    static_assert(HandlerImpl::maxApiVer <= rpc::kApiMaximumValidVersion);
    static_assert(rpc::kApiMinimumSupportedVersion <= HandlerImpl::minApiVer);

    return {
        HandlerImpl::name,
        &handle<HandlerImpl>,
        HandlerImpl::role,
        HandlerImpl::condition,
        HandlerImpl::minApiVer,
        HandlerImpl::maxApiVer};
}

// The handlers, in whatever order reads best. getHandler() searches kHandlers
// below, which is this array sorted; the order here carries no meaning.
constexpr Handler kHandlerArray[]{
    // Request-response methods
    {.name = method::kAccountInfo,
     .valueMethod = &byRef<&doAccountInfo>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kAccountCurrencies,
     .valueMethod = &byRef<&doAccountCurrencies>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kAccountLines,
     .valueMethod = &byRef<&doAccountLines>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kAccountChannels,
     .valueMethod = &byRef<&doAccountChannels>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kAccountNfts,
     .valueMethod = &byRef<&doAccountNFTs>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kAccountObjects,
     .valueMethod = &byRef<&doAccountObjects>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kAccountOffers,
     .valueMethod = &byRef<&doAccountOffers>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kAccountTx,
     .valueMethod = &byRef<&doAccountTx>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kAmmInfo,
     .valueMethod = &byRef<&doAMMInfo>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kBlacklist,
     .valueMethod = &byRef<&doBlackList>,
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = method::kBookChanges,
     .valueMethod = &byRef<&doBookChanges>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kBookOffers,
     .valueMethod = &byRef<&doBookOffers>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kCanDelete,
     .valueMethod = &byRef<&doCanDelete>,
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = method::kChannelAuthorize,
     .valueMethod = &byRef<&doChannelAuthorize>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kChannelVerify,
     .valueMethod = &byRef<&doChannelVerify>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kConnect,
     .valueMethod = &byRef<&doConnect>,
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = method::kConsensusInfo,
     .valueMethod = &byRef<&doConsensusInfo>,
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = method::kDepositAuthorized,
     .valueMethod = &byRef<&doDepositAuthorized>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kFeature,
     .valueMethod = &byRef<&doFeature>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kFee,
     .valueMethod = &byRef<&doFee>,
     .role = Role::USER,
     .condition = Condition::NeedsCurrentLedger},
    {.name = method::kFetchInfo,
     .valueMethod = &byRef<&doFetchInfo>,
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = method::kGatewayBalances,
     .valueMethod = &byRef<&doGatewayBalances>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kGetCounts,
     .valueMethod = &byRef<&doGetCounts>,
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = method::kGetAggregatePrice,
     .valueMethod = &byRef<&doGetAggregatePrice>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kLedgerAccept,
     .valueMethod = &byRef<&doLedgerAccept>,
     .role = Role::ADMIN,
     .condition = Condition::NeedsCurrentLedger},
    {.name = method::kLedgerCleaner,
     .valueMethod = &byRef<&doLedgerCleaner>,
     .role = Role::ADMIN,
     .condition = Condition::NeedsNetworkConnection},
    {.name = method::kLedgerClosed,
     .valueMethod = &byRef<&doLedgerClosed>,
     .role = Role::USER,
     .condition = Condition::NeedsClosedLedger},
    {.name = method::kLedgerCurrent,
     .valueMethod = &byRef<&doLedgerCurrent>,
     .role = Role::USER,
     .condition = Condition::NeedsCurrentLedger},
    {.name = method::kLedgerData,
     .valueMethod = &byRef<&doLedgerData>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kLedgerEntry,
     .valueMethod = &byRef<&doLedgerEntry>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kLedgerHeader,
     .valueMethod = &byRef<&doLedgerHeader>,
     .role = Role::USER,
     .condition = Condition::NoCondition,
     .minApiVer = 1,
     .maxApiVer = 1},
    {.name = method::kLedgerRequest,
     .valueMethod = &byRef<&doLedgerRequest>,
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = method::kLogLevel,
     .valueMethod = &byRef<&doLogLevel>,
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = method::kLogrotate,
     .valueMethod = &byRef<&doLogRotate>,
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = method::kManifest,
     .valueMethod = &byRef<&doManifest>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kNftBuyOffers,
     .valueMethod = &byRef<&doNFTBuyOffers>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kNftSellOffers,
     .valueMethod = &byRef<&doNFTSellOffers>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kNorippleCheck,
     .valueMethod = &byRef<&doNoRippleCheck>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kOwnerInfo,
     .valueMethod = &byRef<&doOwnerInfo>,
     .role = Role::USER,
     .condition = Condition::NeedsCurrentLedger},
    {.name = method::kPeers,
     .valueMethod = &byRef<&doPeers>,
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = method::kPathFind,
     .valueMethod = &byRef<&doPathFind>,
     .role = Role::USER,
     .condition = Condition::NeedsCurrentLedger},
    {.name = method::kPing,
     .valueMethod = &byRef<&doPing>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kPrint,
     .valueMethod = &byRef<&doPrint>,
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = method::kRandom,
     .valueMethod = &byRef<&doRandom>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kPeerReservationsAdd,
     .valueMethod = &byRef<&doPeerReservationsAdd>,
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = method::kPeerReservationsDel,
     .valueMethod = &byRef<&doPeerReservationsDel>,
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = method::kPeerReservationsList,
     .valueMethod = &byRef<&doPeerReservationsList>,
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = method::kRipplePathFind,
     .valueMethod = &byRef<&doRipplePathFind>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kServerDefinitions,
     .valueMethod = &byRef<&doServerDefinitions>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kServerInfo,
     .valueMethod = &byRef<&doServerInfo>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kServerState,
     .valueMethod = &byRef<&doServerState>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kSign,
     .valueMethod = &byRef<&doSign>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kSignFor,
     .valueMethod = &byRef<&doSignFor>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kSimulate,
     .valueMethod = &byRef<&doSimulate>,
     .role = Role::USER,
     .condition = Condition::NeedsCurrentLedger},
    {.name = method::kStop,
     .valueMethod = &byRef<&doStop>,
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = method::kSubmit,
     .valueMethod = &byRef<&doSubmit>,
     .role = Role::USER,
     .condition = Condition::NeedsCurrentLedger},
    {.name = method::kSubmitMultisigned,
     .valueMethod = &byRef<&doSubmitMultiSigned>,
     .role = Role::USER,
     .condition = Condition::NeedsCurrentLedger},
    {.name = method::kTransactionEntry,
     .valueMethod = &byRef<&doTransactionEntry>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kTx,
     .valueMethod = &byRef<&doTxJson>,
     .role = Role::USER,
     .condition = Condition::NeedsNetworkConnection},
    {.name = method::kTxHistory,
     .valueMethod = &byRef<&doTxHistory>,
     .role = Role::USER,
     .condition = Condition::NoCondition,
     .minApiVer = 1,
     .maxApiVer = 1},
    {.name = method::kTxReduceRelay,
     .valueMethod = &byRef<&doTxReduceRelay>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kUnlList,
     .valueMethod = &byRef<&doUnlList>,
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = method::kValidationCreate,
     .valueMethod = &byRef<&doValidationCreate>,
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = method::kValidators,
     .valueMethod = &byRef<&doValidators>,
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = method::kValidatorListSites,
     .valueMethod = &byRef<&doValidatorListSites>,
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = method::kValidatorInfo,
     .valueMethod = &byRef<&doValidatorInfo>,
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    {.name = method::kVaultInfo,
     .valueMethod = &byRef<&doVaultInfo>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kWalletPropose,
     .valueMethod = &byRef<&doWalletPropose>,
     .role = Role::ADMIN,
     .condition = Condition::NoCondition},
    // Event methods
    {.name = method::kSubscribe,
     .valueMethod = &byRef<&doSubscribe>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
    {.name = method::kUnsubscribe,
     .valueMethod = &byRef<&doUnsubscribe>,
     .role = Role::USER,
     .condition = Condition::NoCondition},
};

// The class-based handlers, which carry their name and API range as static
// members rather than as a table entry, so they cannot go in the array above.
constexpr Handler kClassHandlerArray[]{
    handlerFrom<LedgerHandler>(),
    handlerFrom<VersionHandler>(),
};

// The whole dispatch table: the two arrays above, concatenated. Their sizes are
// taken from the arrays so that adding a handler to either needs no change here.
constexpr auto kHandlers = [] {
    std::array<Handler, std::size(kHandlerArray) + std::size(kClassHandlerArray)> all{};
    auto const out = std::ranges::copy(kHandlerArray, all.begin()).out;
    std::ranges::copy(kClassHandlerArray, out);

    // Sorted by name, so a handler can be found by binary search.
    std::ranges::sort(all, {}, &Handler::name);
    return all;
}();

// getHandler() relies on this being sorted to binary search it, and
// kHandlerNames below inherits the order.
static_assert(
    std::ranges::is_sorted(kHandlers, {}, &Handler::name),
    "xrpl::rpc : kHandlers must be sorted by name");

// A name must select exactly one handler, otherwise a request would have two
// answers. Where a method's behaviour differs by API version, the handler
// branches on context.apiVersion rather than being registered once per range.
// Checked here, at compile time, rather than on the first dispatch.
static_assert(
    []() {
        for (std::size_t i = 0; i < kHandlers.size(); ++i)
        {
            auto const& h = kHandlers[i];
            if (h.name.empty() || h.valueMethod == nullptr || h.minApiVer > h.maxApiVer ||
                h.maxApiVer > rpc::kApiMaximumValidVersion ||
                h.minApiVer < rpc::kApiMinimumSupportedVersion)
                return false;

            // Sorted, so a repeat can only be of the preceding entry.
            if (i > 0 && kHandlers[i - 1].name == h.name)
                return false;
        }
        return true;
    }(),
    "xrpl::rpc : every handler needs a unique name, a method, and a valid API "
    "version range");

// The names are handed to json::StaticString, and read as C strings from there,
// so each must be a view of a whole string literal rather than a slice of one.
// Rebuilding the view from its data() as a C string is what StaticString will
// do; a slice loses its tail that way, and an unterminated one does not compile.
static_assert(
    std::ranges::all_of(
        kHandlers,
        [](std::string_view name) { return std::string_view{name.data()} == name; },
        &Handler::name),
    "xrpl::rpc : every handler name must be null-terminated");

// The handler names, which are already distinct and sorted.
constexpr auto kHandlerNames = [] {
    std::array<std::string_view, kHandlers.size()> names{};
    std::ranges::transform(kHandlers, names.begin(), &Handler::name);
    return names;
}();

}  // namespace

Handler const*
getHandler(unsigned version, bool betaEnabled, std::string_view name)
{
    if (version < rpc::kApiMinimumSupportedVersion ||
        version > (betaEnabled ? rpc::kApiBetaVersion : rpc::kApiMaximumSupportedVersion))
        return nullptr;

    // Names are unique, so the binary search finds the only candidate; it then
    // answers this request only if it serves this version.
    auto const i = std::ranges::lower_bound(kHandlers, name, {}, &Handler::name);
    if (i == kHandlers.end() || i->name != name)
        return nullptr;

    if (i->minApiVer <= version && version <= i->maxApiVer)
        return &*i;

    return nullptr;
}

std::span<std::string_view const>
getHandlerNames()
{
    return kHandlerNames;
}

}  // namespace xrpl::rpc
