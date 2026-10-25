#include <test/jtx/vault_manager.h>

#include <test/jtx/Env.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace xrpl::test::jtx::vault_manager {

json::Value
set(AccountID const& account, uint256 const& vaultID, std::uint32_t flags)
{
    json::Value jv;
    jv[sfTransactionType] = jss::VaultManagerSet;
    jv[sfAccount] = to_string(account);
    jv[sfVaultID] = to_string(vaultID);
    jv[sfFlags] = flags;
    return jv;
}

json::Value
mandateEntry(
    Asset const& asset,
    Number const& positionMaximum,
    std::optional<uint256> const& domainID,
    std::optional<std::uint8_t> const& markSource)
{
    json::Value inner;
    inner[sfAsset] = toJson(asset);
    if (positionMaximum != Number{})
        inner[sfPositionMaximum] = to_string(positionMaximum);
    if (domainID)
        inner[sfDomainID] = to_string(*domainID);
    if (markSource)
        inner[sfMarkSource] = *markSource;
    json::Value entry;
    entry[sfMandateEntry] = inner;
    return entry;
}

void
Mandate::operator()(Env&, JTx& jt) const
{
    json::Value arr{json::ValueType::Array};
    for (auto const& e : entries)
        arr.append(e);
    jt.jv[sfMandate.jsonName] = arr;
}

Mandate
mandate(std::vector<json::Value> entries)
{
    return Mandate{.entries = std::move(entries)};
}

json::Value
trade(
    AccountID const& account,
    uint256 const& vaultID,
    STAmount const& amount,
    STAmount const& sendMax,
    std::uint32_t flags)
{
    json::Value jv;
    jv[sfTransactionType] = jss::VaultTrade;
    jv[sfAccount] = to_string(account);
    jv[sfVaultID] = to_string(vaultID);
    jv[sfAmount] = amount.getJson(JsonOptions::Values::None);
    jv[sfSendMax] = sendMax.getJson(JsonOptions::Values::None);
    jv[sfFlags] = flags;
    return jv;
}

json::Value
mark(AccountID const& account, uint256 const& vaultID, Asset const& asset, Number const& price)
{
    json::Value jv;
    jv[sfTransactionType] = jss::VaultMark;
    jv[sfAccount] = to_string(account);
    jv[sfVaultID] = to_string(vaultID);
    jv[sfAsset] = toJson(asset);
    jv[sfMarkPrice] = to_string(price);
    return jv;
}

json::Value
cover(AccountID const& account, uint256 const& vaultID, STAmount const& amount, std::uint32_t flags)
{
    json::Value jv;
    jv[sfTransactionType] = jss::VaultManagerCover;
    jv[sfAccount] = to_string(account);
    jv[sfVaultID] = to_string(vaultID);
    jv[sfAmount] = amount.getJson(JsonOptions::Values::None);
    jv[sfFlags] = flags;
    return jv;
}

json::Value
claim(AccountID const& account, uint256 const& vaultID)
{
    json::Value jv;
    jv[sfTransactionType] = jss::VaultManagerClaim;
    jv[sfAccount] = to_string(account);
    jv[sfVaultID] = to_string(vaultID);
    return jv;
}

json::Value
del(AccountID const& account, uint256 const& vaultID)
{
    json::Value jv;
    jv[sfTransactionType] = jss::VaultManagerDelete;
    jv[sfAccount] = to_string(account);
    jv[sfVaultID] = to_string(vaultID);
    return jv;
}

}  // namespace xrpl::test::jtx::vault_manager
