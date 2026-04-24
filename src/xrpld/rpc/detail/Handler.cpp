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
Handler::Method<Json::Value>
byRef(Function const& f)
{
    return [f](JsonContext& context, Json::Value& result) {
        result = f(context);
        if (result.type() != Json::objectValue)
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
        &handle<Json::Value, HandlerImpl>,
        HandlerImpl::role,
        HandlerImpl::condition,
        HandlerImpl::minApiVer,
        HandlerImpl::maxApiVer};
}

Handler const handlerArray[]{
    // Some handlers not specified here are added to the table via addHandler()
    // Request-response methods
    {.name_ = "account_info",
     .valueMethod_ = byRef(&doAccountInfo),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "account_currencies",
     .valueMethod_ = byRef(&doAccountCurrencies),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "account_lines",
     .valueMethod_ = byRef(&doAccountLines),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "account_channels",
     .valueMethod_ = byRef(&doAccountChannels),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "account_nfts",
     .valueMethod_ = byRef(&doAccountNFTs),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "account_objects",
     .valueMethod_ = byRef(&doAccountObjects),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "account_offers",
     .valueMethod_ = byRef(&doAccountOffers),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "account_tx",
     .valueMethod_ = byRef(&doAccountTx),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "amm_info",
     .valueMethod_ = byRef(&doAMMInfo),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "blacklist",
     .valueMethod_ = byRef(&doBlackList),
     .role_ = Role::ADMIN,
     .condition_ = NO_CONDITION},
    {.name_ = "book_changes",
     .valueMethod_ = byRef(&doBookChanges),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "book_offers",
     .valueMethod_ = byRef(&doBookOffers),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "can_delete",
     .valueMethod_ = byRef(&doCanDelete),
     .role_ = Role::ADMIN,
     .condition_ = NO_CONDITION},
    {.name_ = "channel_authorize",
     .valueMethod_ = byRef(&doChannelAuthorize),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "channel_verify",
     .valueMethod_ = byRef(&doChannelVerify),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "connect",
     .valueMethod_ = byRef(&doConnect),
     .role_ = Role::ADMIN,
     .condition_ = NO_CONDITION},
    {.name_ = "consensus_info",
     .valueMethod_ = byRef(&doConsensusInfo),
     .role_ = Role::ADMIN,
     .condition_ = NO_CONDITION},
    {.name_ = "deposit_authorized",
     .valueMethod_ = byRef(&doDepositAuthorized),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "feature",
     .valueMethod_ = byRef(&doFeature),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "fee",
     .valueMethod_ = byRef(&doFee),
     .role_ = Role::USER,
     .condition_ = NEEDS_CURRENT_LEDGER},
    {.name_ = "fetch_info",
     .valueMethod_ = byRef(&doFetchInfo),
     .role_ = Role::ADMIN,
     .condition_ = NO_CONDITION},
    {.name_ = "gateway_balances",
     .valueMethod_ = byRef(&doGatewayBalances),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "get_counts",
     .valueMethod_ = byRef(&doGetCounts),
     .role_ = Role::ADMIN,
     .condition_ = NO_CONDITION},
    {.name_ = "get_aggregate_price",
     .valueMethod_ = byRef(&doGetAggregatePrice),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "ledger_accept",
     .valueMethod_ = byRef(&doLedgerAccept),
     .role_ = Role::ADMIN,
     .condition_ = NEEDS_CURRENT_LEDGER},
    {.name_ = "ledger_cleaner",
     .valueMethod_ = byRef(&doLedgerCleaner),
     .role_ = Role::ADMIN,
     .condition_ = NEEDS_NETWORK_CONNECTION},
    {.name_ = "ledger_closed",
     .valueMethod_ = byRef(&doLedgerClosed),
     .role_ = Role::USER,
     .condition_ = NEEDS_CLOSED_LEDGER},
    {.name_ = "ledger_current",
     .valueMethod_ = byRef(&doLedgerCurrent),
     .role_ = Role::USER,
     .condition_ = NEEDS_CURRENT_LEDGER},
    {.name_ = "ledger_data",
     .valueMethod_ = byRef(&doLedgerData),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "ledger_entry",
     .valueMethod_ = byRef(&doLedgerEntry),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "ledger_header",
     .valueMethod_ = byRef(&doLedgerHeader),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION,
     .minApiVer_ = 1,
     .maxApiVer_ = 1},
    {.name_ = "ledger_request",
     .valueMethod_ = byRef(&doLedgerRequest),
     .role_ = Role::ADMIN,
     .condition_ = NO_CONDITION},
    {.name_ = "log_level",
     .valueMethod_ = byRef(&doLogLevel),
     .role_ = Role::ADMIN,
     .condition_ = NO_CONDITION},
    {.name_ = "logrotate",
     .valueMethod_ = byRef(&doLogRotate),
     .role_ = Role::ADMIN,
     .condition_ = NO_CONDITION},
    {.name_ = "manifest",
     .valueMethod_ = byRef(&doManifest),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "nft_buy_offers",
     .valueMethod_ = byRef(&doNFTBuyOffers),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "nft_sell_offers",
     .valueMethod_ = byRef(&doNFTSellOffers),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "noripple_check",
     .valueMethod_ = byRef(&doNoRippleCheck),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "owner_info",
     .valueMethod_ = byRef(&doOwnerInfo),
     .role_ = Role::USER,
     .condition_ = NEEDS_CURRENT_LEDGER},
    {.name_ = "peers",
     .valueMethod_ = byRef(&doPeers),
     .role_ = Role::ADMIN,
     .condition_ = NO_CONDITION},
    {.name_ = "path_find",
     .valueMethod_ = byRef(&doPathFind),
     .role_ = Role::USER,
     .condition_ = NEEDS_CURRENT_LEDGER},
    {.name_ = "ping",
     .valueMethod_ = byRef(&doPing),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "print",
     .valueMethod_ = byRef(&doPrint),
     .role_ = Role::ADMIN,
     .condition_ = NO_CONDITION},
    //      {   "profile",              byRef (&doProfile), Role::USER,
    //      NEEDS_CURRENT_LEDGER  },
    {.name_ = "random",
     .valueMethod_ = byRef(&doRandom),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "peer_reservations_add",
     .valueMethod_ = byRef(&doPeerReservationsAdd),
     .role_ = Role::ADMIN,
     .condition_ = NO_CONDITION},
    {.name_ = "peer_reservations_del",
     .valueMethod_ = byRef(&doPeerReservationsDel),
     .role_ = Role::ADMIN,
     .condition_ = NO_CONDITION},
    {.name_ = "peer_reservations_list",
     .valueMethod_ = byRef(&doPeerReservationsList),
     .role_ = Role::ADMIN,
     .condition_ = NO_CONDITION},
    {.name_ = "ripple_path_find",
     .valueMethod_ = byRef(&doRipplePathFind),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "server_definitions",
     .valueMethod_ = byRef(&doServerDefinitions),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "server_info",
     .valueMethod_ = byRef(&doServerInfo),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "server_state",
     .valueMethod_ = byRef(&doServerState),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "sign",
     .valueMethod_ = byRef(&doSign),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "sign_for",
     .valueMethod_ = byRef(&doSignFor),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "simulate",
     .valueMethod_ = byRef(&doSimulate),
     .role_ = Role::USER,
     .condition_ = NEEDS_CURRENT_LEDGER},
    {.name_ = "stop",
     .valueMethod_ = byRef(&doStop),
     .role_ = Role::ADMIN,
     .condition_ = NO_CONDITION},
    {.name_ = "submit",
     .valueMethod_ = byRef(&doSubmit),
     .role_ = Role::USER,
     .condition_ = NEEDS_CURRENT_LEDGER},
    {.name_ = "submit_multisigned",
     .valueMethod_ = byRef(&doSubmitMultiSigned),
     .role_ = Role::USER,
     .condition_ = NEEDS_CURRENT_LEDGER},
    {.name_ = "transaction_entry",
     .valueMethod_ = byRef(&doTransactionEntry),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "tx",
     .valueMethod_ = byRef(&doTxJson),
     .role_ = Role::USER,
     .condition_ = NEEDS_NETWORK_CONNECTION},
    {.name_ = "tx_history",
     .valueMethod_ = byRef(&doTxHistory),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION,
     .minApiVer_ = 1,
     .maxApiVer_ = 1},
    {.name_ = "tx_reduce_relay",
     .valueMethod_ = byRef(&doTxReduceRelay),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "unl_list",
     .valueMethod_ = byRef(&doUnlList),
     .role_ = Role::ADMIN,
     .condition_ = NO_CONDITION},
    {.name_ = "validation_create",
     .valueMethod_ = byRef(&doValidationCreate),
     .role_ = Role::ADMIN,
     .condition_ = NO_CONDITION},
    {.name_ = "validators",
     .valueMethod_ = byRef(&doValidators),
     .role_ = Role::ADMIN,
     .condition_ = NO_CONDITION},
    {.name_ = "validator_list_sites",
     .valueMethod_ = byRef(&doValidatorListSites),
     .role_ = Role::ADMIN,
     .condition_ = NO_CONDITION},
    {.name_ = "validator_info",
     .valueMethod_ = byRef(&doValidatorInfo),
     .role_ = Role::ADMIN,
     .condition_ = NO_CONDITION},
    {.name_ = "vault_info",
     .valueMethod_ = byRef(&doVaultInfo),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "wallet_propose",
     .valueMethod_ = byRef(&doWalletPropose),
     .role_ = Role::ADMIN,
     .condition_ = NO_CONDITION},
    // Event methods
    {.name_ = "subscribe",
     .valueMethod_ = byRef(&doSubscribe),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
    {.name_ = "unsubscribe",
     .valueMethod_ = byRef(&doUnsubscribe),
     .role_ = Role::USER,
     .condition_ = NO_CONDITION},
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
            maxVer <= RPC::apiMaximumValidVersion,
            "xrpl::RPC::HandlerTable : valid max API version");

        return std::any_of(
            range.first,
            range.second,  //
            [minVer, maxVer](auto const& item) {
                return item.second.minApiVer_ <= maxVer && item.second.maxApiVer_ >= minVer;
            });
    }

    template <std::size_t N>
    explicit HandlerTable(Handler const (&entries)[N])
    {
        for (auto const& entry : entries)
        {
            if (overlappingApiVersion(
                    table_.equal_range(entry.name_), entry.minApiVer_, entry.maxApiVer_))
            {
                LogicError(
                    std::string("Handler for ") + entry.name_ +
                    " overlaps with an existing handler");
            }

            table_.insert({entry.name_, entry});
        }

        // This is where the new-style handlers are added.
        addHandler<LedgerHandler>();
        addHandler<VersionHandler>();
    }

public:
    static HandlerTable const&
    instance()
    {
        static HandlerTable const handlerTable(handlerArray);
        return handlerTable;
    }

    [[nodiscard]] Handler const*
    getHandler(unsigned version, bool betaEnabled, std::string const& name) const
    {
        if (version < RPC::apiMinimumSupportedVersion ||
            version > (betaEnabled ? RPC::apiBetaVersion : RPC::apiMaximumSupportedVersion))
            return nullptr;

        auto const range = table_.equal_range(name);
        auto const i = std::find_if(range.first, range.second, [version](auto const& entry) {
            return entry.second.minApiVer_ <= version && version <= entry.second.maxApiVer_;
        });

        return i == range.second ? nullptr : &i->second;
    }

    [[nodiscard]] std::set<char const*>
    getHandlerNames() const
    {
        std::set<char const*> ret;
        for (auto const& i : table_)
            ret.insert(i.second.name_);

        return ret;
    }

private:
    handler_table_t table_;

    template <class HandlerImpl>
    void
    addHandler()
    {
        static_assert(HandlerImpl::minApiVer <= HandlerImpl::maxApiVer);
        static_assert(HandlerImpl::maxApiVer <= RPC::apiMaximumValidVersion);
        static_assert(RPC::apiMinimumSupportedVersion <= HandlerImpl::minApiVer);

        if (overlappingApiVersion(
                table_.equal_range(HandlerImpl::name),
                HandlerImpl::minApiVer,
                HandlerImpl::maxApiVer))
        {
            LogicError(
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
