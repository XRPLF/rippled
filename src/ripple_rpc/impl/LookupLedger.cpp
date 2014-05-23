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

#include "LookupLedger.h"

namespace ripple {
namespace RPC {

static const int LEDGER_CURRENT = -1;
static const int LEDGER_CLOSED = -2;
static const int LEDGER_VALIDATED = -3;

// The previous version of the lookupLedger command would accept the
// "ledger_index" argument as a string and silently treat it as a request to
// return the current ledger which, while not strictly wrong, could cause a
// lot of confusion.
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
    Ledger::pointer& lpLedger,
    NetworkOPs& netOps)
{
    Json::Value jvResult;

    Json::Value ledger_hash = params.get (jss::ledger_hash, Json::Value ("0"));
    Json::Value ledger_index = params.get (jss::ledger_index, Json::Value ("current"));

    // Support for DEPRECATED "ledger" - attempt to deduce our input
    if (params.isMember (jss::ledger))
    {
        if (params[jss::ledger].asString ().size () > 12)
        {
            ledger_hash = params[jss::ledger];
            ledger_index = Json::Value ("");
        }
        else if (params[jss::ledger].isNumeric ())
        {
            ledger_index = params[jss::ledger];
            ledger_hash = Json::Value ("0");
        }
        else
        {
            ledger_index = params[jss::ledger];
            ledger_hash = Json::Value ("0");
        }
    }

    uint256 uLedger (0);

    if (!ledger_hash.isString() || !uLedger.SetHex (ledger_hash.asString ()))
    {
        RPC::inject_error(rpcINVALID_PARAMS, "ledgerHashMalformed", jvResult);
        return jvResult;
    }

    std::int32_t iLedgerIndex = LEDGER_CURRENT;

    // We only try to parse a ledger index if we have not already
    // determined that we have a ledger hash.
    if (!uLedger)
    {
        if (ledger_index.isNumeric ())
            iLedgerIndex = ledger_index.asInt ();
        else
        {
            std::string strLedger = ledger_index.asString ();

            if (strLedger == "current")
            {
                iLedgerIndex = LEDGER_CURRENT;
            }
            else if (strLedger == "closed")
            {
                iLedgerIndex = LEDGER_CLOSED;
            }
            else if (strLedger == "validated")
            {
                iLedgerIndex = LEDGER_VALIDATED;
            }
            else
            {
                RPC::inject_error(rpcINVALID_PARAMS, "ledgerIndexMalformed", jvResult);
                return jvResult;
            }
        }
    }

    // The ledger was directly specified by hash.
    if (uLedger.isNonZero ())
    {
        lpLedger = netOps.getLedgerByHash (uLedger);

        if (!lpLedger)
        {
            RPC::inject_error(rpcLGR_NOT_FOUND, "ledgerNotFound", jvResult);
            return jvResult;
        }

        iLedgerIndex = lpLedger->getLedgerSeq ();
    }

    int ledger_request = 0;
    switch (iLedgerIndex)
    {
    case LEDGER_CURRENT:
        lpLedger = netOps.getCurrentLedger ();
        iLedgerIndex = lpLedger->getLedgerSeq ();
        assert (lpLedger->isImmutable () && !lpLedger->isClosed ());
        ledger_request = LEDGER_CURRENT;
        break;

    case LEDGER_CLOSED:
        lpLedger = getApp().getLedgerMaster ().getClosedLedger ();
        iLedgerIndex = lpLedger->getLedgerSeq ();
        assert (lpLedger->isImmutable () && lpLedger->isClosed ());
        ledger_request = LEDGER_CLOSED;
        break;

    case LEDGER_VALIDATED:
        lpLedger = netOps.getValidatedLedger ();
        iLedgerIndex = lpLedger->getLedgerSeq ();
        assert (lpLedger->isImmutable () && lpLedger->isClosed ());
        ledger_request = LEDGER_VALIDATED;
        break;
    }

    if (iLedgerIndex <= 0)
    {
        RPC::inject_error(rpcINVALID_PARAMS, "ledgerIndexMalformed", jvResult);
        return jvResult;
    }

    if (!lpLedger)
    {
        lpLedger = netOps.getLedgerBySeq (iLedgerIndex);

        if (!lpLedger)
        {
            RPC::inject_error(rpcLGR_NOT_FOUND, "ledgerNotFound", jvResult);
            return jvResult;
        }
    }

    if (lpLedger->isClosed ())
    {
        if (uLedger.isNonZero ())
            jvResult[jss::ledger_hash] = to_string (uLedger);

        jvResult[jss::ledger_index] = iLedgerIndex;
    }
    else
    {
        jvResult[jss::ledger_current_index] = iLedgerIndex;
    }

    if (lpLedger->isValidated ())
        jvResult[jss::validated] = true;
    else
    {
        if (!lpLedger->isClosed ())
            jvResult[jss::validated] = false;
        else if (ledger_request == LEDGER_VALIDATED)
        {
            lpLedger->setValidated();
            jvResult[jss::validated] = true;
        }
        else
        {
            try
            {
                // Use the skip list in the last validated ledger to see if lpLedger
                // comes after the last validated ledger (and thus has been validated)
                if (uLedger == getApp().getLedgerMaster ().walkHashBySeq (iLedgerIndex))
                {
                    lpLedger->setValidated();
                    jvResult[jss::validated] = true;
                }
                else
                {
                    jvResult[jss::validated] = false;
                }
            }
            catch (SHAMapMissingNode const&)
            {
                jvResult[jss::validated] = false;
            }
        }
    }

    return jvResult;
}

} // RPC
} // ripple
