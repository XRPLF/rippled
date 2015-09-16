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

#ifndef RIPPLE_RPC_TRANSACTIONSIGN_H_INCLUDED
#define RIPPLE_RPC_TRANSACTIONSIGN_H_INCLUDED

#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/server/Role.h>

namespace ripple {

// Forward declarations
class Application;
class LoadFeeTrack;

namespace RPC {

/** Fill in the fee on behalf of the client.
    This is called when the client does not explicitly specify the fee.
    The client may also put a ceiling on the amount of the fee. This ceiling
    is expressed as a multiplier based on the current ledger's fee schedule.

    JSON fields

    "Fee"   The fee paid by the transaction. Omitted when the client
            wants the fee filled in.

    "fee_mult_max"  A multiplier applied to the current ledger's transaction
                    fee that caps the maximum the fee server should auto fill.
                    If this optional field is not specified, then a default
                    multiplier is used.

    @param tx       The JSON corresponding to the transaction to fill in.
    @param ledger   A ledger for retrieving the current fee schedule.
    @param roll     Identifies if this is called by an administrative endpoint.

    @return         A JSON object containing the error results, if any
*/
Json::Value checkFee (
    Json::Value& request,
    Role const role,
    bool doAutoFill,
    LoadFeeTrack const& feeTrack,
    std::shared_ptr<ReadView const>& ledger);

// Return a std::function<> that calls NetworkOPs::processTransaction.
using ProcessTransactionFn =
    std::function<void (Transaction::pointer& transaction,
        bool bAdmin, bool bLocal, NetworkOPs::FailHard failType)>;

inline ProcessTransactionFn getProcessTxnFn (NetworkOPs& netOPs)
{
    return [&netOPs](Transaction::pointer& transaction,
        bool bAdmin, bool bLocal, NetworkOPs::FailHard failType)
    {
        netOPs.processTransaction(transaction, bAdmin, bLocal, failType);
    };
}

/** Returns a Json::objectValue. */
Json::Value transactionSign (
    Json::Value params,  // Passed by value so it can be modified locally.
    NetworkOPs::FailHard failType,
    Role role,
    int validatedLedgerAge,
    Application& app,
    std::shared_ptr<ReadView const> ledger);

/** Returns a Json::objectValue. */
Json::Value transactionSubmit (
    Json::Value params,  // Passed by value so it can be modified locally.
    NetworkOPs::FailHard failType,
    Role role,
    int validatedLedgerAge,
    Application& app,
    std::shared_ptr<ReadView const> ledger,
    ProcessTransactionFn const& processTransaction);

/** Returns a Json::objectValue. */
Json::Value transactionSignFor (
    Json::Value params,  // Passed by value so it can be modified locally.
    NetworkOPs::FailHard failType,
    Role role,
    int validatedLedgerAge,
    Application& app,
    std::shared_ptr<ReadView const> ledger);

/** Returns a Json::objectValue. */
Json::Value transactionSubmitMultiSigned (
    Json::Value params,  // Passed by value so it can be modified locally.
    NetworkOPs::FailHard failType,
    Role role,
    int validatedLedgerAge,
    Application& app,
    std::shared_ptr<ReadView const> ledger,
    ProcessTransactionFn const& processTransaction);

} // RPC
} // ripple

#endif
