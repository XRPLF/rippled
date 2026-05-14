/** @file
 *  Protocol-wide constants and validation helpers for the XRP Ledger.
 *
 *  This header is intentionally lightweight: it is included across virtually
 *  the entire codebase, so it avoids heavy dependencies. Everything here is
 *  either a fixed property of the XRP Ledger network (total supply, earliest
 *  known ledger, governance thresholds) or a small convenience function built
 *  directly on those values.
 */

#pragma once

#include <xrpl/basics/chrono.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <string>

namespace xrpl {

/** Return the canonical name of the XRP Ledger daemon.
 *
 *  Uses a Meyers singleton (function-local `static`) to avoid the static
 *  initialization order fiasco. The `inline` specifier allows inclusion in
 *  multiple translation units without ODR violations.
 *
 *  @return The string `"xrpld"`.
 */
static inline std::string const&
systemName()
{
    static std::string const kNAME = "xrpld";
    return kNAME;
}

/** Return the ISO currency code for the native asset.
 *
 *  Uses a Meyers singleton (function-local `static`) for the same reasons as
 *  `systemName()`. Callers should prefer this over scattering `"XRP"` literals
 *  throughout the codebase.
 *
 *  @return The string `"XRP"`.
 */
static inline std::string const&
systemCurrencyCode()
{
    static std::string const kCODE = "XRP";
    return kCODE;
}

/** Total XRP supply at ledger genesis: 100 billion XRP expressed in drops.
 *
 *  Computed as `100'000'000'000 * kDROPS_PER_XRP` (= 10^17 drops).
 *  Two `static_assert`s immediately below guard that the raw bit value is
 *  correct and that `Number::kMAX_REP` can represent it — a compile-time
 *  tripwire if either the XRP total or `Number`'s internal representation
 *  is ever changed.
 */
constexpr XRPAmount kINITIAL_XRP{100'000'000'000 * kDROPS_PER_XRP};
static_assert(kINITIAL_XRP.drops() == 100'000'000'000'000'000);
static_assert(Number::kMAX_REP >= kINITIAL_XRP.drops());

/** Return whether @p amount is within the legal unsigned XRP range.
 *
 *  An amount is legal when it does not exceed the total XRP ever in existence.
 *  Called by `Transactor::preflight1` to reject fee fields that would exceed
 *  `kINITIAL_XRP`, and by `InvariantCheck` as a post-transaction guard.
 *
 *  @param amount The drop amount to validate.
 *  @return `true` if `amount <= kINITIAL_XRP`.
 */
inline bool
isLegalAmount(XRPAmount const& amount)
{
    return amount <= kINITIAL_XRP;
}

/** Return whether @p amount is within the legal signed XRP range.
 *
 *  Extends `isLegalAmount` to accept negative values, which arise in delta
 *  and fee calculations. Used by `InvariantCheck` to ensure no ledger
 *  operation manufactures XRP out of thin air.
 *
 *  @param amount The signed drop amount to validate.
 *  @return `true` if `amount` is in `[-kINITIAL_XRP, kINITIAL_XRP]`.
 */
inline bool
isLegalAmountSigned(XRPAmount const& amount)
{
    return amount >= -kINITIAL_XRP && amount <= kINITIAL_XRP;
}

/** Earliest ledger sequence available on the XRP Ledger mainnet.
 *
 *  Ledgers 1–32569 were lost in an early network incident and no longer exist
 *  anywhere. The `Database` class uses this value as the default lower bound
 *  for the `earliest_seq` configuration parameter, causing any node without
 *  a custom setting to refuse requests for pre-genesis sequences.
 */
static constexpr std::uint32_t kXRP_LEDGER_EARLIEST_SEQ{32570u};

/** Earliest mainnet ledger sequence that contains a `FeeSettings` object.
 *
 *  Used exclusively in `XRPL_ASSERT` calls and tests in the form:
 *  @code
 *  XRPL_ASSERT(
 *      ledger->header().seq < kXRP_LEDGER_EARLIEST_FEES ||
 *          ledger->read(keylet::fees()),
 *      "...");
 *  @endcode
 *  This allows the `FeeSettings` invariant to be checked on modern ledgers
 *  while skipping it for historical replay of early mainnet ledgers where
 *  the object did not yet exist.
 */
static constexpr std::uint32_t kXRP_LEDGER_EARLIEST_FEES{562177u};

/** Required validator support for an amendment to achieve majority.
 *
 *  Represents 80% as a `std::ratio` rather than a floating-point constant
 *  so that `AmendmentTable` can compute the threshold count with pure integer
 *  arithmetic (`(trustedValidations * num) / den`), avoiding rounding error
 *  in this consensus-critical gate.
 */
constexpr std::ratio<80, 100> kAMENDMENT_MAJORITY_CALC_THRESHOLD;

/** Minimum continuous duration that an amendment must hold 80% validator
 *  support before it activates on mainnet.
 *
 *  Seeds `Config::AMENDMENT_MAJORITY_TIME`, which operators may override.
 *  Uses the `weeks` alias from `xrpl/basics/chrono.h` (predates C++20
 *  `std::chrono::weeks`).
 */
constexpr std::chrono::seconds const kDEFAULT_AMENDMENT_MAJORITY_TIME = weeks{2};

}  // namespace xrpl

/** IANA-registered port for XRP Ledger peer-to-peer connections.
 *
 *  Declared outside the `xrpl` namespace so that networking code constructing
 *  socket addresses can reference it without namespace qualification.
 *  Used by `OverlayImpl` peer discovery and the `peer_connect` RPC handler
 *  as the fallback when no explicit port is configured.
 */
inline std::uint16_t constexpr kDEFAULT_PEER_PORT{2459};
