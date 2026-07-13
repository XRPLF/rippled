#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/UintTypes.h>

namespace xrpl::permissioned_dex {

// Check if an account is in a permissioned domain
[[nodiscard]] bool
accountInDomain(ReadView const& view, AccountID const& account, Domain const& domainID);

// Check if an offer is in the permissioned domain
[[nodiscard]] bool
offerInDomain(
    ReadView const& view,
    uint256 const& offerID,
    Domain const& domainID,
    beast::Journal j);

}  // namespace xrpl::permissioned_dex
