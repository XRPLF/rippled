#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstdint>
#include <optional>

namespace xrpl {

/**
 * ceil(value * 10^scale), exact.
 *
 * The shift is a pure exponent adjustment; the +1 for a fractional
 * positive value is exact because a fractional Number is < 10^16.
 * Truncation toward zero already is the ceiling for negative values.
 */
[[nodiscard]] Number
tokenScaledCeil(Number const& value, std::uint8_t scale);

/**
 * floor(value * 10^scale) as integer base units; nullopt if it does not
 * fit in a non-negative int64.
 */
[[nodiscard]] std::optional<std::int64_t>
tokenBaseUnits(Number const& value, std::uint8_t scale);

/**
 * True if the issuance violates
 * ceil(IssuedAmount * 10^TokenScale) + MPT OutstandingAmount > MaximumAmount.
 * Always false for an uncapped issuance.
 */
[[nodiscard]] bool
tokenSupplyExceeded(ReadView const& view, SLE::const_ref sleIssuance);

/**
 * Controls whether an IOU credit hard-fails when a capped TokenIssuance
 * would exceed its MaximumAmount. Flow-engine steps pass No: their send
 * results are advisory (return values ignored, reverse-pass execution can
 * legitimately overshoot transiently) and the supply-cap invariant checker
 * gates the final state instead.
 */
enum class EnforceSupplyCap : bool { No = false, Yes = true };

/**
 * Maintain IssuedAmount on the issuer's TokenIssuance for a trust-line
 * balance move of `amount` from `sender` to `receiver`. Only the amount's
 * issuer is adjusted: balance moving away from the issuer increases its
 * net issuance, balance returning decreases it.
 *
 * No-op when the amendment is disabled, neither party is the issuer, or no
 * TokenIssuance exists. Returns tecSUPPLY_EXCEEDED when the cap is enforced
 * and a capped issuer would exceed MaximumAmount.
 */
[[nodiscard]] TER
adjustTokenIssuance(
    ApplyView& view,
    AccountID const& sender,
    AccountID const& receiver,
    STAmount const& amount,
    EnforceSupplyCap enforceCap,
    beast::Journal j);

/**
 * Remaining IOU the issuer can issue under the supply cap, floored to the
 * representable amount; nullopt when there is no TokenIssuance or no cap.
 */
[[nodiscard]] std::optional<STAmount>
tokenIssuanceHeadroom(ReadView const& view, Issue const& issue);

/**
 * True when the issue's TokenIssuance carries the per-currency lock
 * (lsfTokenLocked). The per-currency analog of lsfGlobalFreeze.
 */
[[nodiscard]] bool
isTokenLocked(ReadView const& view, Issue const& issue);

/**
 * For an MPT issuance bound to a capped TokenIssuance: the additional MPT
 * base units that can be minted under the shared cap
 * (MaximumAmount - OutstandingAmount - ceil(IssuedAmount * 10^TokenScale)).
 * nullopt when unbound or uncapped.
 */
[[nodiscard]] std::optional<std::int64_t>
mptBoundHeadroom(ReadView const& view, SLE::const_ref sleMptIssuance);

/**
 * Validate binding an MPTokenIssuance to a TokenIssuance: the MPT issuance
 * exists, is issued by `account`, is not already bound, and its
 * MaximumAmount/AssetScale equal the TokenIssuance's MaximumAmount/
 * TokenScale.
 */
[[nodiscard]] TER
validateTokenBinding(
    ReadView const& view,
    MPTID const& mptId,
    AccountID const& account,
    std::optional<std::uint64_t> const& maximumAmount,
    std::uint8_t tokenScale);

}  // namespace xrpl
