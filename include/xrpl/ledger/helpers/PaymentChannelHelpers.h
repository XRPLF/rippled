#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace xrpl {

TER
closeChannel(
    std::shared_ptr<SLE> const& slep,
    ApplyView& view,
    uint256 const& key,
    beast::Journal j);

bool
isChannelExpired(ApplyView const& view, std::optional<std::uint32_t> timeField);

}  // namespace xrpl
