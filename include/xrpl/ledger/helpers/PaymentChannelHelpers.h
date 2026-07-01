#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace xrpl {

/** Close a payment channel and return its remaining funds to the channel owner.
 *
 *  @param slep  The SLE for the PayChannel object to close.
 *  @param view  The apply view in which ledger state modifications are made.
 *  @param key   The ledger key identifying the PayChannel entry.
 *  @param j     Journal used for fatal-level diagnostic messages.
 *  @return      tesSUCCESS on success; tefBAD_LEDGER if a directory removal
 *               fails; tefINTERNAL if the source account SLE cannot be found.
 */
TER
closeChannel(SLE::ref slep, ApplyView& view, uint256 const& key, beast::Journal j);

/** Add two uint32_t values with saturation at UINT32_MAX.
 *
 *  @param rules  The current ledger rules used to check amendment status.
 *  @param lhs    Left-hand operand.
 *  @param rhs    Right-hand operand.
 *  @return       @p lhs + @p rhs, saturated at UINT32_MAX when the amendment
 *                is active.
 */
uint32_t
saturatingAdd(Rules const& rules, uint32_t const lhs, uint32_t const rhs);

/** Determine whether a payment channel time field represents an expired time.
 *
 *  @param view       The apply view providing the parent close time and rules.
 *  @param timeField  The optional expiry timestamp (seconds since the XRP
 *                    Ledger epoch).  If empty, the function returns false.
 *  @return           @c true if @p timeField is set and the indicated time is
 *                    in the past relative to the view's parent close time;
 *                    @c false otherwise.
 */
bool
isChannelExpired(ApplyView const& view, std::optional<std::uint32_t> timeField);

}  // namespace xrpl
