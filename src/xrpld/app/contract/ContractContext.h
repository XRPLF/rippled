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

#ifndef RIPPLE_TX_CONTRACTCONTEXT_H_INCLUDED
#define RIPPLE_TX_CONTRACTCONTEXT_H_INCLUDED

#include <xrpld/app/tx/detail/ApplyContext.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpl/protocol/STData.h>
#include <xrpl/protocol/STDataType.h>

namespace ripple {

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
    ripple::AccountID const otxnAccount;

    std::queue<std::shared_ptr<ripple::Transaction>> emittedTxn{};
    ripple::ExitType exitType = ripple::ExitType::ROLLBACK;
    std::string exitReason{""};
    int64_t exitCode{-1};
};

struct ContractContext
{
    ripple::ApplyContext& applyCtx;
    std::vector<ParameterValueVec> callParameters;
    std::vector<ParameterValueVec> funcParameters;
    int64_t expected_etxn_count{-1};
    std::map<ripple::uint256, bool> nonce_used{};
    uint32_t generation = 0;
    uint64_t burden = 0;
    ContractResult result;
};


}  // namespace ripple

#endif
