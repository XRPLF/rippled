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

#include <xrpld/app/hook/applyHook.h>
#include <xrpld/app/ledger/OpenLedger.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/app/tx/detail/Transactor.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/GRPCHandlers.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/FeeUnits.h>

namespace ripple {

inline std::optional<XRPAmount>
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

        return calculateBaseFee(
            *(context.app.openLedger().current()), *stpTrans);
    }

    return std::nullopt;
}

Json::Value
doFee(RPC::JsonContext& context)
{
    // get hook fees, if any
    std::optional<XRPAmount> hookFees;
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
            jvResult[jss::fee_hooks_drops] = to_string(*hookFees);
        return jvResult;
    }

    UNREACHABLE("ripple::doFee : invalid result type");
    RPC::inject_error(rpcINTERNAL, context.params);
    return context.params;
}

}  // namespace ripple
