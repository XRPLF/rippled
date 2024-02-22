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
#include <ripple/basics/mulDiv.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Feature.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/GRPCHandlers.h>
#include <ripple/app/hook/applyHook.h>
#include <ripple/app/tx/impl/Transactor.h>
#include <ripple/basics/FeeUnits.h>

namespace ripple {


inline
std::optional<FeeUnit64>
getHookFees(RPC::JsonContext const& context)
{
    auto const& params(context.params);
    if (params.isMember(jss::tx_blob))
    {
        auto ret = strUnHex(context.params[jss::tx_blob].asString());

        if (!ret || !ret->size())
            throw std::invalid_argument("Invalid tx_blob");

        SerialIter sitTrans(makeSlice(*ret));

        std::unique_ptr<STTx const> stpTrans;
        stpTrans = std::make_unique<STTx const>(std::ref(sitTrans));

        if (!stpTrans->isFieldPresent(sfAccount))
            throw std::invalid_argument("No sfAccount specified");

        return
            invoke_calculateBaseFee(
                *(context.app.openLedger().current()),
                *stpTrans);
    }

    return std::nullopt;
}

Json::Value
doFee(RPC::JsonContext& context)
{
    // get hook fees, if any
    std::optional<FeeUnit64> hookFees;
    try
    {
        hookFees = getHookFees(context);
    }
    catch (std::exception& e)
    {
        Json::Value jvResult;
        jvResult[jss::error] = "invalidTransaction";
        jvResult[jss::error_exception] = e.what();
        return jvResult;
    }

    Json::Value jvResult = context.app.getTxQ().doRPC(context.app, hookFees);
    if (jvResult.type() == Json::objectValue)
    {
        if (hookFees)
            jvResult[jss::fee_hooks_feeunits] = to_string(*hookFees);
        return jvResult;
    }

    assert(false);
    RPC::inject_error(rpcINTERNAL, context.params);
    return context.params;
}

std::pair<org::xrpl::rpc::v1::GetFeeResponse, grpc::Status>
doFeeGrpc(RPC::GRPCContext<org::xrpl::rpc::v1::GetFeeRequest>& context)
{
    org::xrpl::rpc::v1::GetFeeResponse reply;
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
    org::xrpl::rpc::v1::FeeLevels& levels = *reply.mutable_levels();
    levels.set_median_level(metrics.medFeeLevel.fee());
    levels.set_minimum_level(metrics.minProcessingFeeLevel.fee());
    levels.set_open_ledger_level(metrics.openLedgerFeeLevel.fee());
    levels.set_reference_level(metrics.referenceFeeLevel.fee());

    // fee data
    org::xrpl::rpc::v1::Fee& fee = *reply.mutable_fee();
    auto const baseFee = view->fees().base;
    fee.mutable_base_fee()->set_drops(
        toDrops(metrics.referenceFeeLevel, baseFee).drops());
    fee.mutable_minimum_fee()->set_drops(
        toDrops(metrics.minProcessingFeeLevel, baseFee).drops());
    fee.mutable_median_fee()->set_drops(
        toDrops(metrics.medFeeLevel, baseFee).drops());

    fee.mutable_open_ledger_fee()->set_drops(
        (toDrops(metrics.openLedgerFeeLevel - FeeLevel64{1}, baseFee) + 1)
            .drops());
    return {reply, status};
}
}  // namespace ripple
