//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_REPORTING_P2PPROXY_H_INCLUDED
#define RIPPLE_APP_REPORTING_P2PPROXY_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/Handler.h>

#include <boost/beast/websocket.hpp>

#include "org/xrpl/rpc/v1/xrp_ledger.grpc.pb.h"
#include <grpcpp/grpcpp.h>

namespace ripple {
/// Forward a JSON request to a p2p node and return the response
/// @param context context of the request
/// @return response from p2p node
Json::Value
forwardToP2p(RPC::JsonContext& context);

/// Whether a request should be forwarded, based on request parameters
/// @param context context of the request
/// @return true if should be forwarded
bool
shouldForwardToP2p(RPC::JsonContext& context);

template <class Request>
bool
needCurrentOrClosed(Request& request)
{
    // These are the only gRPC requests that specify a ledger
    if constexpr (
        std::is_same<Request, org::xrpl::rpc::v1::GetAccountInfoRequest>::
            value ||
        std::is_same<Request, org::xrpl::rpc::v1::GetLedgerRequest>::value ||
        std::is_same<Request, org::xrpl::rpc::v1::GetLedgerDataRequest>::
            value ||
        std::is_same<Request, org::xrpl::rpc::v1::GetLedgerEntryRequest>::value)
    {
        if (request.ledger().ledger_case() ==
            org::xrpl::rpc::v1::LedgerSpecifier::LedgerCase::kShortcut)
        {
            if (request.ledger().shortcut() !=
                    org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_VALIDATED &&
                request.ledger().shortcut() !=
                    org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_UNSPECIFIED)
                return true;
        }
    }
    // GetLedgerDiff specifies two ledgers
    else if constexpr (std::is_same<
                           Request,
                           org::xrpl::rpc::v1::GetLedgerDiffRequest>::value)
    {
        auto help = [](auto specifier) {
            if (specifier.ledger_case() ==
                org::xrpl::rpc::v1::LedgerSpecifier::LedgerCase::kShortcut)
            {
                if (specifier.shortcut() !=
                        org::xrpl::rpc::v1::LedgerSpecifier::
                            SHORTCUT_VALIDATED &&
                    specifier.shortcut() !=
                        org::xrpl::rpc::v1::LedgerSpecifier::
                            SHORTCUT_UNSPECIFIED)
                    return true;
            }
            return false;
        };
        return help(request.base_ledger()) || help(request.desired_ledger());
    }
    return false;
}

/// Whether a request should be forwarded, based on request parameters
/// @param context context of the request
/// @condition required condition for the request
/// @return true if should be forwarded
template <class Request>
bool
shouldForwardToP2p(RPC::GRPCContext<Request>& context, RPC::Condition condition)
{
    if (!context.app.config().reporting())
        return false;
    if (condition == RPC::NEEDS_CURRENT_LEDGER ||
        condition == RPC::NEEDS_CLOSED_LEDGER)
        return true;

    return needCurrentOrClosed(context.params);
}

/// Get stub used to forward gRPC requests to a p2p node
/// @param context context of the request
/// @return stub to forward requests
std::unique_ptr<org::xrpl::rpc::v1::XRPLedgerAPIService::Stub>
getP2pForwardingStub(RPC::Context& context);

}  // namespace ripple
#endif
