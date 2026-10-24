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
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

/**
 * Contract operations
 */
namespace xrpl::test::jtx::contract {

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
 * A valid WASM module of exactly `bytes` bytes, as hex.
 *
 * The module exports `exportName` as `() -> i32` returning `result`, and is padded to size
 * with a custom section. `bytes` must leave room for that section; `result` must be in
 * [-64, 63].
 */
std::string
wasmOfSize(std::size_t bytes, std::string const& exportName, std::int32_t result = 1);

/**
 * Add Function on a JTx.
 */
class AddFunction
{
private:
    std::string const name_;
    std::vector<std::tuple<std::uint32_t, std::string, std::string>> callParams_;

public:
    explicit AddFunction(
        std::string name,
        std::vector<std::tuple<std::uint32_t, std::string, std::string>> callParams)
        : name_{std::move(name)}, callParams_{std::move(callParams)}
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/**
 * Add Instance Parameter on a JTx.
 */
template <typename T>
class AddInstanceParam
{
private:
    std::uint32_t flags_;
    std::string name_;
    std::string type_;
    T value_;

public:
    explicit AddInstanceParam(std::uint32_t flags, std::string name, std::string type, T value)
        : flags_{flags}, name_{std::move(name)}, type_{std::move(type)}, value_{value}
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
class AddParam
{
private:
    std::uint32_t flags_;
    std::string name_;
    std::string type_;
    T value_;

public:
    explicit AddParam(std::uint32_t flags, std::string name, std::string type, T value)
        : flags_(flags), name_(std::move(name)), type_(std::move(type)), value_(value)
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

}  // namespace xrpl::test::jtx::contract
