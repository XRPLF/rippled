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
#include <ripple/ledger/ReadView.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/LedgerFormats.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/GRPCHandlers.h>
#include <ripple/rpc/Role.h>
#include <ripple/rpc/impl/GRPCHelpers.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <ripple/rpc/impl/Tuning.h>

namespace ripple {

// Get state nodes from a ledger
//   Inputs:
//     limit:        integer, maximum number of entries
//     marker:       opaque, resume point
//     binary:       boolean, format
//     type:         string // optional, defaults to all ledger node types
//   Outputs:
//     ledger_hash:  chosen ledger's hash
//     ledger_index: chosen ledger's index
//     state:        array of state nodes
//     marker:       resume point, if any
Json::Value
doLedgerData(RPC::JsonContext& context)
{
    std::shared_ptr<ReadView const> lpLedger;
    auto const& params = context.params;

    auto jvResult = RPC::lookupLedger(lpLedger, context);
    if (!lpLedger)
        return jvResult;

    bool const isMarker = params.isMember(jss::marker);
    ReadView::key_type key = ReadView::key_type();
    if (isMarker)
    {
        Json::Value const& jMarker = params[jss::marker];
        if (!(jMarker.isString() && key.parseHex(jMarker.asString())))
            return RPC::expected_field_error(jss::marker, "valid");
    }

    bool const isBinary = params[jss::binary].asBool();

    int limit = -1;
    if (params.isMember(jss::limit))
    {
        Json::Value const& jLimit = params[jss::limit];
        if (!jLimit.isIntegral())
            return RPC::expected_field_error(jss::limit, "integer");

        limit = jLimit.asInt();
    }

    auto maxLimit = RPC::Tuning::pageLength(isBinary);
    if ((limit < 0) || ((limit > maxLimit) && (!isUnlimited(context.role))))
        limit = maxLimit;

    jvResult[jss::ledger_hash] = to_string(lpLedger->info().hash);
    jvResult[jss::ledger_index] = lpLedger->info().seq;

    if (!isMarker)
    {
        // Return base ledger data on first query
        jvResult[jss::ledger] = getJson(LedgerFill(
            *lpLedger, &context, isBinary ? LedgerFill::Options::binary : 0));
    }

    auto [rpcStatus, type] = RPC::chooseLedgerEntryType(params);
    if (rpcStatus)
    {
        jvResult.clear();
        rpcStatus.inject(jvResult);
        return jvResult;
    }
    Json::Value& nodes = jvResult[jss::state];

    auto e = lpLedger->sles.end();
    for (auto i = lpLedger->sles.upper_bound(key); i != e; ++i)
    {
        auto sle = lpLedger->read(keylet::unchecked((*i)->key()));
        if (limit-- <= 0)
        {
            // Stop processing before the current key.
            auto k = sle->key();
            jvResult[jss::marker] = to_string(--k);
            break;
        }

        if (type == ltINVALID || sle->getType() == type)
        {
            if (isBinary)
            {
                Json::Value& entry = nodes.append(Json::objectValue);
                entry[jss::data] = serializeHex(*sle);
                entry[jss::index] = to_string(sle->key());
            }
            else
            {
                Json::Value& entry =
                    nodes.append(sle->getJson(JsonOptions::none));
                entry[jss::index] = to_string(sle->key());
            }
        }
    }

    return jvResult;
}

std::pair<org::xrpl::rpc::v1::GetLedgerDataResponse, grpc::Status>
doLedgerDataGrpc(
    RPC::GRPCContext<org::xrpl::rpc::v1::GetLedgerDataRequest>& context)
{
    org::xrpl::rpc::v1::GetLedgerDataRequest& request = context.params;
    org::xrpl::rpc::v1::GetLedgerDataResponse response;
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

    uint256 startKey;
    if (auto key = uint256::fromVoidChecked(request.marker()))
    {
        startKey = *key;
    }
    else if (request.marker().size() != 0)
    {
        grpc::Status errorStatus{
            grpc::StatusCode::INVALID_ARGUMENT, "marker malformed"};
        return {response, errorStatus};
    }

    auto e = ledger->sles.end();
    if (auto key = uint256::fromVoidChecked(request.end_marker()))
    {
        e = ledger->sles.upper_bound(*key);
    }
    else if (request.end_marker().size() != 0)
    {
        grpc::Status errorStatus{
            grpc::StatusCode::INVALID_ARGUMENT, "end marker malformed"};
        return {response, errorStatus};
    }

    int maxLimit = RPC::Tuning::pageLength(true);

    for (auto i = ledger->sles.upper_bound(startKey); i != e; ++i)
    {
        auto sle = ledger->read(keylet::unchecked((*i)->key()));
        if (maxLimit-- <= 0)
        {
            // Stop processing before the current key.
            auto k = sle->key();
            --k;
            response.set_marker(k.data(), k.size());
            break;
        }
        auto stateObject = response.mutable_ledger_objects()->add_objects();
        Serializer s;
        sle->add(s);
        stateObject->set_data(s.peekData().data(), s.getLength());
        stateObject->set_key(sle->key().data(), sle->key().size());
    }
    return {response, status};
}

}  // namespace ripple
