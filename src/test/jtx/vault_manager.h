#pragma once

#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace xrpl::test::jtx::vault_manager {

// VaultManagerSet: create, propose, or with a flag veto, cancel or halt.
json::Value
set(AccountID const& account, uint256 const& vaultID, std::uint32_t flags = 0);

// One sfMandateEntry object, for mandate().
json::Value
mandateEntry(
    Asset const& asset,
    Number const& positionMaximum = Number{},
    std::optional<uint256> const& domainID = std::nullopt,
    std::optional<std::uint8_t> const& markSource = std::nullopt);

// Funclet setting sfMandate to the given entries.
struct Mandate
{
    std::vector<json::Value> entries;

    void
    operator()(Env&, JTx& jt) const;
};

Mandate
mandate(std::vector<json::Value> entries);

// VaultTrade: receive amount, spend at most sendMax.
json::Value
trade(
    AccountID const& account,
    uint256 const& vaultID,
    STAmount const& amount,
    STAmount const& sendMax,
    std::uint32_t flags = 0);

// VaultMark: write one position's price in units of the vault asset.
json::Value
mark(AccountID const& account, uint256 const& vaultID, Asset const& asset, Number const& price);

// VaultManagerCover with exactly one of tfCoverDeposit, tfCoverWithdraw, tfCoverClawback.
json::Value
cover(
    AccountID const& account,
    uint256 const& vaultID,
    STAmount const& amount,
    std::uint32_t flags);

// VaultManagerClaim: crystallize fees.
json::Value
claim(AccountID const& account, uint256 const& vaultID);

// Use "del" because "delete" is a reserved word in C++.
json::Value
del(AccountID const& account, uint256 const& vaultID);

auto const kManager = JTxFieldWrapper<AccountIdField>(sfManager);
auto const kValuer = JTxFieldWrapper<AccountIdField>(sfValuer);
auto const kGuardian = JTxFieldWrapper<AccountIdField>(sfGuardian);
auto const kDeployedMaximum = simpleField<SF_NUMBER>(sfDeployedMaximum);
auto const kMarkMoveLimit = simpleField<SF_UINT32>(sfMarkMoveLimit);
auto const kMarkHaltPeriod = simpleField<SF_UINT32>(sfMarkHaltPeriod);
auto const kMarkStaleAfter = simpleField<SF_UINT32>(sfMarkStaleAfter);
auto const kTimelock = simpleField<SF_UINT32>(sfTimelock);
auto const kPerformanceFeeRate = simpleField<SF_UINT32>(sfPerformanceFeeRate);
auto const kManagementFeeRate = simpleField<SF_UINT16>(sfManagementFeeRate);
auto const kCoverRateMinimum = simpleField<SF_UINT32>(sfCoverRateMinimum);
auto const kCoverRateLiquidation = simpleField<SF_UINT32>(sfCoverRateLiquidation);
auto const kDeliverMin = JTxFieldWrapper<StAmountField>(sfDeliverMin);
auto const kDomain = JTxFieldWrapper<UInt256Field>(sfDomainID);

}  // namespace xrpl::test::jtx::vault_manager
