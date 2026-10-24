#include <test/jtx/contract.h>

#include <test/jtx/utility.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/strHex.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

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

std::string
wasmOfSize(std::size_t bytes, std::string const& exportName, std::int32_t result)
{
    if (exportName.size() > 100 || result < -64 || result > 63)
        throw std::invalid_argument("wasmOfSize: export name or result out of range");

    // Header, then one type `() -> i32` and one function of that type.
    Blob wasm{
        0x00,
        0x61,
        0x73,
        0x6D,
        0x01,
        0x00,
        0x00,
        0x00,
        0x01,
        0x05,
        0x01,
        0x60,
        0x00,
        0x01,
        0x7F,
        0x03,
        0x02,
        0x01,
        0x00};

    // Export section: the function under `exportName`.
    wasm.push_back(0x07);
    wasm.push_back(static_cast<std::uint8_t>(exportName.size() + 4));
    wasm.push_back(0x01);
    wasm.push_back(static_cast<std::uint8_t>(exportName.size()));
    wasm.insert(wasm.end(), exportName.begin(), exportName.end());
    wasm.push_back(0x00);
    wasm.push_back(0x00);

    // Code section: `i32.const result; end`, the constant as one signed LEB128 byte.
    Blob const code{
        0x0A, 0x06, 0x01, 0x04, 0x00, 0x41, static_cast<std::uint8_t>(result & 0x7F), 0x0B};
    wasm.insert(wasm.end(), code.begin(), code.end());

    if (bytes == wasm.size())
        return strHex(wasm);

    // Custom section with an empty name: its length in five LEB128 bytes, so the
    // padding needs no search over encodings.
    std::size_t constexpr kCustomOverhead = 1 + 5 + 1;
    if (bytes < wasm.size() + kCustomOverhead)
        throw std::invalid_argument("wasmOfSize: too small to pad");
    auto const padding = bytes - wasm.size() - kCustomOverhead;
    auto content = static_cast<std::uint32_t>(padding + 1);
    wasm.push_back(0x00);
    for (int i = 0; i < 4; ++i)
    {
        wasm.push_back(static_cast<std::uint8_t>((content & 0x7F) | 0x80));
        content >>= 7;
    }
    wasm.push_back(static_cast<std::uint8_t>(content & 0x7F));
    wasm.push_back(0x00);
    wasm.insert(wasm.end(), padding, 0x00);
    return strHex(wasm);
}

}  // namespace contract

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
