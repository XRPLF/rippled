//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_WASM_CONTRACTUTILS_H_INCLUDED
#define RIPPLE_APP_WASM_CONTRACTUTILS_H_INCLUDED

#include <xrpld/app/wasm/ContractContext.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STData.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {
namespace contract {

/** The maximum number of data modifications in a single function. */
int64_t constexpr maxDataModifications = 1000;

/** The maximum number of bytes the data can occupy. */
int64_t constexpr maxContractDataSize = 1024;

/** The multiplier for contract data size calculations. */
int64_t constexpr dataByteMultiplier = 512;

/** The cost multiplier of creating a contract in bytes. */
int64_t constexpr createByteMultiplier = 500ULL;

/** The value to return when the fee calculation failed. */
int64_t constexpr feeCalculationFailed = 0x7FFFFFFFFFFFFFFFLL;

/** The maximum number of contract parameters that can be in a transaction. */
std::size_t constexpr maxContractParams = 32;

/** The maximum number of contract functions that can be in a transaction. */
std::size_t constexpr maxContractFunctions = 12;

int64_t
contractCreateFee(uint64_t byteCount);

NotTEC
preflightFunctions(STTx const& tx, beast::Journal j);

NotTEC
preflightInstanceParameters(STTx const& tx, beast::Journal j);

bool
validateParameterMapping(
    STArray const& params,
    STArray const& values,
    beast::Journal j);

NotTEC
preflightInstanceParameterValues(STTx const& tx, beast::Journal j);

bool
isValidParameterFlag(std::uint32_t flags);

TER
handleFlagParameters(
    ApplyView& view,
    STTx const& tx,
    AccountID const& sourceAccount,
    AccountID const& contractAccount,
    STArray const& parameters,
    XRPAmount const& priorBalance,
    beast::Journal j);

TER
finalizeContractData(
    ApplyContext& applyCtx,
    AccountID const& contractAccount,
    ContractDataMap const& dataMap,
    ContractEventMap const& eventMap,
    uint256 const& txnID);

}  // namespace contract
}  // namespace ripple

#endif
