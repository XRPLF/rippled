//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2014 Ripple Labs Inc.

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
#include <ripple/app/tx/impl/CheckAndConsumeTicket.h>
#include <ripple/app/tx/impl/Transactor.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Indexes.h>

namespace ripple {

class CancelTicket
    : public Transactor
{
public:
    CancelTicket (
        STTx const& txn,
        TransactionEngineParams params,
        TransactionEngine* engine)
        : Transactor (
            txn,
            params,
            engine,
            deprecatedLogs().journal("CancelTicket"))
    {

    }

    TER preCheck () override
    {
        // A Ticket may not be used to cancel a Ticket.  So a Sequence of
        // zero (which indicates that a Ticket is being used) is malformed.
        if (mTxn.getSequence () == 0)
        {
            // To preserve transaction backward compatibility we return
            // a "less good" error code if Tickets are disabled.  When
            // Tickets are enabled we're changing transaction behavior
            // anyway, so we can return the better code.
            if (mEngine->enableTickets())
                return temBAD_SEQUENCE;
            else
                return tefPAST_SEQ;
        }
        return Transactor::preCheck ();
    }

    TER doApply () override
    {
        assert (mTxnAccount);

        // See if we can legitimately use the Ticket.  Note that canceling
        // an expired Ticket is a tesSUCCESS.
        return checkAndConsumeCancelTicket (mTxn, mTxnAccountID, mEngine);
    }
};

TER
transact_CancelTicket (
    STTx const& txn,
    TransactionEngineParams params,
    TransactionEngine* engine)
{
    if (! engine->enableTickets())
        return temDISABLED;
    return CancelTicket (txn, params, engine).apply();
}


}
