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

#include <ripple/module/rpc/impl/LookupLedger.h>

namespace ripple {
namespace RPC {

static const int LEDGER_CURRENT = -1;
static const int LEDGER_CLOSED = -2;
static const int LEDGER_VALIDATED = -3;

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
Json::Value lookupLedger (
    Json::Value const& params,
    Ledger::pointer& ledger,
    NetworkOPs& netOps)
{
    using RPC::make_error;
    ledger.reset();

    auto jsonHash = params.get (jss::ledger_hash, Json::Value ("0"));
    auto jsonIndex = params.get (jss::ledger_index, Json::Value ("current"));

    // Support for DEPRECATED "ledger" - attempt to deduce our input
    if (params.isMember (jss::ledger))
    {
        if (params[jss::ledger].asString ().size () > 12)
        {
            jsonHash = params[jss::ledger];
            jsonIndex = Json::Value ("");
        }
        else if (params[jss::ledger].isNumeric ())
        {
            jsonIndex = params[jss::ledger];
            jsonHash = Json::Value ("0");
        }
        else
        {
            jsonIndex = params[jss::ledger];
            jsonHash = Json::Value ("0");
        }
    }

    uint256 ledgerHash (0);

    if (!jsonHash.isString() || !ledgerHash.SetHex (jsonHash.asString ()))
        return make_error(rpcINVALID_PARAMS, "ledgerHashMalformed");

    std::int32_t ledgerIndex = LEDGER_CURRENT;

    // We only try to parse a ledger index if we have not already
    // determined that we have a ledger hash.
    if (ledgerHash == zero)
    {
        if (jsonIndex.isNumeric ())
        {
            ledgerIndex = jsonIndex.asInt ();
        }
        else
        {
            std::string index = jsonIndex.asString ();

            if (index == "current")
                ledgerIndex = LEDGER_CURRENT;
            else if (index == "closed")
                ledgerIndex = LEDGER_CLOSED;
            else if (index == "validated")
                ledgerIndex = LEDGER_VALIDATED;
            else
                return make_error(rpcINVALID_PARAMS, "ledgerIndexMalformed");
        }
    }
    else
    {
        ledger = netOps.getLedgerByHash (ledgerHash);

        if (!ledger)
            return make_error(rpcLGR_NOT_FOUND, "ledgerNotFound");

        ledgerIndex = ledger->getLedgerSeq ();
    }

    int ledgerRequest = 0;

    if (ledgerIndex <= 0) {
        switch (ledgerIndex)
        {
        case LEDGER_CURRENT:
            ledger = netOps.getCurrentLedger ();
            break;

        case LEDGER_CLOSED:
            ledger = getApp().getLedgerMaster ().getClosedLedger ();
            break;

        case LEDGER_VALIDATED:
            ledger = netOps.getValidatedLedger ();
            break;

        default:
            return make_error(rpcINVALID_PARAMS, "ledgerIndexMalformed");
        }

        assert (ledger->isImmutable());
        assert (ledger->isClosed() == (ledgerIndex != LEDGER_CURRENT));
        ledgerRequest = ledgerIndex;
        ledgerIndex = ledger->getLedgerSeq ();
    }

    if (!ledger)
    {
        ledger = netOps.getLedgerBySeq (ledgerIndex);

        if (!ledger)
            return make_error(rpcLGR_NOT_FOUND, "ledgerNotFound");
    }

    Json::Value jsonResult;
    if (ledger->isClosed ())
    {
        if (ledgerHash != zero)
            jsonResult[jss::ledger_hash] = to_string (ledgerHash);

        jsonResult[jss::ledger_index] = ledgerIndex;
    }
    else
    {
        jsonResult[jss::ledger_current_index] = ledgerIndex;
    }

    if (ledger->isValidated ())
    {
        jsonResult[jss::validated] = true;
    }
    else if (!ledger->isClosed ())
    {
        jsonResult[jss::validated] = false;
    }
    else
    {
        try
        {
            // Use the skip list in the last validated ledger to see if ledger
            // comes after the last validated ledger (and thus has been
            // validated).
            auto next = getApp().getLedgerMaster ().walkHashBySeq (ledgerIndex);
            if (ledgerHash == next)
            {
                ledger->setValidated();
                jsonResult[jss::validated] = true;
            }
            else
            {
                jsonResult[jss::validated] = false;
            }
        }
        catch (SHAMapMissingNode const&)
        {
            jsonResult[jss::validated] = false;
        }
    }

    return jsonResult;
}

} // RPC
} // ripple
