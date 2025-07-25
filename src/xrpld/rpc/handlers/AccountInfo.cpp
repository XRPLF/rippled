//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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

#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/ledger/ReadView.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/GRPCHandlers.h>
#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

// {
//   account: <ident>,
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
//   signer_lists : <bool> // optional (default false)
//                         //   if true return SignerList(s).
//   queue : <bool>        // optional (default false)
//                         //   if true return information about transactions
//                         //   in the current TxQ, only if the requested
//                         //   ledger is open. Otherwise if true, returns an
//                         //   error.
// }

// TODO(tom): what is that "default"?
Json::Value
doAccountInfo(RPC::JsonContext& context)
{
    auto& params = context.params;

    std::string strIdent;
    if (params.isMember(jss::account))
    {
        if (!params[jss::account].isString())
            return RPC::invalid_field_error(jss::account);
        strIdent = params[jss::account].asString();
    }
    else if (params.isMember(jss::ident))
    {
        if (!params[jss::ident].isString())
            return RPC::invalid_field_error(jss::ident);
        strIdent = params[jss::ident].asString();
    }
    else
        return RPC::missing_field_error(jss::account);

    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger(ledger, context);

    if (!ledger)
        return result;

    // Get info on account.
    auto id = parseBase58<AccountID>(strIdent);
    if (!id)
    {
        RPC::inject_error(rpcACT_MALFORMED, result);
        return result;
    }
    auto const accountID{std::move(id.value())};

    static constexpr std::
        array<std::pair<std::string_view, LedgerSpecificFlags>, 9>
            lsFlags{
                {{"defaultRipple", lsfDefaultRipple},
                 {"depositAuth", lsfDepositAuth},
                 {"disableMasterKey", lsfDisableMaster},
                 {"disallowIncomingXRP", lsfDisallowXRP},
                 {"globalFreeze", lsfGlobalFreeze},
                 {"noFreeze", lsfNoFreeze},
                 {"passwordSpent", lsfPasswordSpent},
                 {"requireAuthorization", lsfRequireAuth},
                 {"requireDestinationTag", lsfRequireDestTag}}};

    static constexpr std::
        array<std::pair<std::string_view, LedgerSpecificFlags>, 4>
            disallowIncomingFlags{
                {{"disallowIncomingNFTokenOffer",
                  lsfDisallowIncomingNFTokenOffer},
                 {"disallowIncomingCheck", lsfDisallowIncomingCheck},
                 {"disallowIncomingPayChan", lsfDisallowIncomingPayChan},
                 {"disallowIncomingTrustline", lsfDisallowIncomingTrustline}}};

    static constexpr std::pair<std::string_view, LedgerSpecificFlags>
        allowTrustLineClawbackFlag{
            "allowTrustLineClawback", lsfAllowTrustLineClawback};

    static constexpr std::pair<std::string_view, LedgerSpecificFlags>
        allowTrustLineLockingFlag{
            "allowTrustLineLocking", lsfAllowTrustLineLocking};

    auto const sleAccepted = ledger->read(keylet::account(accountID));
    if (sleAccepted)
    {
        auto const queue =
            params.isMember(jss::queue) && params[jss::queue].asBool();

        if (queue && !ledger->open())
        {
            // It doesn't make sense to request the queue
            // with any closed or validated ledger.
            RPC::inject_error(rpcINVALID_PARAMS, result);
            return result;
        }

        Json::Value jvAccepted(Json::objectValue);
        RPC::injectSLE(jvAccepted, *sleAccepted);
        result[jss::account_data] = jvAccepted;

        Json::Value acctFlags{Json::objectValue};
        for (auto const& lsf : lsFlags)
            acctFlags[lsf.first.data()] = sleAccepted->isFlag(lsf.second);

        if (ledger->rules().enabled(featureDisallowIncoming))
        {
            for (auto const& lsf : disallowIncomingFlags)
                acctFlags[lsf.first.data()] = sleAccepted->isFlag(lsf.second);
        }

        if (ledger->rules().enabled(featureClawback))
            acctFlags[allowTrustLineClawbackFlag.first.data()] =
                sleAccepted->isFlag(allowTrustLineClawbackFlag.second);

        if (ledger->rules().enabled(featureTokenEscrow))
            acctFlags[allowTrustLineLockingFlag.first.data()] =
                sleAccepted->isFlag(allowTrustLineLockingFlag.second);

        result[jss::account_flags] = std::move(acctFlags);

        // The document[https://xrpl.org/account_info.html#account_info] states
        // that signer_lists is a bool, however assigning any string value
        // works. Do not allow this. This check is for api Version 2 onwards
        // only
        if (context.apiVersion > 1u && params.isMember(jss::signer_lists) &&
            !params[jss::signer_lists].isBool())
        {
            RPC::inject_error(rpcINVALID_PARAMS, result);
            return result;
        }

        // Return SignerList(s) if that is requested.
        if (params.isMember(jss::signer_lists) &&
            params[jss::signer_lists].asBool())
        {
            // We put the SignerList in an array because of an anticipated
            // future when we support multiple signer lists on one account.
            Json::Value jvSignerList = Json::arrayValue;

            // This code will need to be revisited if in the future we support
            // multiple SignerLists on one account.
            auto const sleSigners = ledger->read(keylet::signers(accountID));
            if (sleSigners)
                jvSignerList.append(sleSigners->getJson(JsonOptions::none));

            // Documentation states this is returned as part of the account_info
            // response, but previously the code put it under account_data. We
            // can move this to the documentated location from apiVersion 2
            // onwards.
            if (context.apiVersion == 1)
            {
                result[jss::account_data][jss::signer_lists] =
                    std::move(jvSignerList);
            }
            else
            {
                result[jss::signer_lists] = std::move(jvSignerList);
            }
        }
        // Return queue info if that is requested
        if (queue)
        {
            Json::Value jvQueueData = Json::objectValue;

            auto const txs = context.app.getTxQ().getAccountTxs(accountID);
            if (!txs.empty())
            {
                jvQueueData[jss::txn_count] =
                    static_cast<Json::UInt>(txs.size());

                auto& jvQueueTx = jvQueueData[jss::transactions];
                jvQueueTx = Json::arrayValue;

                std::uint32_t seqCount = 0;
                std::uint32_t ticketCount = 0;
                std::optional<std::uint32_t> lowestSeq;
                std::optional<std::uint32_t> highestSeq;
                std::optional<std::uint32_t> lowestTicket;
                std::optional<std::uint32_t> highestTicket;
                bool anyAuthChanged = false;
                XRPAmount totalSpend(0);

                // We expect txs to be returned sorted by SeqProxy.  Verify
                // that with a couple of asserts.
                SeqProxy prevSeqProxy = SeqProxy::sequence(0);
                for (auto const& tx : txs)
                {
                    Json::Value jvTx = Json::objectValue;

                    if (tx.seqProxy.isSeq())
                    {
                        XRPL_ASSERT(
                            prevSeqProxy < tx.seqProxy,
                            "rpple::doAccountInfo : first sorted proxy");
                        prevSeqProxy = tx.seqProxy;
                        jvTx[jss::seq] = tx.seqProxy.value();
                        ++seqCount;
                        if (!lowestSeq)
                            lowestSeq = tx.seqProxy.value();
                        highestSeq = tx.seqProxy.value();
                    }
                    else
                    {
                        XRPL_ASSERT(
                            prevSeqProxy < tx.seqProxy,
                            "rpple::doAccountInfo : second sorted proxy");
                        prevSeqProxy = tx.seqProxy;
                        jvTx[jss::ticket] = tx.seqProxy.value();
                        ++ticketCount;
                        if (!lowestTicket)
                            lowestTicket = tx.seqProxy.value();
                        highestTicket = tx.seqProxy.value();
                    }

                    jvTx[jss::fee_level] = to_string(tx.feeLevel);
                    if (tx.lastValid)
                        jvTx[jss::LastLedgerSequence] = *tx.lastValid;

                    jvTx[jss::fee] = to_string(tx.consequences.fee());
                    auto const spend = tx.consequences.potentialSpend() +
                        tx.consequences.fee();
                    jvTx[jss::max_spend_drops] = to_string(spend);
                    totalSpend += spend;
                    bool const authChanged = tx.consequences.isBlocker();
                    if (authChanged)
                        anyAuthChanged = authChanged;
                    jvTx[jss::auth_change] = authChanged;

                    jvQueueTx.append(std::move(jvTx));
                }

                if (seqCount)
                    jvQueueData[jss::sequence_count] = seqCount;
                if (ticketCount)
                    jvQueueData[jss::ticket_count] = ticketCount;
                if (lowestSeq)
                    jvQueueData[jss::lowest_sequence] = *lowestSeq;
                if (highestSeq)
                    jvQueueData[jss::highest_sequence] = *highestSeq;
                if (lowestTicket)
                    jvQueueData[jss::lowest_ticket] = *lowestTicket;
                if (highestTicket)
                    jvQueueData[jss::highest_ticket] = *highestTicket;

                jvQueueData[jss::auth_change_queued] = anyAuthChanged;
                jvQueueData[jss::max_spend_drops_total] = to_string(totalSpend);
            }
            else
                jvQueueData[jss::txn_count] = 0u;

            result[jss::queue_data] = std::move(jvQueueData);
        }
    }
    else
    {
        result[jss::account] = toBase58(accountID);
        RPC::inject_error(rpcACT_NOT_FOUND, result);
    }

    return result;
}

}  // namespace ripple
