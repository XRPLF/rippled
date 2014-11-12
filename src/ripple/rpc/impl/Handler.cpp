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
    {   "account_info",         &doAccountInfo,         Role::USER,  NEEDS_CURRENT_LEDGER  },
    {   "account_currencies",   &doAccountCurrencies,   Role::USER,  NEEDS_CURRENT_LEDGER  },
    {   "account_lines",        &doAccountLines,        Role::USER,  NEEDS_CURRENT_LEDGER  },
    {   "account_offers",       &doAccountOffers,       Role::USER,  NEEDS_CURRENT_LEDGER  },
    {   "account_tx",           &doAccountTxSwitch,     Role::USER,  NEEDS_NETWORK_CONNECTION  },
    {   "blacklist",            &doBlackList,           Role::ADMIN,   NO_CONDITION     },
    {   "book_offers",          &doBookOffers,          Role::USER,  NEEDS_CURRENT_LEDGER  },
    {   "connect",              &doConnect,             Role::ADMIN,   NO_CONDITION     },
    {   "consensus_info",       &doConsensusInfo,       Role::ADMIN,   NO_CONDITION     },
    {   "get_counts",           &doGetCounts,           Role::ADMIN,   NO_CONDITION     },
    {   "get_signingaccount",   &doGetSigningAccount,   Role::USER,    NO_CONDITION     },
    {   "internal",             &doInternal,            Role::ADMIN,   NO_CONDITION     },
    {   "feature",              &doFeature,             Role::ADMIN,   NO_CONDITION     },
    {   "fetch_info",           &doFetchInfo,           Role::ADMIN,   NO_CONDITION     },
    {   "ledger",               &doLedger,              Role::USER,  NEEDS_NETWORK_CONNECTION  },
    {   "ledger_accept",        &doLedgerAccept,        Role::ADMIN,   NEEDS_CURRENT_LEDGER  },
    {   "ledger_cleaner",       &doLedgerCleaner,       Role::ADMIN,   NEEDS_NETWORK_CONNECTION  },
    {   "ledger_closed",        &doLedgerClosed,        Role::USER,  NEEDS_CLOSED_LEDGER   },
    {   "ledger_current",       &doLedgerCurrent,       Role::USER,  NEEDS_CURRENT_LEDGER  },
    {   "ledger_data",          &doLedgerData,          Role::USER,  NEEDS_CURRENT_LEDGER  },
    {   "ledger_entry",         &doLedgerEntry,         Role::USER,  NEEDS_CURRENT_LEDGER  },
    {   "ledger_header",        &doLedgerHeader,        Role::USER,  NEEDS_CURRENT_LEDGER  },
    {   "ledger_request",       &doLedgerRequest,       Role::ADMIN,   NO_CONDITION     },
    {   "log_level",            &doLogLevel,            Role::ADMIN,   NO_CONDITION     },
    {   "logrotate",            &doLogRotate,           Role::ADMIN,   NO_CONDITION     },
    {   "owner_info",           &doOwnerInfo,           Role::USER,  NEEDS_CURRENT_LEDGER  },
    {   "peers",                &doPeers,               Role::ADMIN,   NO_CONDITION     },
    {   "path_find",            &doPathFind,            Role::USER,  NEEDS_CURRENT_LEDGER  },
    {   "ping",                 &doPing,                Role::USER,  NO_CONDITION     },
    {   "print",                &doPrint,               Role::ADMIN,   NO_CONDITION     },
//      {   "profile",              &doProfile,             Role::USER,  NEEDS_CURRENT_LEDGER  },
    {   "proof_create",         &doProofCreate,         Role::ADMIN,   NO_CONDITION     },
    {   "proof_solve",          &doProofSolve,          Role::ADMIN,   NO_CONDITION     },
    {   "proof_verify",         &doProofVerify,         Role::ADMIN,   NO_CONDITION     },
    {   "random",               &doRandom,              Role::USER,  NO_CONDITION     },
    {   "ripple_path_find",     &doRipplePathFind,      Role::USER,  NEEDS_CURRENT_LEDGER  },
    {   "sign",                 &doSign,                Role::USER,  NO_CONDITION     },
    {   "submit",               &doSubmit,              Role::USER,  NEEDS_CURRENT_LEDGER  },
    {   "submit_multisigned",   &doSubmitMultiSigned,   Role::USER,    NEEDS_CURRENT_LEDGER  },
    {   "server_info",          &doServerInfo,          Role::USER,  NO_CONDITION     },
    {   "server_state",         &doServerState,         Role::USER,  NO_CONDITION     },
    {   "sms",                  &doSMS,                 Role::ADMIN,   NO_CONDITION     },
    {   "stop",                 &doStop,                Role::ADMIN,   NO_CONDITION     },
    {   "transaction_entry",    &doTransactionEntry,    Role::USER,  NEEDS_CURRENT_LEDGER  },
    {   "tx",                   &doTx,                  Role::USER,  NEEDS_NETWORK_CONNECTION  },
    {   "tx_history",           &doTxHistory,           Role::USER,  NO_CONDITION     },
    {   "unl_add",              &doUnlAdd,              Role::ADMIN,   NO_CONDITION     },
    {   "unl_delete",           &doUnlDelete,           Role::ADMIN,   NO_CONDITION     },
    {   "unl_list",             &doUnlList,             Role::ADMIN,   NO_CONDITION     },
    {   "unl_load",             &doUnlLoad,             Role::ADMIN,   NO_CONDITION     },
    {   "unl_network",          &doUnlNetwork,          Role::ADMIN,   NO_CONDITION     },
    {   "unl_reset",            &doUnlReset,            Role::ADMIN,   NO_CONDITION     },
    {   "unl_score",            &doUnlScore,            Role::ADMIN,   NO_CONDITION     },
    {   "validation_create",    &doValidationCreate,    Role::ADMIN,   NO_CONDITION     },
    {   "validation_seed",      &doValidationSeed,      Role::ADMIN,   NO_CONDITION     },
    {   "wallet_accounts",      &doWalletAccounts,      Role::USER,  NEEDS_CURRENT_LEDGER  },
    {   "wallet_propose",       &doWalletPropose,       Role::ADMIN,   NO_CONDITION     },
    {   "wallet_seed",          &doWalletSeed,          Role::ADMIN,   NO_CONDITION     },

    // Evented methods
    {   "subscribe",            &doSubscribe,           Role::USER,  NO_CONDITION     },
    {   "unsubscribe",          &doUnsubscribe,         Role::USER,  NO_CONDITION     },
});

} // namespace

const Handler* getHandler(std::string name) {
    return HANDLERS.getHandler(name);
}

} // RPC
} // ripple
