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

#include <ripple/app/main/Application.h>
#include <ripple/app/misc/TxQ.h>
#include <ripple/json/json_value.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/UintTypes.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/GRPCHandlers.h>
#include <ripple/rpc/impl/GRPCHelpers.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <grpc/status.h>

namespace ripple {

// {
//   account: <ident>,
//   strict: <bool>        // optional (default false)
//                         //   if true only allow public keys and addresses.
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
        strIdent = params[jss::account].asString();
    else if (params.isMember(jss::ident))
        strIdent = params[jss::ident].asString();
    else
        return RPC::missing_field_error(jss::account);

    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger(ledger, context);

    if (!ledger)
        return result;

    bool bStrict = params.isMember(jss::strict) && params[jss::strict].asBool();
    AccountID accountID;

    // Get info on account.

    auto jvAccepted = RPC::accountFromString(accountID, strIdent, bStrict);

    if (jvAccepted)
        return jvAccepted;

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

        RPC::injectSLE(jvAccepted, *sleAccepted);
        result[jss::account_data] = jvAccepted;

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

            result[jss::account_data][jss::signer_lists] =
                std::move(jvSignerList);
        }
        // Return queue info if that is requested
        if (queue)
        {
            Json::Value jvQueueData = Json::objectValue;

            auto const txs =
                context.app.getTxQ().getAccountTxs(accountID, *ledger);
            if (!txs.empty())
            {
                jvQueueData[jss::txn_count] =
                    static_cast<Json::UInt>(txs.size());
                jvQueueData[jss::lowest_sequence] = txs.begin()->first;
                jvQueueData[jss::highest_sequence] = txs.rbegin()->first;

                auto& jvQueueTx = jvQueueData[jss::transactions];
                jvQueueTx = Json::arrayValue;

                boost::optional<bool> anyAuthChanged(false);
                boost::optional<XRPAmount> totalSpend(0);

                for (auto const& [txSeq, txDetails] : txs)
                {
                    Json::Value jvTx = Json::objectValue;

                    jvTx[jss::seq] = txSeq;
                    jvTx[jss::fee_level] = to_string(txDetails.feeLevel);
                    if (txDetails.lastValid)
                        jvTx[jss::LastLedgerSequence] = *txDetails.lastValid;
                    if (txDetails.consequences)
                    {
                        jvTx[jss::fee] = to_string(txDetails.consequences->fee);
                        auto spend = txDetails.consequences->potentialSpend +
                            txDetails.consequences->fee;
                        jvTx[jss::max_spend_drops] = to_string(spend);
                        if (totalSpend)
                            *totalSpend += spend;
                        auto authChanged = txDetails.consequences->category ==
                            TxConsequences::blocker;
                        if (authChanged)
                            anyAuthChanged.emplace(authChanged);
                        jvTx[jss::auth_change] = authChanged;
                    }
                    else
                    {
                        if (anyAuthChanged && !*anyAuthChanged)
                            anyAuthChanged.reset();
                        totalSpend.reset();
                    }

                    jvQueueTx.append(std::move(jvTx));
                }

                if (anyAuthChanged)
                    jvQueueData[jss::auth_change_queued] = *anyAuthChanged;
                if (totalSpend)
                    jvQueueData[jss::max_spend_drops_total] =
                        to_string(*totalSpend);
            }
            else
                jvQueueData[jss::txn_count] = 0u;

            result[jss::queue_data] = std::move(jvQueueData);
        }
    }
    else
    {
        result[jss::account] = context.app.accountIDCache().toBase58(accountID);
        RPC::inject_error(rpcACT_NOT_FOUND, result);
    }

    return result;
}

std::pair<org::xrpl::rpc::v1::GetAccountInfoResponse, grpc::Status>
doAccountInfoGrpc(
    RPC::GRPCContext<org::xrpl::rpc::v1::GetAccountInfoRequest>& context)
{
    // Return values
    org::xrpl::rpc::v1::GetAccountInfoResponse result;
    grpc::Status status = grpc::Status::OK;

    // input
    org::xrpl::rpc::v1::GetAccountInfoRequest& params = context.params;

    // get ledger
    std::shared_ptr<ReadView const> ledger;
    auto lgrStatus = RPC::ledgerFromRequest(ledger, context);
    if (lgrStatus || !ledger)
    {
        grpc::Status errorStatus;
        if (lgrStatus.toErrorCode() == rpcINVALID_PARAMS)
        {
            errorStatus = grpc::Status(
                grpc::StatusCode::INVALID_ARGUMENT, lgrStatus.message());
        }
        else
        {
            errorStatus =
                grpc::Status(grpc::StatusCode::NOT_FOUND, lgrStatus.message());
        }
        return {result, errorStatus};
    }

    result.set_ledger_index(ledger->info().seq);
    result.set_validated(
        RPC::isValidated(context.ledgerMaster, *ledger, context.app));

    // decode account
    AccountID accountID;
    std::string strIdent = params.account().address();
    error_code_i code =
        RPC::accountFromStringWithCode(accountID, strIdent, params.strict());
    if (code != rpcSUCCESS)
    {
        grpc::Status errorStatus{
            grpc::StatusCode::INVALID_ARGUMENT, "invalid account"};
        return {result, errorStatus};
    }

    // get account data
    auto const sleAccepted = ledger->read(keylet::account(accountID));
    if (sleAccepted)
    {
        RPC::convert(*result.mutable_account_data(), *sleAccepted);

        // signer lists
        if (params.signer_lists())
        {
            auto const sleSigners = ledger->read(keylet::signers(accountID));
            if (sleSigners)
            {
                org::xrpl::rpc::v1::SignerList& signerListProto =
                    *result.mutable_signer_list();
                RPC::convert(signerListProto, *sleSigners);
            }
        }

        // queued transactions
        if (params.queue())
        {
            if (!ledger->open())
            {
                grpc::Status errorStatus{
                    grpc::StatusCode::INVALID_ARGUMENT,
                    "requested queue but ledger is not open"};
                return {result, errorStatus};
            }
            auto const txs =
                context.app.getTxQ().getAccountTxs(accountID, *ledger);
            org::xrpl::rpc::v1::QueueData& queueData =
                *result.mutable_queue_data();
            RPC::convert(queueData, txs);
        }
    }
    else
    {
        grpc::Status errorStatus{
            grpc::StatusCode::NOT_FOUND, "account not found"};
        return {result, errorStatus};
    }

    return {result, status};
}

}  // namespace ripple
