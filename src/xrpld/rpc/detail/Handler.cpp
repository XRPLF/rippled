#include <xrpld/rpc/detail/Handler.h>

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/MethodNames.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/detail/SpecBridge.hpp>
#include <xrpld/rpc/handlers/Handlers.h>
#include <xrpld/rpc/handlers/ledger/Ledger.h>
#include <xrpld/rpc/handlers/orderbook/BookChanges.h>
#include <xrpld/rpc/handlers/server_info/Version.h>
#include <xrpld/rpc/handlers/transaction/TransactionEntry.h>

#include <xrpl/basics/StringUtilities.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ApiVersion.h>

#include <rpcspec/Errors.hpp>
#include <rpcspec/Types.hpp>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <expected>
#include <span>
#include <string_view>
#include <utility>

namespace xrpl::rpc {
namespace {

// Shorthand: the tables below name this type once per entry.
using Method = Handler::Method;

/**
 * Adjust an old-style handler to be call-by-reference.
 *
 * The handler is a template parameter rather than an argument, so that byRef
 * names a plain function instead of returning a closure over it.
 */
template <json::Value (*Function)(JsonContext&)>
::rpc::Status
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

    return ::rpc::Status::kOK;
}

template <typename HandlerImpl>
concept SpecBasedHandler = std::derived_from<HandlerImpl, HandlerFor<typename HandlerImpl::Input>>;

template <typename HandlerImpl>
concept InputlessHandler = requires(HandlerImpl& handler) { handler.process(); };

template <class HandlerImpl, class Output>
::rpc::Status
respond(
    HandlerImpl const& handler,
    json::Value& object,
    std::expected<Output, ::rpc::Status> const& output,
    ::rpc::spec::Warnings const& warnings)
{
    if (not output.has_value())
    {
        injectSpecError(object, output.error());
        return output.error();
    }

    json::Value result;
    handler.writeResult(result, *output);
    injectSpecWarnings(result, warnings);
    object = std::move(result);

    return ::rpc::Status::kOK;
}

template <class HandlerImpl>
::rpc::Status
handle(JsonContext& context, json::Value& object)
    requires SpecBasedHandler<HandlerImpl> or InputlessHandler<HandlerImpl>
{
    if constexpr (SpecBasedHandler<HandlerImpl>)
    {
        auto const input = HandlerImpl::parseInput(context.params, context.apiVersion);
        if (not input.has_value())
        {
            injectSpecError(object, input.error());
            return input.error();
        }

        HandlerImpl handler(context);
        auto const output = handler.process(*input);
        auto const warnings = output.has_value()
            ? HandlerImpl::spec(context.apiVersion).check(context.params)
            : ::rpc::spec::Warnings{};
        return respond(handler, object, output, warnings);
    }
    else
    {
        HandlerImpl handler(context);
        return respond(handler, object, handler.process(), /* warnings= */ {});
    }
}

constexpr auto kHandlerArray = std::to_array<Handler>({
    // Request-response methods
    {
        .name = method::kAccountInfo,
        .valueMethod = Method::of<&byRef<&doAccountInfo>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kAccountCurrencies,
        .valueMethod = Method::of<&byRef<&doAccountCurrencies>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kAccountLines,
        .valueMethod = Method::of<&byRef<&doAccountLines>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kAccountChannels,
        .valueMethod = Method::of<&byRef<&doAccountChannels>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kAccountNfts,
        .valueMethod = Method::of<&byRef<&doAccountNFTs>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kAccountObjects,
        .valueMethod = Method::of<&byRef<&doAccountObjects>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kAccountOffers,
        .valueMethod = Method::of<&byRef<&doAccountOffers>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kAccountTx,
        .valueMethod = Method::of<&byRef<&doAccountTx>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kAmmInfo,
        .valueMethod = Method::of<&byRef<&doAMMInfo>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kBlacklist,
        .valueMethod = Method::of<&byRef<&doBlackList>>(),
        .role = Role::ADMIN,
        .condition = Condition::NoCondition,
        .hasCommandLineForm = false,
    },
    {
        .name = method::kBookChanges,
        .valueMethod = Method::of<&handle<BookChangesHandler>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kBookOffers,
        .valueMethod = Method::of<&byRef<&doBookOffers>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kCanDelete,
        .valueMethod = Method::of<&byRef<&doCanDelete>>(),
        .role = Role::ADMIN,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kChannelAuthorize,
        .valueMethod = Method::of<&byRef<&doChannelAuthorize>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kChannelVerify,
        .valueMethod = Method::of<&byRef<&doChannelVerify>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kConnect,
        .valueMethod = Method::of<&byRef<&doConnect>>(),
        .role = Role::ADMIN,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kConsensusInfo,
        .valueMethod = Method::of<&byRef<&doConsensusInfo>>(),
        .role = Role::ADMIN,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kDepositAuthorized,
        .valueMethod = Method::of<&byRef<&doDepositAuthorized>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kFeature,
        .valueMethod = Method::of<&byRef<&doFeature>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kFee,
        .valueMethod = Method::of<&byRef<&doFee>>(),
        .role = Role::USER,
        .condition = Condition::NeedsCurrentLedger,
        .hasCommandLineForm = false,
    },
    {
        .name = method::kFetchInfo,
        .valueMethod = Method::of<&byRef<&doFetchInfo>>(),
        .role = Role::ADMIN,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kGatewayBalances,
        .valueMethod = Method::of<&byRef<&doGatewayBalances>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kGetCounts,
        .valueMethod = Method::of<&byRef<&doGetCounts>>(),
        .role = Role::ADMIN,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kGetAggregatePrice,
        .valueMethod = Method::of<&byRef<&doGetAggregatePrice>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
        .hasCommandLineForm = false,
    },
    {
        .name = method::kLedger,
        .valueMethod = Method::of<&handle<LedgerHandler>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kLedgerAccept,
        .valueMethod = Method::of<&byRef<&doLedgerAccept>>(),
        .role = Role::ADMIN,
        .condition = Condition::NeedsCurrentLedger,
    },
    {
        .name = method::kLedgerCleaner,
        .valueMethod = Method::of<&byRef<&doLedgerCleaner>>(),
        .role = Role::ADMIN,
        .condition = Condition::NeedsNetworkConnection,
        .hasCommandLineForm = false,
    },
    {
        .name = method::kLedgerClosed,
        .valueMethod = Method::of<&byRef<&doLedgerClosed>>(),
        .role = Role::USER,
        .condition = Condition::NeedsClosedLedger,
    },
    {
        .name = method::kLedgerCurrent,
        .valueMethod = Method::of<&byRef<&doLedgerCurrent>>(),
        .role = Role::USER,
        .condition = Condition::NeedsCurrentLedger,
    },
    {
        .name = method::kLedgerData,
        .valueMethod = Method::of<&byRef<&doLedgerData>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
        .hasCommandLineForm = false,
    },
    {
        .name = method::kLedgerEntry,
        .valueMethod = Method::of<&byRef<&doLedgerEntry>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kLedgerHeader,
        .valueMethod = Method::of<&byRef<&doLedgerHeader>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
        .minApiVer = 1,
        .maxApiVer = 1,
    },
    {
        .name = method::kLedgerRequest,
        .valueMethod = Method::of<&byRef<&doLedgerRequest>>(),
        .role = Role::ADMIN,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kLogLevel,
        .valueMethod = Method::of<&byRef<&doLogLevel>>(),
        .role = Role::ADMIN,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kLogrotate,
        .valueMethod = Method::of<&byRef<&doLogRotate>>(),
        .role = Role::ADMIN,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kManifest,
        .valueMethod = Method::of<&byRef<&doManifest>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kNftBuyOffers,
        .valueMethod = Method::of<&byRef<&doNFTBuyOffers>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
        .hasCommandLineForm = false,
    },
    {
        .name = method::kNftSellOffers,
        .valueMethod = Method::of<&byRef<&doNFTSellOffers>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
        .hasCommandLineForm = false,
    },
    {
        .name = method::kNorippleCheck,
        .valueMethod = Method::of<&byRef<&doNoRippleCheck>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
        .hasCommandLineForm = false,
    },
    {
        .name = method::kOwnerInfo,
        .valueMethod = Method::of<&byRef<&doOwnerInfo>>(),
        .role = Role::USER,
        .condition = Condition::NeedsCurrentLedger,
    },
    {
        .name = method::kPeers,
        .valueMethod = Method::of<&byRef<&doPeers>>(),
        .role = Role::ADMIN,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kPathFind,
        .valueMethod = Method::of<&byRef<&doPathFind>>(),
        .role = Role::USER,
        .condition = Condition::NeedsCurrentLedger,
    },
    {
        .name = method::kPing,
        .valueMethod = Method::of<&byRef<&doPing>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kPrint,
        .valueMethod = Method::of<&byRef<&doPrint>>(),
        .role = Role::ADMIN,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kRandom,
        .valueMethod = Method::of<&byRef<&doRandom>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kPeerReservationsAdd,
        .valueMethod = Method::of<&byRef<&doPeerReservationsAdd>>(),
        .role = Role::ADMIN,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kPeerReservationsDel,
        .valueMethod = Method::of<&byRef<&doPeerReservationsDel>>(),
        .role = Role::ADMIN,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kPeerReservationsList,
        .valueMethod = Method::of<&byRef<&doPeerReservationsList>>(),
        .role = Role::ADMIN,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kRipplePathFind,
        .valueMethod = Method::of<&byRef<&doRipplePathFind>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kServerDefinitions,
        .valueMethod = Method::of<&byRef<&doServerDefinitions>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kServerInfo,
        .valueMethod = Method::of<&byRef<&doServerInfo>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kServerState,
        .valueMethod = Method::of<&byRef<&doServerState>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = "shed",
        .valueMethod = Method::of<&byRef<&doShed>>(),
        .role = Role::ADMIN,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kSign,
        .valueMethod = Method::of<&byRef<&doSign>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kSignFor,
        .valueMethod = Method::of<&byRef<&doSignFor>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kSimulate,
        .valueMethod = Method::of<&byRef<&doSimulate>>(),
        .role = Role::USER,
        .condition = Condition::NeedsCurrentLedger,
    },
    {
        .name = method::kStop,
        .valueMethod = Method::of<&byRef<&doStop>>(),
        .role = Role::ADMIN,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kSubmit,
        .valueMethod = Method::of<&byRef<&doSubmit>>(),
        .role = Role::USER,
        .condition = Condition::NeedsCurrentLedger,
    },
    {
        .name = method::kSubmitMultisigned,
        .valueMethod = Method::of<&byRef<&doSubmitMultiSigned>>(),
        .role = Role::USER,
        .condition = Condition::NeedsCurrentLedger,
    },
    {
        .name = method::kTransactionEntry,
        .valueMethod = Method::of<&handle<TransactionEntryHandler>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kTx,
        .valueMethod = Method::of<&byRef<&doTxJson>>(),
        .role = Role::USER,
        .condition = Condition::NeedsNetworkConnection,
    },
    {
        .name = method::kTxHistory,
        .valueMethod = Method::of<&byRef<&doTxHistory>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
        .minApiVer = 1,
        .maxApiVer = 1,
    },
    {
        .name = method::kTxReduceRelay,
        .valueMethod = Method::of<&byRef<&doTxReduceRelay>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
        .hasCommandLineForm = false,
    },
    {
        .name = method::kUnlList,
        .valueMethod = Method::of<&byRef<&doUnlList>>(),
        .role = Role::ADMIN,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kValidationCreate,
        .valueMethod = Method::of<&byRef<&doValidationCreate>>(),
        .role = Role::ADMIN,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kValidators,
        .valueMethod = Method::of<&byRef<&doValidators>>(),
        .role = Role::ADMIN,
        .condition = Condition::NoCondition,
        .hasCommandLineForm = false,
    },
    {
        .name = method::kValidatorListSites,
        .valueMethod = Method::of<&byRef<&doValidatorListSites>>(),
        .role = Role::ADMIN,
        .condition = Condition::NoCondition,
        .hasCommandLineForm = false,
    },
    {
        .name = method::kValidatorInfo,
        .valueMethod = Method::of<&byRef<&doValidatorInfo>>(),
        .role = Role::ADMIN,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kVaultInfo,
        .valueMethod = Method::of<&byRef<&doVaultInfo>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kVersion,
        .valueMethod = Method::of<&handle<VersionHandler>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kWalletPropose,
        .valueMethod = Method::of<&byRef<&doWalletPropose>>(),
        .role = Role::ADMIN,
        .condition = Condition::NoCondition,
    },
    // Event methods
    {
        .name = method::kSubscribe,
        .valueMethod = Method::of<&byRef<&doSubscribe>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
    {
        .name = method::kUnsubscribe,
        .valueMethod = Method::of<&byRef<&doUnsubscribe>>(),
        .role = Role::USER,
        .condition = Condition::NoCondition,
    },
});

// The whole dispatch table.
constexpr auto kHandlers = [] {
    auto all = kHandlerArray;

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
//
// The method is not checked: Handler::Method has no default constructor, so an
// entry that omits it does not compile.
static_assert(
    [] {
        for (std::size_t i = 0; i < kHandlers.size(); ++i)
        {
            auto const& h = kHandlers[i];
            if (h.name.empty() || h.minApiVer > h.maxApiVer ||
                h.maxApiVer > rpc::kApiMaximumValidVersion ||
                h.minApiVer < rpc::kApiMinimumSupportedVersion)
                return false;

            // Sorted, so a repeat can only be of the preceding entry.
            if (i > 0 && kHandlers[i - 1].name == h.name)
                return false;
        }
        return true;
    }(),
    "xrpl::rpc : every handler needs a unique name and a valid API version range");

/**
 * Convert the handler names to a form that may be read as C strings.
 *
 * NullTerminatedView's constructor rejects a name that does not reach its
 * terminating null, so this replaces the separate assertion that used to check
 * the same property. It is consteval because that constructor is.
 *
 * @tparam I The indices of kHandlers.
 * @return The names, in the order kHandlers holds them, which is sorted.
 */
template <std::size_t... I>
consteval auto
checkedHandlerNames(std::index_sequence<I...>)
{
    return std::array<NullTerminatedView, sizeof...(I)>{NullTerminatedView{kHandlers[I].name}...};
}

// The handler names, which are already distinct and sorted.
constexpr auto kHandlerNames = checkedHandlerNames(std::make_index_sequence<kHandlers.size()>{});

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

std::span<NullTerminatedView const>
getHandlerNames()
{
    return kHandlerNames;
}

}  // namespace xrpl::rpc
