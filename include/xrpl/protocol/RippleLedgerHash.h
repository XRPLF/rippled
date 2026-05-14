/** @file
 *  Defines the `LedgerHash` type alias used throughout the ledger stack to
 *  identify closed ledgers by their cryptographic digest.
 */

#pragma once

#include <xrpl/basics/base_uint.h>

namespace xrpl {

/** The SHA-512/256 digest that uniquely identifies a closed XRP Ledger.
 *
 *  A ledger hash is computed over the serialized `LedgerHeader` — covering the
 *  account-state hash, transaction-set hash, sequence number, close time, drop
 *  totals, and parent ledger hash — and is 32 bytes wide.
 *
 *  The alias over bare `uint256` serves two purposes: it makes interfaces
 *  self-documenting at call sites (e.g., `CanonicalTXSet(LedgerHash const&)`),
 *  and it isolates all ledger-hash usage behind a single name so that a
 *  tagged variant (`base_uint<256, struct LedgerHashTag>`) can be introduced
 *  later to prevent cross-domain substitution with transaction hashes or
 *  account IDs without touching every call site.
 *
 *  @note `LedgerHeader` stores its hash fields as bare `uint256` for historical
 *      reasons; higher-level APIs (`CanonicalTXSet`, `LedgerHistory`,
 *      `InboundLedgers`, `RCLValidations`) consistently use this alias.
 */
using LedgerHash = uint256;

}  // namespace xrpl
