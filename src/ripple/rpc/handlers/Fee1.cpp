//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2015 Ripple Labs Inc.

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

#include <ripple/app/ledger/OpenLedger.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/TxQ.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/GRPCHandlers.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Feature.h>
#include <ripple/basics/mulDiv.h>

namespace ripple
{
    Json::Value doFee(RPC::JsonContext& context)
    {
        auto result = context.app.getTxQ().doRPC(context.app);
        if (result.type() == Json::objectValue)
            return result;
        assert(false);
        RPC::inject_error(rpcINTERNAL, context.params);
        return context.params;
    }

std::pair<rpc::v1::GetFeeResponse, grpc::Status>
doFeeGrpc(RPC::GRPCContext<rpc::v1::GetFeeRequest>& context)
{
    rpc::v1::GetFeeResponse reply;
    grpc::Status status = grpc::Status::OK;

    Application& app = context.app;
    auto const view = app.openLedger().current();
    if (!view)
    {
        BOOST_ASSERT(false);
        return {reply, status};
    }

    auto const metrics = app.getTxQ().getMetrics(*view);

    // current ledger data
    reply.set_current_ledger_size(metrics.txInLedger);
    reply.set_current_queue_size(metrics.txCount);
    reply.set_expected_ledger_size(metrics.txPerLedger);
    reply.set_ledger_current_index(view->info().seq);
    reply.set_max_queue_size(*metrics.txQMaxSize);

    // fee levels data
    rpc::v1::FeeLevels& levels = *reply.mutable_levels();
    levels.set_median_level(metrics.medFeeLevel.fee());
    levels.set_minimum_level(metrics.minProcessingFeeLevel.fee());
    levels.set_open_ledger_level(metrics.openLedgerFeeLevel.fee());
    levels.set_reference_level(metrics.referenceFeeLevel.fee());

    // fee data
    rpc::v1::Fee& drops = *reply.mutable_drops();
    auto const baseFee = view->fees().base;
    drops.mutable_base_fee()->set_drops(
        toDrops(metrics.referenceFeeLevel, baseFee).second.drops());
    drops.mutable_minimum_fee()->set_drops(
        toDrops(metrics.minProcessingFeeLevel, baseFee).second.drops());
    drops.mutable_median_fee()->set_drops(
        toDrops(metrics.medFeeLevel, baseFee).second.drops());

    drops.mutable_open_ledger_fee()->set_drops(
        (toDrops(metrics.openLedgerFeeLevel - FeeLevel64{1}, baseFee).second +
         1)
            .drops());
    return {reply, status};
}
}  // namespace ripple
