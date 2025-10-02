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
modify(
    jtx::Account const& account,
    jtx::Account const& contractAccount,
    std::string const& contractCode);

Json::Value
modify(
    jtx::Account const& account,
    jtx::Account const& contractAccount,
    uint256 const& contractHash);

Json::Value
del(jtx::Account const& account, jtx::Account const& contractAccount);

Json::Value
call(
    jtx::Account const& account,
    jtx::Account const& contractAccount,
    std::string const& functionName);

/** Add Function on a JTx. */
class add_function
{
private:
    std::string const name_;
    std::vector<std::tuple<std::uint32_t, std::string, std::string>>
        call_params_;

public:
    explicit add_function(
        std::string const& name,
        std::vector<std::tuple<std::uint32_t, std::string, std::string>>
            call_params)
        : name_{name}, call_params_{std::move(call_params)}
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Add Instance Parameter on a JTx. */
template <typename T>
class add_instance_param
{
private:
    std::uint32_t flags_;
    std::string name_;
    std::string type_;
    T value_;

public:
    explicit add_instance_param(
        std::uint32_t flags,
        std::string const& name,
        std::string const& type,
        T value)
        : flags_{flags}, name_{name}, type_{type}, value_{value}
    {
    }

    void
    operator()(Env&, JTx& jtx) const
    {
        if (jtx.jv.isMember(sfContractCode.fieldName))
        {
            // Add instance Parameters
            if (!jtx.jv.isMember(sfInstanceParameters.fieldName))
            {
                jtx.jv[sfInstanceParameters.fieldName] =
                    Json::Value(Json::arrayValue);
            }
            Json::Value param = Json::Value(Json::objectValue);
            param[sfInstanceParameter.fieldName][sfParameterFlag.fieldName] =
                flags_;
            param[sfInstanceParameter.fieldName][sfParameterName.fieldName] =
                strHex(name_);
            param[sfInstanceParameter.fieldName][sfParameterType.fieldName]
                 [jss::type] = type_;
            jtx.jv[sfInstanceParameters.fieldName].append(param);
        }

        // Add instance Parameter Values
        if (!jtx.jv.isMember(sfInstanceParameterValues.fieldName))
        {
            jtx.jv[sfInstanceParameterValues.fieldName] =
                Json::Value(Json::arrayValue);
        }
        Json::Value param = Json::Value(Json::objectValue);
        param[sfInstanceParameterValue.fieldName][sfParameterFlag.fieldName] =
            flags_;
        param[sfInstanceParameterValue.fieldName][sfParameterValue.fieldName]
             [jss::name] = strHex(name_);
        param[sfInstanceParameterValue.fieldName][sfParameterValue.fieldName]
             [jss::type] = type_;
        param[sfInstanceParameterValue.fieldName][sfParameterValue.fieldName]
             [jss::value] = value_;
        jtx.jv[sfInstanceParameterValues.fieldName].append(param);
    }
};

/** Add Parameter Value on a JTx. */
template <typename T>
class add_param
{
private:
    std::uint32_t flags_;
    std::string name_;
    std::string type_;
    T value_;

public:
    explicit add_param(
        std::uint32_t flags,
        std::string const& name,
        std::string const& type,
        T value)
        : flags_(flags), name_(name), type_(type), value_(value)
    {
    }

    void
    operator()(Env&, JTx& jtx) const
    {
        Json::Value param = Json::Value(Json::objectValue);
        param[sfParameter] = Json::Value(Json::objectValue);
        param[sfParameter][sfParameterFlag] = flags_;
        param[sfParameter][sfParameterValue] = Json::Value(Json::objectValue);
        param[sfParameter][sfParameterValue][jss::name] = strHex(name_);
        param[sfParameter][sfParameterValue][jss::type] = type_;
        param[sfParameter][sfParameterValue][jss::value] = value_;
        jtx.jv[sfParameters].append(param);
    }
};

}  // namespace contract

}  // namespace jtx

}  // namespace test
}  // namespace ripple

#endif
