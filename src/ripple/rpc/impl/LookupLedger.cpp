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

#include <BeastConfig.h>
#include <ripple/rpc/impl/LookupLedger.h>

namespace ripple {
namespace RPC {

namespace {

Status ledgerFromRequest (
    Json::Value const& params,
    Ledger::pointer& ledger,
    NetworkOPs& netOps)
{
    ledger.reset();

    auto indexValue = params[jss::ledger_index];
    auto hashValue = params[jss::ledger_hash];

    // We need to support the legacy "ledger" field.
    auto& legacyLedger = params[jss::ledger];
    if (!legacyLedger.empty())
    {
        if (legacyLedger.asString().size () > 12)
            hashValue = legacyLedger;
        else
            indexValue = legacyLedger;
    }

    if (!hashValue.empty())
    {
        uint256 ledgerHash;
        if (hashValue.isString() && ledgerHash.SetHex (hashValue.asString ()))
            ledger = netOps.getLedgerByHash (ledgerHash);
        else
            return {rpcINVALID_PARAMS, "ledgerHashMalformed"};
    }
    else if (indexValue.isNumeric())
    {
        ledger = netOps.getLedgerBySeq (indexValue.asInt());
    }
    else
    {
        auto index = indexValue.asString();
        auto isCurrent = index.empty() || index == "current";
        if (isCurrent)
            ledger = netOps.getCurrentLedger ();
        else if (index == "closed")
            ledger = getApp().getLedgerMaster ().getClosedLedger ();
        else if (index == "validated")
            ledger = netOps.getValidatedLedger ();
        else
            return {rpcINVALID_PARAMS, "ledgerIndexMalformed"};

        assert (ledger->isImmutable());
        assert (ledger->isClosed() == !isCurrent);
    }

    if (!ledger)
        return {rpcLGR_NOT_FOUND, "ledgerNotFound"};

    return Status::OK;
}

bool isValidated (Ledger& ledger)
{
    if (ledger.isValidated ())
        return true;

    if (!ledger.isClosed ())
        return false;

    auto seq = ledger.getLedgerSeq();
    try
    {
        // Use the skip list in the last validated ledger to see if ledger
        // comes before the last validated ledger (and thus has been
        // validated).
        auto hash = getApp().getLedgerMaster ().walkHashBySeq (seq);
        if (ledger.getHash() != hash)
            return false;
    }
    catch (SHAMapMissingNode const&)
    {
        WriteLog (lsWARNING, RPCHandler)
                << "Missing SHANode " << std::to_string (seq);
        return false;
    }

    // Mark ledger as validated to save time if we see it again.
    ledger.setValidated();
    return true;
}

} // namespace

// The previous version of the lookupLedger command would accept the
// "ledger_index" argument as a string and silently treat it as a request to
// return the current ledger which, while not strictly wrong, could cause a lot
// of confusion.
//
// The code now robustly validates the input and ensures that the only possible
// values for the "ledger_index" parameter are the index of a ledger passed as
// an integer or one of the strings "current", "closed" or "validated".
// Additionally, the code ensures that the value passed in "ledger_hash" is a
// string and a valid hash. Invalid values will return an appropriate error
// code.
//
// In the absence of the "ledger_hash" or "ledger_index" parameters, the code
// assumes that "ledger_index" has the value "current".
//
// Returns a Json::objectValue.  If there was an error, it will be in that
// return value.  Otherwise, the object contains the field "validated" and
// optionally the fields "ledger_hash", "ledger_index" and
// "ledger_current_index", if they are defined.
Status lookupLedger (
    Json::Value const& params,
    Ledger::pointer& ledger,
    NetworkOPs& netOps,
    Json::Value& jsonResult)
{
    if (auto status = ledgerFromRequest (params, ledger, netOps))
        return status;

    if (ledger->isClosed ())
    {
        jsonResult[jss::ledger_hash] = to_string (ledger->getHash());
        jsonResult[jss::ledger_index] = ledger->getLedgerSeq();
    }
    else
    {
        jsonResult[jss::ledger_current_index] = ledger->getLedgerSeq();
    }
    jsonResult[jss::validated] = isValidated (*ledger);
    return Status::OK;
}

Json::Value lookupLedger (
    Json::Value const& params,
    Ledger::pointer& ledger,
    NetworkOPs& netOps)
{
    Json::Value value (Json::objectValue);
    if (auto status = lookupLedger (params, ledger, netOps, value))
        status.inject (value);

    return value;
}

} // RPC
} // ripple
