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

#ifndef RIPPLE_TEST_JTX_CONTRACT_H_INCLUDED
#define RIPPLE_TEST_JTX_CONTRACT_H_INCLUDED

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/owners.h>
#include <test/jtx/tags.h>
#include <test/jtx/utility.h>

#include <xrpl/protocol/TxFlags.h>

#include "test/jtx/SignerUtils.h"

#include <concepts>
#include <cstdint>
#include <optional>

namespace ripple {
namespace test {
namespace jtx {

/** Contract operations */
namespace contract {

Json::Value
create(jtx::Account const& account, std::string const& contractCode);

Json::Value
create(jtx::Account const& account, uint256 const& contractHash);

Json::Value
call(jtx::Account const& account, std::string const& contractAccount, std::string const& functionName);

Json::Value
addFuncParam(std::string const& name, std::string const& typeName);

/** Add Function on a JTx. */
class add_function
{
private:
    std::string const name_;
    std::vector<std::pair<std::string, std::string>> params_;

public:
    explicit add_function(
        std::string const& name,
        std::vector<std::pair<std::string, std::string>> params)
        : name_{name}, params_{std::move(params)}
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Add Parameter Value on a JTx. */
template <typename T>
class add_param_value
{
private:
    std::string name_;
    std::string type_;
    T value_;

public:
    explicit add_param_value(std::string const& name, std::string const& type, T value)
        : name_(name), type_(type), value_(value)
    {
    }

    void
    operator()(Env&, JTx& jtx) const
    {
        Json::Value param = Json::Value(Json::objectValue);
        param[sfInstanceParameter] = Json::Value(Json::objectValue);
        param[sfInstanceParameter][sfParameterValue] =
            Json::Value(Json::objectValue);
        param[sfInstanceParameter][sfParameterValue][jss::name] = strHex(name_);
        param[sfInstanceParameter][sfParameterValue][jss::type] = type_;
        param[sfInstanceParameter][sfParameterValue][jss::value] = value_;
        jtx.jv[sfInstanceParameters].append(param);
    }
};

}  // namespace contract

}  // namespace jtx

}  // namespace test
}  // namespace ripple

#endif