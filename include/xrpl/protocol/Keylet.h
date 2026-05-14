#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/LedgerFormats.h>

namespace xrpl {

class STLedgerEntry;

/** Bundles the 256-bit SHAMap locator of a ledger object with its expected
 *  `LedgerEntryType`, making ledger lookups type-safe by construction.
 *
 *  The name is a portmanteau of "key" and "LET" (LedgerEntryType). Callers
 *  never build a `Keylet` by hand — they use one of the factory functions in
 *  the `keylet::` namespace (see `Indexes.h`), each of which encapsulates the
 *  correct SHA-512Half derivation for a specific object type and returns a
 *  `Keylet` already annotated with the matching `LedgerEntryType`.
 *
 *  The ledger view's `read()` method accepts a `Keylet` and calls `check()`
 *  before returning an entry, so an incorrect type annotation surfaces at
 *  the access point rather than silently yielding a mistyped object.
 *
 *  Two sentinel types participate in the matching protocol but are never
 *  stored on-ledger:
 *  - `ltANY` — wildcard; bypasses type checking entirely (used by
 *    `keylet::unchecked`).
 *  - `ltCHILD` — matches any entry that is not a directory node, reflecting
 *    the semantics of owner-directory children.
 *
 *  @see keylet namespace in `Indexes.h` for all factory functions.
 */
struct Keylet
{
    /** 256-bit SHAMap key that locates the ledger entry in the state tree. */
    uint256 key;

    /** Expected `LedgerEntryType` of the entry at `key`.
     *
     *  May be the sentinel `ltANY` (wildcard) or `ltCHILD` (any non-directory
     *  entry); all other values are concrete on-ledger types whose numeric
     *  identities are protocol-stable and consensus-critical.
     */
    LedgerEntryType type;

    /** Constructs a keylet from an explicit type and key.
     *
     *  Prefer the factory functions in the `keylet::` namespace over calling
     *  this constructor directly; they ensure the correct key derivation
     *  formula is used for each ledger object type.
     *
     *  @param type The expected `LedgerEntryType` of the addressed entry.
     *  @param key  The 256-bit SHAMap key of the entry.
     */
    Keylet(LedgerEntryType type, uint256 const& key) : key(key), type(type)
    {
    }

    /** Validates that a deserialized ledger entry corresponds to this keylet.
     *
     *  Applies a three-tier match ordered from most-permissive to most-strict:
     *  - `ltANY`: always returns `true`; the caller bears full responsibility
     *    for type safety.
     *  - `ltCHILD`: returns `true` for any entry whose concrete type is not
     *    `ltDIR_NODE`. Directory nodes are structural bookkeeping objects; a
     *    directory child is definitionally something other than a directory.
     *  - Concrete type: requires both `sle.getType() == type` and
     *    `sle.key() == key`. This is the common case and gives the strongest
     *    safety guarantee.
     *
     *  @param sle The deserialized ledger entry retrieved from the state map.
     *  @return `true` if `sle` legitimately corresponds to this keylet.
     *  @note `sle` must not itself carry `ltANY` or `ltCHILD` as its stored
     *      type; those are query-side sentinels, not real on-ledger types.
     *      An `XRPL_ASSERT` enforces this precondition in debug builds.
     */
    [[nodiscard]] bool
    check(STLedgerEntry const&) const;
};

}  // namespace xrpl
