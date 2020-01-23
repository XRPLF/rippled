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

#include <ripple/rpc/impl/Handler.h>
#include <ripple/rpc/handlers/Handlers.h>
#include <ripple/rpc/handlers/Version.h>
#include <ripple/rpc/impl/RPCHelpers.h>

namespace ripple {
namespace RPC {
namespace {

/** Adjust an old-style handler to be call-by-reference. */
template <typename Function>
Handler::Method<Json::Value> byRef (Function const& f)
{
    return [f] (JsonContext& context, Json::Value& result)
    {
        result = f (context);
        if (result.type() != Json::objectValue)
        {
            assert (false);
            result = RPC::makeObjectValue (result);
        }

        return Status();
    };
}

template <class Object, class HandlerImpl>
Status handle (JsonContext& context, Object& object)
{
    HandlerImpl handler (context);

    auto status = handler.check ();
    if (status)
        status.inject (object);
    else
        handler.writeResult (object);
    return status;
};

Handler const handlerArray[] {
    // Some handlers not specified here are added to the table via addHandler()
    // Request-response methods
    {   "account_info",         byRef (&doAccountInfo),         Role::USER,  NO_CONDITION  },
    {   "account_currencies",   byRef (&doAccountCurrencies),   Role::USER,  NO_CONDITION  },
    {   "account_lines",        byRef (&doAccountLines),        Role::USER,  NO_CONDITION  },
    {   "account_channels",     byRef (&doAccountChannels),     Role::USER,  NO_CONDITION  },
    {   "account_objects",      byRef (&doAccountObjects),      Role::USER,  NO_CONDITION  },
    {   "account_offers",       byRef (&doAccountOffers),       Role::USER,  NO_CONDITION  },
    {   "account_tx",           byRef (&doAccountTxSwitch),     Role::USER,  NO_CONDITION  },
    {   "blacklist",            byRef (&doBlackList),           Role::ADMIN,   NO_CONDITION     },
    {   "book_offers",          byRef (&doBookOffers),          Role::USER,  NO_CONDITION  },
    {   "can_delete",           byRef (&doCanDelete),           Role::ADMIN,   NO_CONDITION     },
    {   "channel_authorize",    byRef (&doChannelAuthorize),    Role::USER,  NO_CONDITION  },
    {   "channel_verify",       byRef (&doChannelVerify),       Role::USER,  NO_CONDITION  },
    {   "connect",              byRef (&doConnect),             Role::ADMIN,   NO_CONDITION     },
    {   "consensus_info",       byRef (&doConsensusInfo),       Role::ADMIN,   NO_CONDITION     },
    {   "deposit_authorized",   byRef (&doDepositAuthorized),   Role::USER,  NO_CONDITION  },
    {   "download_shard",       byRef (&doDownloadShard),       Role::ADMIN,   NO_CONDITION     },
    {   "gateway_balances",     byRef (&doGatewayBalances),     Role::USER,  NO_CONDITION  },
    {   "get_counts",           byRef (&doGetCounts),           Role::ADMIN,   NO_CONDITION     },
    {   "feature",              byRef (&doFeature),             Role::ADMIN,   NO_CONDITION     },
    {   "fee",                  byRef (&doFee),                 Role::USER,    NEEDS_CURRENT_LEDGER     },
    {   "fetch_info",           byRef (&doFetchInfo),           Role::ADMIN,   NO_CONDITION     },
    {   "ledger_accept",        byRef (&doLedgerAccept),        Role::ADMIN,   NEEDS_CURRENT_LEDGER  },
    {   "ledger_cleaner",       byRef (&doLedgerCleaner),       Role::ADMIN,   NEEDS_NETWORK_CONNECTION  },
    {   "ledger_closed",        byRef (&doLedgerClosed),        Role::USER,  NO_CONDITION   },
    {   "ledger_current",       byRef (&doLedgerCurrent),       Role::USER,  NEEDS_CURRENT_LEDGER  },
    {   "ledger_data",          byRef (&doLedgerData),          Role::USER,  NO_CONDITION  },
    {   "ledger_entry",         byRef (&doLedgerEntry),         Role::USER,  NO_CONDITION  },
    {   "ledger_header",        byRef (&doLedgerHeader),        Role::USER,  NO_CONDITION  },
    {   "ledger_request",       byRef (&doLedgerRequest),       Role::ADMIN,   NO_CONDITION     },
    {   "log_level",            byRef (&doLogLevel),            Role::ADMIN,   NO_CONDITION     },
    {   "logrotate",            byRef (&doLogRotate),           Role::ADMIN,   NO_CONDITION     },
    {   "manifest",             byRef (&doManifest),            Role::ADMIN,   NO_CONDITION     },
    {   "noripple_check",       byRef (&doNoRippleCheck),       Role::USER,  NO_CONDITION  },
    {   "owner_info",           byRef (&doOwnerInfo),           Role::USER,  NEEDS_CURRENT_LEDGER  },
    {   "peers",                byRef (&doPeers),               Role::ADMIN,   NO_CONDITION     },
    {   "path_find",            byRef (&doPathFind),            Role::USER,  NEEDS_CURRENT_LEDGER  },
    {   "ping",                 byRef (&doPing),                Role::USER,  NO_CONDITION     },
    {   "print",                byRef (&doPrint),               Role::ADMIN,   NO_CONDITION     },
//      {   "profile",              byRef (&doProfile),             Role::USER,  NEEDS_CURRENT_LEDGER  },
    {   "random",               byRef (&doRandom),              Role::USER,  NO_CONDITION     },
    {   "peer_reservations_add",  byRef (&doPeerReservationsAdd),     Role::ADMIN,  NO_CONDITION  },
    {   "peer_reservations_del",  byRef (&doPeerReservationsDel),     Role::ADMIN,  NO_CONDITION  },
    {   "peer_reservations_list", byRef (&doPeerReservationsList),    Role::ADMIN,  NO_CONDITION  },
    {   "ripple_path_find",     byRef (&doRipplePathFind),      Role::USER,  NO_CONDITION  },
    {   "sign",                 byRef (&doSign),                Role::USER,  NO_CONDITION     },
    {   "sign_for",             byRef (&doSignFor),             Role::USER,  NO_CONDITION     },
    {   "submit",               byRef (&doSubmit),              Role::USER,  NEEDS_CURRENT_LEDGER  },
    {   "submit_multisigned",   byRef (&doSubmitMultiSigned),   Role::USER,  NEEDS_CURRENT_LEDGER  },
    {   "server_info",          byRef (&doServerInfo),          Role::USER,  NO_CONDITION     },
    {   "server_state",         byRef (&doServerState),         Role::USER,  NO_CONDITION     },
    {   "crawl_shards",         byRef (&doCrawlShards),         Role::ADMIN,  NO_CONDITION     },
    {   "stop",                 byRef (&doStop),                Role::ADMIN,   NO_CONDITION     },
    {   "transaction_entry",    byRef (&doTransactionEntry),    Role::USER,  NO_CONDITION  },
    {   "tx",                   byRef (&doTx),                  Role::USER,  NEEDS_NETWORK_CONNECTION  },
    {   "tx_history",           byRef (&doTxHistory),           Role::USER,  NO_CONDITION     },
    {   "unl_list",             byRef (&doUnlList),             Role::ADMIN,   NO_CONDITION     },
    {   "validation_create",    byRef (&doValidationCreate),    Role::ADMIN,   NO_CONDITION     },
    {   "validators",           byRef (&doValidators),          Role::ADMIN,   NO_CONDITION     },
    {   "validator_list_sites", byRef (&doValidatorListSites),  Role::ADMIN,   NO_CONDITION     },
    {   "validator_info",       byRef (&doValidatorInfo),       Role::ADMIN,   NO_CONDITION     },
    {   "wallet_propose",       byRef (&doWalletPropose),       Role::ADMIN,   NO_CONDITION     },

    // Evented methods
    {   "subscribe",            byRef (&doSubscribe),           Role::USER,  NO_CONDITION     },
    {   "unsubscribe",          byRef (&doUnsubscribe),         Role::USER,  NO_CONDITION     },
};

class HandlerTable {
  private:
    template<std::size_t N>
    explicit
    HandlerTable (const Handler(&entries)[N])
    {
        for(auto v = RPC::ApiMinimumSupportedVersion; v <= RPC::ApiMaximumSupportedVersion; ++v)
        {
            for (std::size_t i = 0; i < N; ++i)
            {
                auto & innerTable = table_[versionToIndex(v)];
                auto const& entry = entries[i];
                assert (innerTable.find(entry.name_) == innerTable.end());
                innerTable[entry.name_] = entry;
            }

            // This is where the new-style handlers are added.
            // This is also where different versions of handlers are added.
            addHandler<LedgerHandler>(v);
            addHandler<VersionHandler>(v);
        }
    }

  public:
    static HandlerTable const& instance()
    {
        static HandlerTable const handlerTable (handlerArray);
        return handlerTable;
    }

    Handler const* getHandler(unsigned version, std::string name) const
    {
        if(version > RPC::ApiMaximumSupportedVersion || version < RPC::ApiMinimumSupportedVersion)
            return nullptr;
        auto & innerTable = table_[versionToIndex(version)];
        auto i = innerTable.find(name);
        return i == innerTable.end() ? nullptr : &i->second;
    }

    std::vector<char const*>
    getHandlerNames() const
    {
        std::unordered_set<char const*> name_set;
        for ( int index = 0; index < table_.size(); ++index)
        {
            for(auto const& h : table_[index])
            {
                name_set.insert(h.second.name_);
            }
        }
        return std::vector<char const*>(name_set.begin(), name_set.end());
    }

  private:
    std::array<std::map<std::string, Handler>, APINumberVersionSupported> table_;

    template <class HandlerImpl>
    void addHandler(unsigned version)
    {
        assert (version >= RPC::ApiMinimumSupportedVersion && version <= RPC::ApiMaximumSupportedVersion);
        auto & innerTable = table_[versionToIndex(version)];
        assert (innerTable.find(HandlerImpl::name()) == innerTable.end());

        Handler h;
        h.name_ = HandlerImpl::name();
        h.valueMethod_ = &handle<Json::Value, HandlerImpl>;
        h.role_ = HandlerImpl::role();
        h.condition_ = HandlerImpl::condition();

        innerTable[HandlerImpl::name()] = h;
    }

    inline unsigned versionToIndex(unsigned version) const
    {
        return version - RPC::ApiMinimumSupportedVersion;
    }
};

} // namespace

Handler const* getHandler(unsigned version, std::string const& name)
{
    return HandlerTable::instance().getHandler(version, name);
}

std::vector<char const*>
getHandlerNames()
{
    return HandlerTable::instance().getHandlerNames();
};

} // RPC
} // ripple
