#pragma once

#include <xrpl/basics/Expected.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

#include <optional>

namespace xrpl::vault {

enum class TruncateShares : bool { no = false, yes = true };

// Low-level v2 math — exposed for unit testing.
namespace detail {

[[nodiscard]] STAmount
assetsToSharesDeposit(SLE::const_ref vault, SLE::const_ref issuance, STAmount const& assets);

[[nodiscard]] STAmount
sharesToAssetsDeposit(SLE::const_ref vault, SLE::const_ref issuance, STAmount const& shares);

[[nodiscard]] STAmount
assetsToSharesWithdraw(
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& assets,
    TruncateShares truncate = TruncateShares::no);

[[nodiscard]] STAmount
sharesToAssetsWithdraw(SLE::const_ref vault, SLE::const_ref issuance, STAmount const& shares);

}  // namespace detail

// High-level API — orchestrates forward+reverse conversions, handles overflow.

struct ExchangeResult
{
    STAmount assets;
    STAmount shares;
};

[[nodiscard]] Expected<ExchangeResult, TER>
computeDeposit(
    Rules const& rules,
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& assets,
    beast::Journal j);

[[nodiscard]] Expected<ExchangeResult, TER>
computeWithdrawByAssets(
    Rules const& rules,
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& assets,
    beast::Journal j);

[[nodiscard]] Expected<ExchangeResult, TER>
computeWithdrawByShares(
    Rules const& rules,
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& shares,
    beast::Journal j);

[[nodiscard]] Expected<ExchangeResult, TER>
computeClawback(
    Rules const& rules,
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& clawbackAmount,
    Number const& assetsAvailable,
    beast::Journal j);

}  // namespace xrpl::vault
