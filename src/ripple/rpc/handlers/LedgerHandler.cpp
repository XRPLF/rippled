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

#include <ripple/app/ledger/LedgerToJson.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/LoadFeeTrack.h>
#include <ripple/json/Object.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/GRPCHandlers.h>
#include <ripple/rpc/Role.h>
#include <ripple/rpc/handlers/LedgerHandler.h>
#include <ripple/rpc/impl/GRPCHelpers.h>
#include <ripple/rpc/impl/RPCHelpers.h>

namespace ripple {
namespace RPC {

LedgerHandler::LedgerHandler(JsonContext& context) : context_(context)
{
}

Status
LedgerHandler::check()
{
    auto const& params = context_.params;
    bool needsLedger = params.isMember(jss::ledger) ||
        params.isMember(jss::ledger_hash) ||
        params.isMember(jss::ledger_index) || context_.app.config().reporting();
    if (!needsLedger)
        return Status::OK;

    if (auto s = lookupLedger(ledger_, context_, result_))
        return s;

    bool const full = params[jss::full].asBool();
    bool const transactions = params[jss::transactions].asBool();
    bool const accounts = params[jss::accounts].asBool();
    bool const expand = params[jss::expand].asBool();
    bool const binary = params[jss::binary].asBool();
    bool const owner_funds = params[jss::owner_funds].asBool();
    bool const queue = params[jss::queue].asBool();
    auto type = chooseLedgerEntryType(params);
    if (type.first)
        return type.first;
    type_ = type.second;

    options_ = (full ? LedgerFill::full : 0) |
        (expand ? LedgerFill::expand : 0) |
        (transactions ? LedgerFill::dumpTxrp : 0) |
        (accounts ? LedgerFill::dumpState : 0) |
        (binary ? LedgerFill::binary : 0) |
        (owner_funds ? LedgerFill::ownerFunds : 0) |
        (queue ? LedgerFill::dumpQueue : 0);

    if (full || accounts)
    {
        // Until some sane way to get full ledgers has been implemented,
        // disallow retrieving all state nodes.
        if (!isUnlimited(context_.role))
            return rpcNO_PERMISSION;

        if (context_.app.getFeeTrack().isLoadedLocal() &&
            !isUnlimited(context_.role))
        {
            return rpcTOO_BUSY;
        }
        context_.loadType =
            binary ? Resource::feeMediumBurdenRPC : Resource::feeHighBurdenRPC;
    }
    if (queue)
    {
        if (!ledger_ || !ledger_->open())
        {
            // It doesn't make sense to request the queue
            // with a non-existant or closed/validated ledger.
            return rpcINVALID_PARAMS;
        }

        queueTxs_ = context_.app.getTxQ().getTxs(*ledger_);
    }

    return Status::OK;
}

}  // namespace RPC

std::pair<org::xrpl::rpc::v1::GetLedgerResponse, grpc::Status>
doLedgerGrpc(RPC::GRPCContext<org::xrpl::rpc::v1::GetLedgerRequest>& context)
{
    org::xrpl::rpc::v1::GetLedgerRequest& request = context.params;
    org::xrpl::rpc::v1::GetLedgerResponse response;
    grpc::Status status = grpc::Status::OK;

    std::shared_ptr<ReadView const> ledger;
    if (auto status = RPC::ledgerFromRequest(ledger, context))
    {
        grpc::Status errorStatus;
        if (status.toErrorCode() == rpcINVALID_PARAMS)
        {
            errorStatus = grpc::Status(
                grpc::StatusCode::INVALID_ARGUMENT, status.message());
        }
        else
        {
            errorStatus =
                grpc::Status(grpc::StatusCode::NOT_FOUND, status.message());
        }
        return {response, errorStatus};
    }

    Serializer s;
    addRaw(ledger->info(), s, true);

    response.set_ledger_header(s.peekData().data(), s.getLength());

    if (request.transactions())
    {
        for (auto& i : ledger->txs)
        {
            assert(i.first);
            if (request.expand())
            {
                auto txn =
                    response.mutable_transactions_list()->add_transactions();
                Serializer sTxn = i.first->getSerializer();
                txn->set_transaction_blob(sTxn.data(), sTxn.getLength());
                if (i.second)
                {
                    Serializer sMeta = i.second->getSerializer();
                    txn->set_metadata_blob(sMeta.data(), sMeta.getLength());
                }
            }
            else
            {
                auto const& hash = i.first->getTransactionID();
                response.mutable_hashes_list()->add_hashes(
                    hash.data(), hash.size());
            }
        }
    }

    if (request.get_objects())
    {
        std::shared_ptr<ReadView const> parent =
            context.app.getLedgerMaster().getLedgerBySeq(ledger->seq() - 1);

        std::shared_ptr<Ledger const> base =
            std::dynamic_pointer_cast<Ledger const>(parent);
        if (!base)
        {
            grpc::Status errorStatus{
                grpc::StatusCode::NOT_FOUND, "parent ledger not validated"};
            return {response, errorStatus};
        }

        std::shared_ptr<Ledger const> desired =
            std::dynamic_pointer_cast<Ledger const>(ledger);
        if (!desired)
        {
            grpc::Status errorStatus{
                grpc::StatusCode::NOT_FOUND, "ledger not validated"};
            return {response, errorStatus};
        }
        SHAMap::Delta differences;

        int maxDifferences = std::numeric_limits<int>::max();

        bool res = base->stateMap().compare(
            desired->stateMap(), differences, maxDifferences);
        if (!res)
        {
            grpc::Status errorStatus{
                grpc::StatusCode::RESOURCE_EXHAUSTED,
                "too many differences between specified ledgers"};
            return {response, errorStatus};
        }

        for (auto& [k, v] : differences)
        {
            auto obj = response.mutable_ledger_objects()->add_objects();
            auto inBase = v.first;
            auto inDesired = v.second;

            obj->set_key(k.data(), k.size());
            if (inDesired)
            {
                assert(inDesired->size() > 0);
                obj->set_data(inDesired->data(), inDesired->size());
            }
            if (inBase && inDesired)
                obj->set_mod_type(
                    org::xrpl::rpc::v1::RawLedgerObject::MODIFIED);
            else if (inBase && !inDesired)
                obj->set_mod_type(org::xrpl::rpc::v1::RawLedgerObject::DELETED);
            else
                obj->set_mod_type(org::xrpl::rpc::v1::RawLedgerObject::CREATED);
        }
        response.set_skiplist_included(true);
    }

    response.set_validated(
        RPC::isValidated(context.ledgerMaster, *ledger, context.app));

    return {response, status};
}
}  // namespace ripple
