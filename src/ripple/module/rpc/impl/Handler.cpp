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

#include <ripple/module/rpc/impl/Handler.h>
#include <ripple/module/rpc/handlers/Handlers.h>

namespace ripple {
namespace RPC {
namespace {

class HandlerTable {
  public:
    HandlerTable(std::vector<Handler> const& entries) {
        for (auto& entry: entries)
            table_[entry.name_] = entry;
    }

    const Handler* getHandler(std::string name) {
        auto i = table_.find(name);
        return i == table_.end() ? nullptr : &i->second;
    }

  private:
    std::map<std::string, Handler> table_;
};

HandlerTable HANDLERS({
    // Request-response methods
    {   "account_info",         &doAccountInfo,         Config::USER,  NEEDS_CURRENT_LEDGER  },
    {   "account_currencies",   &doAccountCurrencies,   Config::USER,  NEEDS_CURRENT_LEDGER  },
    {   "account_lines",        &doAccountLines,        Config::USER,  NEEDS_CURRENT_LEDGER  },
    {   "account_offers",       &doAccountOffers,       Config::USER,  NEEDS_CURRENT_LEDGER  },
    {   "account_tx",           &doAccountTxSwitch,     Config::USER,  NEEDS_NETWORK_CONNECTION  },
    {   "blacklist",            &doBlackList,           Config::ADMIN,   NO_CONDITION     },
    {   "book_offers",          &doBookOffers,          Config::USER,  NEEDS_CURRENT_LEDGER  },
    {   "connect",              &doConnect,             Config::ADMIN,   NO_CONDITION     },
    {   "consensus_info",       &doConsensusInfo,       Config::ADMIN,   NO_CONDITION     },
    {   "get_counts",           &doGetCounts,           Config::ADMIN,   NO_CONDITION     },
    {   "internal",             &doInternal,            Config::ADMIN,   NO_CONDITION     },
    {   "feature",              &doFeature,             Config::ADMIN,   NO_CONDITION     },
    {   "fetch_info",           &doFetchInfo,           Config::ADMIN,   NO_CONDITION     },
    {   "ledger",               &doLedger,              Config::USER,  NEEDS_NETWORK_CONNECTION  },
    {   "ledger_accept",        &doLedgerAccept,        Config::ADMIN,   NEEDS_CURRENT_LEDGER  },
    {   "ledger_cleaner",       &doLedgerCleaner,       Config::ADMIN,   NEEDS_NETWORK_CONNECTION  },
    {   "ledger_closed",        &doLedgerClosed,        Config::USER,  NEEDS_CLOSED_LEDGER   },
    {   "ledger_current",       &doLedgerCurrent,       Config::USER,  NEEDS_CURRENT_LEDGER  },
    {   "ledger_data",          &doLedgerData,          Config::USER,  NEEDS_CURRENT_LEDGER  },
    {   "ledger_entry",         &doLedgerEntry,         Config::USER,  NEEDS_CURRENT_LEDGER  },
    {   "ledger_header",        &doLedgerHeader,        Config::USER,  NEEDS_CURRENT_LEDGER  },
    {   "ledger_request",       &doLedgerRequest,       Config::ADMIN,   NO_CONDITION     },
    {   "log_level",            &doLogLevel,            Config::ADMIN,   NO_CONDITION     },
    {   "logrotate",            &doLogRotate,           Config::ADMIN,   NO_CONDITION     },
//      {   "nickname_info",        &doNicknameInfo,        Config::USER,  NEEDS_CURRENT_LEDGER  },
    {   "owner_info",           &doOwnerInfo,           Config::USER,  NEEDS_CURRENT_LEDGER  },
    {   "peers",                &doPeers,               Config::ADMIN,   NO_CONDITION     },
    {   "path_find",            &doPathFind,            Config::USER,  NEEDS_CURRENT_LEDGER  },
    {   "ping",                 &doPing,                Config::USER,  NO_CONDITION     },
    {   "print",                &doPrint,               Config::ADMIN,   NO_CONDITION     },
//      {   "profile",              &doProfile,             Config::USER,  NEEDS_CURRENT_LEDGER  },
    {   "proof_create",         &doProofCreate,         Config::ADMIN,   NO_CONDITION     },
    {   "proof_solve",          &doProofSolve,          Config::ADMIN,   NO_CONDITION     },
    {   "proof_verify",         &doProofVerify,         Config::ADMIN,   NO_CONDITION     },
    {   "random",               &doRandom,              Config::USER,  NO_CONDITION     },
    {   "ripple_path_find",     &doRipplePathFind,      Config::USER,  NEEDS_CURRENT_LEDGER  },
    {   "sign",                 &doSign,                Config::USER,  NO_CONDITION     },
    {   "submit",               &doSubmit,              Config::USER,  NEEDS_CURRENT_LEDGER  },
    {   "server_info",          &doServerInfo,          Config::USER,  NO_CONDITION     },
    {   "server_state",         &doServerState,         Config::USER,  NO_CONDITION     },
    {   "sms",                  &doSMS,                 Config::ADMIN,   NO_CONDITION     },
    {   "stop",                 &doStop,                Config::ADMIN,   NO_CONDITION     },
    {   "transaction_entry",    &doTransactionEntry,    Config::USER,  NEEDS_CURRENT_LEDGER  },
    {   "tx",                   &doTx,                  Config::USER,  NEEDS_NETWORK_CONNECTION  },
    {   "tx_history",           &doTxHistory,           Config::USER,  NO_CONDITION     },
    {   "unl_add",              &doUnlAdd,              Config::ADMIN,   NO_CONDITION     },
    {   "unl_delete",           &doUnlDelete,           Config::ADMIN,   NO_CONDITION     },
    {   "unl_list",             &doUnlList,             Config::ADMIN,   NO_CONDITION     },
    {   "unl_load",             &doUnlLoad,             Config::ADMIN,   NO_CONDITION     },
    {   "unl_network",          &doUnlNetwork,          Config::ADMIN,   NO_CONDITION     },
    {   "unl_reset",            &doUnlReset,            Config::ADMIN,   NO_CONDITION     },
    {   "unl_score",            &doUnlScore,            Config::ADMIN,   NO_CONDITION     },
    {   "validation_create",    &doValidationCreate,    Config::ADMIN,   NO_CONDITION     },
    {   "validation_seed",      &doValidationSeed,      Config::ADMIN,   NO_CONDITION     },
    {   "wallet_accounts",      &doWalletAccounts,      Config::USER,  NEEDS_CURRENT_LEDGER  },
    {   "wallet_propose",       &doWalletPropose,       Config::ADMIN,   NO_CONDITION     },
    {   "wallet_seed",          &doWalletSeed,          Config::ADMIN,   NO_CONDITION     },

    // Evented methods
    {   "subscribe",            &doSubscribe,           Config::USER,  NO_CONDITION     },
    {   "unsubscribe",          &doUnsubscribe,         Config::USER,  NO_CONDITION     },
});

} // namespace

const Handler* getHandler(std::string name) {
    return HANDLERS.getHandler(name);
}

} // RPC
} // ripple
