#include <test/jtx/contract.h>

#include <test/jtx/utility.h>

#include <optional>
#include <sstream>

namespace xrpl {
namespace test {
namespace jtx {

namespace contract {

json::Value
create(jtx::Account const& account, std::string const& contractCode)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::ContractCreate;
    jv[jss::Account] = account.human();
    jv[sfContractCode] = contractCode;
    return jv;
}

json::Value
create(jtx::Account const& account, uint256 const& contractHash)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::ContractCreate;
    jv[jss::Account] = account.human();
    jv[sfContractHash] = to_string(contractHash);
    return jv;
}

json::Value
modify(
    jtx::Account const& account,
    jtx::Account const& contractAccount,
    std::string const& contractCode)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::ContractModify;
    jv[jss::Account] = account.human();
    jv[sfContractAccount] = contractAccount.human();
    jv[sfContractCode] = contractCode;
    return jv;
}

json::Value
modify(
    jtx::Account const& account,
    jtx::Account const& contractAccount,
    uint256 const& contractHash)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::ContractModify;
    jv[jss::Account] = account.human();
    jv[sfContractAccount] = contractAccount.human();
    jv[sfContractHash] = to_string(contractHash);
    return jv;
}

json::Value
modify(jtx::Account const& account, jtx::Account const& contractAccount, jtx::Account const& owner)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::ContractModify;
    jv[jss::Account] = account.human();
    jv[sfContractAccount] = contractAccount.human();
    jv[sfOwner] = owner.human();
    return jv;
}

json::Value
del(jtx::Account const& account, jtx::Account const& contractAccount)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::ContractDelete;
    jv[jss::Account] = account.human();
    jv[sfContractAccount] = contractAccount.human();
    return jv;
}

json::Value
call(
    jtx::Account const& account,
    jtx::Account const& contractAccount,
    std::string const& functionName)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::ContractCall;
    jv[jss::Account] = account.human();
    jv[sfContractAccount] = contractAccount.human();
    jv[sfFunctionName] = strHex(functionName);
    jv[sfParameters] = json::Value(json::ValueType::Array);
    return jv;
}

json::Value
userDelete(jtx::Account const& account, jtx::Account const& contractAccount)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::ContractUserDelete;
    jv[jss::Account] = account.human();
    jv[sfContractAccount] = contractAccount.human();
    return jv;
}

json::Value
addCallParam(std::uint32_t const& flags, std::string const& name, std::string const& typeName)
{
    json::Value param = json::Value(json::ValueType::Object);
    param[sfParameter][sfParameterFlag] = flags;
    param[sfParameter][sfParameterType][jss::type] = typeName;
    return param;
};

void
add_function::operator()(Env&, JTx& jt) const
{
    auto const index = jt.jv[sfFunctions].size();
    json::Value& function = jt.jv[sfFunctions][index];

    function = json::Value{};
    function[sfFunction][sfFunctionName] = strHex(name_);
    for (auto const& [p_flags, p_name, p_type] : call_params_)
    {
        function[sfFunction][sfParameters].append(addCallParam(p_flags, p_name, p_type));
    }
}

}  // namespace contract

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
