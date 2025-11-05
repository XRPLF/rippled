#pragma once
#include <xrpl/ledger/View.h>

namespace ripple {
namespace permissioned_dex {

// Check if an account is in a permissioned domain
[[nodiscard]] bool
accountInDomain(
    ReadView const& view,
    AccountID const& account,
    Domain const& domainID);

// Check if an offer is in the permissioned domain
[[nodiscard]] bool
offerInDomain(
    ReadView const& view,
    uint256 const& offerID,
    Domain const& domainID,
    beast::Journal j);

}  // namespace permissioned_dex

}  // namespace ripple
