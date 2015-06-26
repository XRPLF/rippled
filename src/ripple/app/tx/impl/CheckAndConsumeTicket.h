//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_TX_CHECKANDCONSUMETICKET_H_INCLUDED
#define RIPPLE_TX_CHECKANDCONSUMETICKET_H_INCLUDED

#include <ripple/protocol/TER.h>
#include <ripple/protocol/UintTypes.h>                    // Account

namespace ripple
{
// Forward declarations
class STTx;
class TransactionEngine;

// Function that checks and consumes a ticket used as a transaction sequence.
TER checkAndConsumeSeqTicket (STTx const& txn,
    AccountID const& txnAccountID, TransactionEngine* const txnEngine);

// Function that checks and consumes a Ticket for a TICKET_CANCEL transaction.
TER checkAndConsumeCancelTicket (STTx const& txn,
    AccountID const& txnAccountID, TransactionEngine* const txnEngine);
}

#endif // RIPPLE_TX_CHECKANDCONSUMETICKET_H_INCLUDED
