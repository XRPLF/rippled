#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/SignerUtils.h>
#include <test/jtx/amount.h>
#include <test/jtx/owners.h>
#include <test/jtx/tags.h>
#include <test/jtx/utility.h>

#include <xrpl/protocol/TxFlags.h>

#include <concepts>
#include <cstdint>
#include <optional>

namespace xrpl {
namespace test {
namespace jtx {

/**
 * Contract operations
 */
namespace contract {

json::Value
create(jtx::Account const& account, std::string const& contractCode);

json::Value
create(jtx::Account const& account, uint256 const& contractHash);

json::Value
modify(
    jtx::Account const& account,
    jtx::Account const& contractAccount,
    std::string const& contractCode);

json::Value
modify(
    jtx::Account const& account,
    jtx::Account const& contractAccount,
    uint256 const& contractHash);

json::Value
modify(jtx::Account const& account, jtx::Account const& contractAccount, jtx::Account const& owner);

json::Value
del(jtx::Account const& account, jtx::Account const& contractAccount);

json::Value
call(
    jtx::Account const& account,
    jtx::Account const& contractAccount,
    std::string const& functionName);

json::Value
userDelete(jtx::Account const& account, jtx::Account const& contractAccount);

/**
 * Add Function on a JTx.
 */
class add_function
{
private:
    std::string const name_;
    std::vector<std::tuple<std::uint32_t, std::string, std::string>> call_params_;

public:
    explicit add_function(
        std::string const& name,
        std::vector<std::tuple<std::uint32_t, std::string, std::string>> call_params)
        : name_{name}, call_params_{std::move(call_params)}
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/**
 * Add Instance Parameter on a JTx.
 */
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
                jtx.jv[sfInstanceParameters.fieldName] = json::Value(json::ValueType::Array);
            }
            json::Value param = json::Value(json::ValueType::Object);
            param[sfInstanceParameter.fieldName][sfParameterFlag.fieldName] = flags_;
            param[sfInstanceParameter.fieldName][sfParameterType.fieldName][jss::type] = type_;
            jtx.jv[sfInstanceParameters.fieldName].append(param);
        }

        // Add instance Parameter Values
        if (!jtx.jv.isMember(sfInstanceParameterValues.fieldName))
        {
            jtx.jv[sfInstanceParameterValues.fieldName] = json::Value(json::ValueType::Array);
        }
        json::Value param = json::Value(json::ValueType::Object);
        param[sfInstanceParameterValue.fieldName][sfParameterFlag.fieldName] = flags_;
        param[sfInstanceParameterValue.fieldName][sfParameterValue.fieldName][jss::type] = type_;
        param[sfInstanceParameterValue.fieldName][sfParameterValue.fieldName][jss::value] = value_;
        jtx.jv[sfInstanceParameterValues.fieldName].append(param);
    }
};

/**
 * Add Parameter Value on a JTx.
 */
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
        json::Value param = json::Value(json::ValueType::Object);
        param[sfParameter] = json::Value(json::ValueType::Object);
        param[sfParameter][sfParameterFlag] = flags_;
        param[sfParameter][sfParameterValue] = json::Value(json::ValueType::Object);
        param[sfParameter][sfParameterValue][jss::type] = type_;
        param[sfParameter][sfParameterValue][jss::value] = value_;
        jtx.jv[sfParameters].append(param);
    }
};

}  // namespace contract

}  // namespace jtx

}  // namespace test
}  // namespace xrpl
