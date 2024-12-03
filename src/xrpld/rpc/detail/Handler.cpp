//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <xrpld/rpc/detail/Handler.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/handlers/Handlers.h>
#include <xrpld/rpc/handlers/Version.h>
#include <xrpl/basics/contract.h>

#include <map>

namespace ripple {
namespace RPC {
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
            UNREACHABLE("ripple::RPC::byRef : result is object");
            result = RPC::makeObjectValue(result);
        }

        return Status();
    };
}

template <class Object, class HandlerImpl>
Status
handle(JsonContext& context, Object& object)
{
    ASSERT(
        context.apiVersion >= HandlerImpl::minApiVer &&
            context.apiVersion <= HandlerImpl::maxApiVer,
        "ripple::RPC::handle : valid API version");
    HandlerImpl handler(context);

    auto status = handler.check();
    if (status)
        status.inject(object);
    else
        handler.writeResult(object);
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
    {"account_info", byRef(&doAccountInfo), Role::USER, NO_CONDITION},
    {"account_currencies",
     byRef(&doAccountCurrencies),
     Role::USER,
     NO_CONDITION},
    {"account_lines", byRef(&doAccountLines), Role::USER, NO_CONDITION},
    {"account_channels", byRef(&doAccountChannels), Role::USER, NO_CONDITION},
    {"account_nfts", byRef(&doAccountNFTs), Role::USER, NO_CONDITION},
    {"account_objects", byRef(&doAccountObjects), Role::USER, NO_CONDITION},
    {"account_offers", byRef(&doAccountOffers), Role::USER, NO_CONDITION},
    {"account_tx", byRef(&doAccountTxJson), Role::USER, NO_CONDITION},
    {"amm_info", byRef(&doAMMInfo), Role::USER, NO_CONDITION},
    {"blacklist", byRef(&doBlackList), Role::ADMIN, NO_CONDITION},
    {"book_changes", byRef(&doBookChanges), Role::USER, NO_CONDITION},
    {"book_offers", byRef(&doBookOffers), Role::USER, NO_CONDITION},
    {"can_delete", byRef(&doCanDelete), Role::ADMIN, NO_CONDITION},
    {"channel_authorize", byRef(&doChannelAuthorize), Role::USER, NO_CONDITION},
    {"channel_verify", byRef(&doChannelVerify), Role::USER, NO_CONDITION},
    {"connect", byRef(&doConnect), Role::ADMIN, NO_CONDITION},
    {"consensus_info", byRef(&doConsensusInfo), Role::ADMIN, NO_CONDITION},
    {"deposit_authorized",
     byRef(&doDepositAuthorized),
     Role::USER,
     NO_CONDITION},
    {"feature", byRef(&doFeature), Role::USER, NO_CONDITION},
    {"fee", byRef(&doFee), Role::USER, NEEDS_CURRENT_LEDGER},
    {"fetch_info", byRef(&doFetchInfo), Role::ADMIN, NO_CONDITION},
    {"gateway_balances", byRef(&doGatewayBalances), Role::USER, NO_CONDITION},
    {"get_counts", byRef(&doGetCounts), Role::ADMIN, NO_CONDITION},
    {"get_aggregate_price",
     byRef(&doGetAggregatePrice),
     Role::USER,
     NO_CONDITION},
    {"ledger_accept",
     byRef(&doLedgerAccept),
     Role::ADMIN,
     NEEDS_CURRENT_LEDGER},
    {"ledger_cleaner",
     byRef(&doLedgerCleaner),
     Role::ADMIN,
     NEEDS_NETWORK_CONNECTION},
    {"ledger_closed", byRef(&doLedgerClosed), Role::USER, NEEDS_CLOSED_LEDGER},
    {"ledger_current",
     byRef(&doLedgerCurrent),
     Role::USER,
     NEEDS_CURRENT_LEDGER},
    {"ledger_data", byRef(&doLedgerData), Role::USER, NO_CONDITION},
    {"ledger_entry", byRef(&doLedgerEntry), Role::USER, NO_CONDITION},
    {"ledger_header", byRef(&doLedgerHeader), Role::USER, NO_CONDITION, 1, 1},
    {"ledger_request", byRef(&doLedgerRequest), Role::ADMIN, NO_CONDITION},
    {"log_level", byRef(&doLogLevel), Role::ADMIN, NO_CONDITION},
    {"logrotate", byRef(&doLogRotate), Role::ADMIN, NO_CONDITION},
    {"manifest", byRef(&doManifest), Role::USER, NO_CONDITION},
    {"nft_buy_offers", byRef(&doNFTBuyOffers), Role::USER, NO_CONDITION},
    {"nft_sell_offers", byRef(&doNFTSellOffers), Role::USER, NO_CONDITION},
    {"noripple_check", byRef(&doNoRippleCheck), Role::USER, NO_CONDITION},
    {"owner_info", byRef(&doOwnerInfo), Role::USER, NEEDS_CURRENT_LEDGER},
    {"peers", byRef(&doPeers), Role::ADMIN, NO_CONDITION},
    {"path_find", byRef(&doPathFind), Role::USER, NEEDS_CURRENT_LEDGER},
    {"ping", byRef(&doPing), Role::USER, NO_CONDITION},
    {"print", byRef(&doPrint), Role::ADMIN, NO_CONDITION},
    //      {   "profile",              byRef (&doProfile), Role::USER,
    //      NEEDS_CURRENT_LEDGER  },
    {"random", byRef(&doRandom), Role::USER, NO_CONDITION},
    {"peer_reservations_add",
     byRef(&doPeerReservationsAdd),
     Role::ADMIN,
     NO_CONDITION},
    {"peer_reservations_del",
     byRef(&doPeerReservationsDel),
     Role::ADMIN,
     NO_CONDITION},
    {"peer_reservations_list",
     byRef(&doPeerReservationsList),
     Role::ADMIN,
     NO_CONDITION},
    {"ripple_path_find", byRef(&doRipplePathFind), Role::USER, NO_CONDITION},
    {"server_definitions",
     byRef(&doServerDefinitions),
     Role::USER,
     NO_CONDITION},
    {"server_info", byRef(&doServerInfo), Role::USER, NO_CONDITION},
    {"server_state", byRef(&doServerState), Role::USER, NO_CONDITION},
    {"sign", byRef(&doSign), Role::USER, NO_CONDITION},
    {"sign_for", byRef(&doSignFor), Role::USER, NO_CONDITION},
    {"stop", byRef(&doStop), Role::ADMIN, NO_CONDITION},
    {"submit", byRef(&doSubmit), Role::USER, NEEDS_CURRENT_LEDGER},
    {"submit_multisigned",
     byRef(&doSubmitMultiSigned),
     Role::USER,
     NEEDS_CURRENT_LEDGER},
    {"transaction_entry", byRef(&doTransactionEntry), Role::USER, NO_CONDITION},
    {"tx", byRef(&doTxJson), Role::USER, NEEDS_NETWORK_CONNECTION},
    {"tx_history", byRef(&doTxHistory), Role::USER, NO_CONDITION, 1, 1},
    {"tx_reduce_relay", byRef(&doTxReduceRelay), Role::USER, NO_CONDITION},
    {"unl_list", byRef(&doUnlList), Role::ADMIN, NO_CONDITION},
    {"validation_create",
     byRef(&doValidationCreate),
     Role::ADMIN,
     NO_CONDITION},
    {"validators", byRef(&doValidators), Role::ADMIN, NO_CONDITION},
    {"validator_list_sites",
     byRef(&doValidatorListSites),
     Role::ADMIN,
     NO_CONDITION},
    {"validator_info", byRef(&doValidatorInfo), Role::ADMIN, NO_CONDITION},
    {"wallet_propose", byRef(&doWalletPropose), Role::ADMIN, NO_CONDITION},
    // Evented methods
    {"subscribe", byRef(&doSubscribe), Role::USER, NO_CONDITION},
    {"unsubscribe", byRef(&doUnsubscribe), Role::USER, NO_CONDITION},
};

class HandlerTable
{
private:
    using handler_table_t = std::multimap<std::string, Handler>;

    // Use with equal_range to enforce that API range of a newly added handler
    // does not overlap with API range of an existing handler with same name
    [[nodiscard]] bool
    overlappingApiVersion(
        std::pair<handler_table_t::iterator, handler_table_t::iterator> range,
        unsigned minVer,
        unsigned maxVer)
    {
        ASSERT(
            minVer <= maxVer,
            "ripple::RPC::HandlerTable : valid API version range");
        ASSERT(
            maxVer <= RPC::apiMaximumValidVersion,
            "ripple::RPC::HandlerTable : valid max API version");

        return std::any_of(
            range.first,
            range.second,  //
            [minVer, maxVer](auto const& item) {
                return item.second.minApiVer_ <= maxVer &&
                    item.second.maxApiVer_ >= minVer;
            });
    }

    template <std::size_t N>
    explicit HandlerTable(const Handler (&entries)[N])
    {
        for (auto const& entry : entries)
        {
            if (overlappingApiVersion(
                    table_.equal_range(entry.name_),
                    entry.minApiVer_,
                    entry.maxApiVer_))
                LogicError(
                    std::string("Handler for ") + entry.name_ +
                    " overlaps with an existing handler");

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
    getHandler(unsigned version, bool betaEnabled, std::string const& name)
        const
    {
        if (version < RPC::apiMinimumSupportedVersion ||
            version > (betaEnabled ? RPC::apiBetaVersion
                                   : RPC::apiMaximumSupportedVersion))
            return nullptr;

        auto const range = table_.equal_range(name);
        auto const i = std::find_if(
            range.first, range.second, [version](auto const& entry) {
                return entry.second.minApiVer_ <= version &&
                    version <= entry.second.maxApiVer_;
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
        static_assert(
            RPC::apiMinimumSupportedVersion <= HandlerImpl::minApiVer);

        if (overlappingApiVersion(
                table_.equal_range(HandlerImpl::name),
                HandlerImpl::minApiVer,
                HandlerImpl::maxApiVer))
            LogicError(
                std::string("Handler for ") + HandlerImpl::name +
                " overlaps with an existing handler");

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

}  // namespace RPC
}  // namespace ripple
