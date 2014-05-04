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

    Json::Value ledger_hash = params.get ("ledger_hash", Json::Value ("0"));
    Json::Value ledger_index = params.get ("ledger_index", Json::Value ("current"));

    // Support for DEPRECATED "ledger" - attempt to deduce our input
    if (params.isMember ("ledger"))
    {
        if (params["ledger"].asString ().size () > 12)
        {
            ledger_hash = params["ledger"];
            ledger_index = Json::Value ("");
        }
        else if (params["ledger"].isNumeric ())
        {
            ledger_index = params["ledger"];
            ledger_hash = Json::Value ("0");
        }
        else
        {
            ledger_index = params["ledger"];
            ledger_hash = Json::Value ("0");
        }
    }

    uint256 uLedger (0);

    if (!ledger_hash.isString() || !uLedger.SetHex (ledger_hash.asString ()))
    {
        jvResult["error"] = "ledgerHashMalformed";
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
                jvResult["error"] = "ledgerIndexMalformed";
                return jvResult;
            }
        }
    }

    // The ledger was directly specified by hash.
    if (!!uLedger)
    {
        lpLedger = netOps.getLedgerByHash (uLedger);

        if (!lpLedger)
        {
            jvResult["error"] = "ledgerNotFound";
            return jvResult;
        }

        iLedgerIndex = lpLedger->getLedgerSeq ();
    }

    switch (iLedgerIndex)
    {
    case LEDGER_CURRENT:
        lpLedger = netOps.getCurrentLedger ();
        iLedgerIndex = lpLedger->getLedgerSeq ();
        assert (lpLedger->isImmutable () && !lpLedger->isClosed ());
        break;

    case LEDGER_CLOSED:
        lpLedger = getApp().getLedgerMaster ().getClosedLedger ();
        iLedgerIndex = lpLedger->getLedgerSeq ();
        assert (lpLedger->isImmutable () && lpLedger->isClosed ());
        break;

    case LEDGER_VALIDATED:
        lpLedger = netOps.getValidatedLedger ();
        iLedgerIndex = lpLedger->getLedgerSeq ();
        assert (lpLedger->isImmutable () && lpLedger->isClosed ());
        break;
    }

    if (iLedgerIndex <= 0)
    {
        jvResult["error"] = "ledgerIndexMalformed";
        return jvResult;
    }

    if (!lpLedger)
    {
        lpLedger = netOps.getLedgerBySeq (iLedgerIndex);

        if (!lpLedger)
        {
            jvResult["error"] = "ledgerNotFound"; // ledger_index from future?
            return jvResult;
        }
    }

    if (lpLedger->isClosed ())
    {
        if (!!uLedger)
            jvResult["ledger_hash"] = to_string (uLedger);

        jvResult["ledger_index"] = iLedgerIndex;
    }
    else
    {
        // CHECKME - What is this supposed to signify?
        jvResult["ledger_current_index"] = iLedgerIndex;
    }

    return jvResult;
}

} // RPC
} // ripple
