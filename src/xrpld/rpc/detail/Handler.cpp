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

/** Adjust an old-style handler to be call-by-reference. */
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

Handler const kHandlerArray[]{
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

class HandlerTable
{
private:
    using handler_table_t = std::multimap<std::string, Handler>;

    // Use with equal_range to enforce that API range of a newly added handler
    // does not overlap with API range of an existing handler with same name
    [[nodiscard]] static bool
    overlappingApiVersion(
        std::pair<handler_table_t::iterator, handler_table_t::iterator> range,
        unsigned minVer,
        unsigned maxVer)
    {
        XRPL_ASSERT(minVer <= maxVer, "xrpl::RPC::HandlerTable : valid API version range");
        XRPL_ASSERT(
            maxVer <= RPC::kApiMaximumValidVersion,
            "xrpl::RPC::HandlerTable : valid max API version");

        return std::any_of(
            range.first,
            range.second,  //
            [minVer, maxVer](auto const& item) {
                return item.second.minApiVer <= maxVer && item.second.maxApiVer >= minVer;
            });
    }

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
    static HandlerTable const&
    instance()
    {
        static HandlerTable const kHandlerTable(kHandlerArray);
        return kHandlerTable;
    }

    [[nodiscard]] Handler const*
    getHandler(unsigned version, bool betaEnabled, std::string const& name) const
    {
        if (version < RPC::kApiMinimumSupportedVersion ||
            version > (betaEnabled ? RPC::kApiBetaVersion : RPC::kApiMaximumSupportedVersion))
            return nullptr;

        auto const range = table_.equal_range(name);
        auto const i = std::find_if(range.first, range.second, [version](auto const& entry) {
            return entry.second.minApiVer <= version && version <= entry.second.maxApiVer;
        });

        return i == range.second ? nullptr : &i->second;
    }

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

    template <class HandlerImpl>
    void
    addHandler()
    {
        static_assert(HandlerImpl::minApiVer <= HandlerImpl::maxApiVer);
        static_assert(HandlerImpl::maxApiVer <= RPC::kApiMaximumValidVersion);
        static_assert(RPC::kApiMinimumSupportedVersion <= HandlerImpl::minApiVer);

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

Handler const*
getHandler(unsigned version, bool betaEnabled, std::string const& name)
{
    return HandlerTable::instance().getHandler(version, betaEnabled, name);
}

std::set<char const*>
getHandlerNames()
{
    return HandlerTable::instance().getHandlerNames();
}

}  // namespace xrpl::RPC
