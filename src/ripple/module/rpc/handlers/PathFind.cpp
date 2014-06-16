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


namespace ripple {

Json::Value doPathFind (RPC::Context& context)
{
    Ledger::pointer lpLedger = context.netOps_.getClosedLedger();
    context.lock_.unlock();

    if (!context.params_.isMember ("subcommand") || !context.params_["subcommand"].isString ())
        return rpcError (rpcINVALID_PARAMS);

    if (!context.infoSub_)
        return rpcError (rpcNO_EVENTS);

    std::string sSubCommand = context.params_["subcommand"].asString ();

    if (sSubCommand == "create")
    {
        context.loadType_ = Resource::feeHighBurdenRPC;
        context.infoSub_->clearPathRequest ();
        return getApp().getPathRequests().makePathRequest (context.infoSub_, lpLedger, context.params_);
    }

    if (sSubCommand == "close")
    {
        PathRequest::pointer request = context.infoSub_->getPathRequest ();

        if (!request)
            return rpcError (rpcNO_PF_REQUEST);

        context.infoSub_->clearPathRequest ();
        return request->doClose (context.params_);
    }

    if (sSubCommand == "status")
    {
        PathRequest::pointer request = context.infoSub_->getPathRequest ();

        if (!request)
            return rpcError (rpcNO_PF_REQUEST);

        return request->doStatus (context.params_);
    }

    return rpcError (rpcINVALID_PARAMS);
}

} // ripple
