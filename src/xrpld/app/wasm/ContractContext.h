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

#pragma once

#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/tx/detail/ApplyContext.h>

#include <xrpl/ledger/Sandbox.h>
#include <xrpl/protocol/STData.h>
#include <xrpl/protocol/STDataType.h>

#include <queue>

namespace xrpl {

class ContractDataMap : public std::map<xrpl::AccountID, std::pair<bool, STJson>>
{
public:
    uint32_t modifiedCount = 0;
};

class ContractEventMap : public std::map<std::string, STJson>
{
};

struct ParameterValueVec
{
    xrpl::STData const value;
};

struct FunctionParameterValueVecWithName
{
    xrpl::Blob const name;
    xrpl::STData const value;
};

struct ParameterTypeVec
{
    xrpl::STDataType const type;
};

std::vector<ParameterValueVec>
getParameterValueVec(xrpl::STArray const& functionParameters);

std::vector<ParameterTypeVec>
getParameterTypeVec(xrpl::STArray const& functionParameters);

enum ExitType : uint8_t {
    UNSET = 0,
    WASM_ERROR = 1,
    ROLLBACK = 2,
    ACCEPT = 3,
};

struct ContractResult
{
    xrpl::uint256 const contractHash;          // Hash of the contract code
    xrpl::Keylet const contractKeylet;         // Keylet for the contract instance
    xrpl::Keylet const contractSourceKeylet;   // Keylet for the contract source
    xrpl::Keylet const contractAccountKeylet;  // Keylet for the contract account
    xrpl::AccountID const contractAccount;     // AccountID of the contract account
    std::uint32_t nextSequence;                // Next sequence number for the contract account
    xrpl::AccountID const otxnAccount;         // AccountID for the originating transaction
    xrpl::uint256 const otxnId;                // ID for the originating transaction
    std::string exitReason{""};
    int64_t exitCode{-1};
    ContractDataMap dataMap;
    ContractEventMap eventMap;
    std::queue<std::shared_ptr<xrpl::Transaction>> emittedTxns{};
    std::size_t changedDataCount{0};
};

struct ContractContext
{
    xrpl::ApplyContext& applyCtx;
    std::vector<ParameterValueVec> instanceParameters;
    std::vector<ParameterValueVec> functionParameters;
    std::vector<STObject> built_txns;
    int64_t expected_etxn_count{-1};             // expected emitted transaction count
    std::map<xrpl::uint256, bool> nonce_used{};  // nonces used in this execution
    uint32_t generation = 0;                     // generation of the contract being executed
    uint64_t burden = 0;                         // computational burden used
    ContractResult result;
};

}  // namespace xrpl
