#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

namespace xrpl {

TER
closeChannel(SLE::ref slep, ApplyView& view, uint256 const& key, beast::Journal j);

}  // namespace xrpl
