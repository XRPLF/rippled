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

namespace ripple {

class ContractDataMap
    : public std::map<ripple::AccountID, std::pair<bool, STJson>>
{
public:
    uint32_t modifiedCount = 0;
};

class ContractEventMap : public std::map<std::string, STJson>
{
};

struct ParameterValueVec
{
    ripple::STData const value;
};

struct FunctionParameterValueVecWithName
{
    ripple::Blob const name;
    ripple::STData const value;
};

struct ParameterTypeVec
{
    ripple::Blob const name;
    ripple::STDataType const type;
};

std::vector<ParameterValueVec>
getParameterValueVec(ripple::STArray const& functionParameters);

std::vector<ParameterTypeVec>
getParameterTypeVec(ripple::STArray const& functionParameters);

enum ExitType : uint8_t {
    UNSET = 0,
    WASM_ERROR = 1,
    ROLLBACK = 2,
    ACCEPT = 3,
};

struct ContractResult
{
    ripple::uint256 const contractHash;
    ripple::Keylet const contractKeylet;
    ripple::Keylet const contractSourceKeylet;
    ripple::Keylet const contractAccountKeylet;
    ripple::AccountID const contractAccount;
    std::uint32_t nextSequence;
    ripple::AccountID const otxnAccount;
    ripple::ExitType exitType = ripple::ExitType::ROLLBACK;
    int64_t exitCode{-1};
    ContractDataMap dataMap;
    ContractEventMap eventMap;
    std::queue<std::shared_ptr<ripple::Transaction>> emittedTxns{};
    std::size_t changedDataCount{0};
};

struct ContractContext
{
    ripple::ApplyContext& applyCtx;
    std::vector<ParameterValueVec> instanceParameters;
    std::vector<ParameterValueVec> functionParameters;
    std::vector<STObject> built_txns;
    int64_t expected_etxn_count{-1};
    std::map<ripple::uint256, bool> nonce_used{};
    uint32_t generation = 0;
    uint64_t burden = 0;
    ContractResult result;
};

}  // namespace ripple
