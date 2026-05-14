#pragma once

namespace xrpl {

/** Symbolic names for the three canonical XRPL ledger states.
 *
 *  The XRPL consensus model maintains three distinct ledger states at any
 *  point in time. Rather than requiring callers to pass magic strings
 *  (`"current"`, `"closed"`, `"validated"`) or ad-hoc integer sentinels,
 *  `LedgerShortcut` gives the type system a precise vocabulary for expressing
 *  ledger-selection intent without a specific sequence number or hash.
 *
 *  In `RPCLedgerHelpers.cpp`, `lookupLedger` parsing maps the JSON strings
 *  `"current"`, `"closed"`, and `"validated"` onto the corresponding enum
 *  values before dispatching to the appropriate `getLedger` overload. The
 *  `AccountTx` RPC handler performs the same mapping when processing the
 *  `ledger_index` field. The gRPC adapter maps protobuf shortcut constants to
 *  these values as well.
 *
 *  `LedgerShortcut` also participates as one arm of
 *  `RelationalDatabase::LedgerSpecifier` — a
 *  `std::variant<LedgerRange, LedgerShortcut, LedgerSequence, LedgerHash>` —
 *  allowing symbolic ledger names to flow through the database query layer via
 *  `std::visit` dispatch without special-case handling.
 *
 *  @note The scoped `enum class` form prevents implicit integer conversions and
 *      namespace pollution, both of which are hazards in a codebase that also
 *      works extensively with raw integer ledger sequence numbers.
 */
enum class LedgerShortcut {
    /** The open, in-progress ledger still accumulating new transactions.
     *
     *  This ledger has not been closed or validated, so its contents may
     *  change. Results derived from it are not final and may be rolled back
     *  during a reorganisation or consensus failure.
     */
    Current,

    /** The most recently closed ledger; stable in structure but not yet
     *  consensus-validated.
     *
     *  No new transactions are accepted into this ledger, but the network has
     *  not yet confirmed it as the authoritative chain tip. It is more stable
     *  than `Current` but still not suitable for finality guarantees.
     */
    Closed,

    /** The most recently validated ledger; the fully consensus-confirmed chain
     *  tip.
     *
     *  This is the only state considered immutable and trustworthy for finality
     *  purposes. An RPC node that cannot provide a fresh validated ledger
     *  (i.e., it is stale) will return an error rather than serve potentially
     *  incorrect data.
     */
    Validated
};

}  // namespace xrpl
