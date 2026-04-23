#pragma once

#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/UintTypes.h>

namespace xrpl {

TER
closeChannel(
    std::shared_ptr<SLE> const& slep,
    ApplyView& view,
    uint256 const& key,
    beast::Journal j);

}  // namespace xrpl
