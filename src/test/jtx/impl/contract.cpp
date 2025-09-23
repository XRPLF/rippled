//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#include <test/jtx/contract.h>
#include <test/jtx/utility.h>

#include <optional>
#include <sstream>

namespace ripple {
namespace test {
namespace jtx {

namespace contract {

Json::Value
create(jtx::Account const& account, std::string const& contractCode)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::ContractCreate;
    jv[jss::Account] = account.human();
    jv[sfContractCode] = contractCode;
    return jv;
}

Json::Value
create(jtx::Account const& account, uint256 const& contractHash)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::ContractCreate;
    jv[jss::Account] = account.human();
    jv[sfContractHash] = to_string(contractHash);
    return jv;
}

Json::Value
modify(
    jtx::Account const& account,
    jtx::Account const& contractAccount,
    std::string const& contractCode)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::ContractModify;
    jv[jss::Account] = account.human();
    jv[sfContractAccount] = contractAccount.human();
    jv[sfContractCode] = contractCode;
    return jv;
}

Json::Value
modify(
    jtx::Account const& account,
    jtx::Account const& contractAccount,
    uint256 const& contractHash)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::ContractModify;
    jv[jss::Account] = account.human();
    jv[sfContractAccount] = contractAccount.human();
    jv[sfContractHash] = to_string(contractHash);
    return jv;
}

Json::Value
del(jtx::Account const& account, jtx::Account const& contractAccount)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::ContractDelete;
    jv[jss::Account] = account.human();
    jv[sfContractAccount] = contractAccount.human();
    return jv;
}

Json::Value
call(
    jtx::Account const& account,
    jtx::Account const& contractAccount,
    std::string const& functionName)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::ContractCall;
    jv[jss::Account] = account.human();
    jv[sfContractAccount] = contractAccount.human();
    jv[sfFunctionName] = strHex(functionName);
    jv[sfParameters] = Json::Value(Json::arrayValue);
    return jv;
}

Json::Value
addCallParam(
    std::uint32_t const& flags,
    std::string const& name,
    std::string const& typeName)
{
    Json::Value param = Json::Value(Json::objectValue);
    param[sfParameter][sfParameterFlag] = flags;
    param[sfParameter][sfParameterName] = strHex(name);
    param[sfParameter][sfParameterType][jss::type] = typeName;
    return param;
};

void
add_function::operator()(Env&, JTx& jt) const
{
    auto const index = jt.jv[sfFunctions].size();
    Json::Value& function = jt.jv[sfFunctions][index];

    function = Json::Value{};
    function[sfFunction][sfFunctionName] = strHex(name_);
    for (auto const& [p_flags, p_name, p_type] : call_params_)
    {
        function[sfFunction][sfParameters].append(
            addCallParam(p_flags, p_name, p_type));
    }
}

}  // namespace contract

}  // namespace jtx
}  // namespace test
}  // namespace ripple
