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
#include <ripple/ledger/ApplyView.h>
#include <ripple/rpc/Role.h>

namespace ripple {

// Forward declarations
class Application;
class LoadFeeTrack;
class Transaction;
class TxQ;

namespace RPC {

/** Fill in the fee on behalf of the client.
    This is called when the client does not explicitly specify the fee.
    The client may also put a ceiling on the amount of the fee. This ceiling
    is expressed as a multiplier based on the current ledger's fee schedule.

    JSON fields

    "Fee"   The fee paid by the transaction. Omitted when the client
            wants the fee filled in.

    "fee_mult_max"  A multiplier applied to the current ledger's transaction
                    fee that caps the maximum fee the server should auto fill.
                    If this optional field is not specified, then a default
                    multiplier is used.
    "fee_div_max"   A divider applied to the current ledger's transaction
                    fee that caps the maximum fee the server should auto fill.
                    If this optional field is not specified, then a default
                    divider (1) is used. "fee_mult_max" and "fee_div_max"
                    are both used such that the maximum fee will be
                    `base * fee_mult_max / fee_div_max` as an integer.

    @param tx       The JSON corresponding to the transaction to fill in.
    @param ledger   A ledger for retrieving the current fee schedule.
    @param roll     Identifies if this is called by an administrative endpoint.

    @return         A JSON object containing the error results, if any
*/
Json::Value
checkFee(
    Json::Value& request,
    Role const role,
    bool doAutoFill,
    Config const& config,
    LoadFeeTrack const& feeTrack,
    TxQ const& txQ,
    Application const& app);

// Return a std::function<> that calls NetworkOPs::processTransaction.
using ProcessTransactionFn = std::function<void(
    std::shared_ptr<Transaction>& transaction,
    bool bUnlimited,
    bool bLocal,
    NetworkOPs::FailHard failType)>;

inline ProcessTransactionFn
getProcessTxnFn(NetworkOPs& netOPs)
{
    return [&netOPs](
               std::shared_ptr<Transaction>& transaction,
               bool bUnlimited,
               bool bLocal,
               NetworkOPs::FailHard failType) {
        netOPs.processTransaction(transaction, bUnlimited, bLocal, failType);
    };
}

/** Returns a Json::objectValue. */
Json::Value
transactionSign(
    Json::Value params,  // Passed by value so it can be modified locally.
    NetworkOPs::FailHard failType,
    Role role,
    std::chrono::seconds validatedLedgerAge,
    Application& app);

/** Returns a Json::objectValue. */
Json::Value
transactionSubmit(
    Json::Value params,  // Passed by value so it can be modified locally.
    NetworkOPs::FailHard failType,
    Role role,
    std::chrono::seconds validatedLedgerAge,
    Application& app,
    ProcessTransactionFn const& processTransaction);

/** Returns a Json::objectValue. */
Json::Value
transactionSignFor(
    Json::Value params,  // Passed by value so it can be modified locally.
    NetworkOPs::FailHard failType,
    Role role,
    std::chrono::seconds validatedLedgerAge,
    Application& app);

/** Returns a Json::objectValue. */
Json::Value
transactionSubmitMultiSigned(
    Json::Value params,  // Passed by value so it can be modified locally.
    NetworkOPs::FailHard failType,
    Role role,
    std::chrono::seconds validatedLedgerAge,
    Application& app,
    ProcessTransactionFn const& processTransaction);

}  // namespace RPC
}  // namespace ripple

#endif
