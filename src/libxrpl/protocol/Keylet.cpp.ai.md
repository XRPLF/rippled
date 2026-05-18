# `Keylet.cpp` — Ledger Entry Type and Key Validation

`Keylet.cpp` provides the sole out-of-line method for the `Keylet` struct, which is the fundamental handle used throughout the XRPL codebase to locate and type-check objects in the ledger's state map. The `Keylet` struct itself (defined in `include/xrpl/protocol/Keylet.h`) is deliberately minimal: it carries exactly two fields, a `uint256 key` (the SHAMap hash used to locate the object) and a `LedgerEntryType type` (the expected on-ledger type). The portmanteau name "Keylet" fuses "key" with "LET" (LedgerEntryType), making the dual purpose self-documenting.

## The `check()` Method

`Keylet::check(STLedgerEntry const& sle)` answers a single question: *does this ledger entry legitimately correspond to this keylet?* It is the enforcement point that keeps callers from accidentally retrieving an entry of the wrong type after a successful SHAMap lookup.

The logic is a deliberate three-tier match, ordered from most-permissive to most-strict:

1. **`ltANY` wildcard.** When `type == ltANY`, the keylet was constructed without caring about the entry's concrete type — the `keylet::unchecked` family uses this. `check()` returns `true` unconditionally, placing the burden of correctness on the caller.

2. **`ltCHILD` pseudo-type.** When `type == ltCHILD`, the keylet represents a "child" of a directory structure. The only constraint is that the retrieved entry must *not* itself be a `ltDIR_NODE`. Directory nodes are structural bookkeeping objects that hold sorted lists of other entries; a child of a directory is definitionally something other than a directory node. Any other concrete type is accepted without checking the key.

3. **Exact match.** For all concrete types, both `sle.getType() == type` and `sle.key() == key` must hold simultaneously. This is the normal case for typed lookups — it verifies that the SHAMap returned the exact object that was expected, at the exact slot, with the correct declared type embedded in the serialized entry itself.

## Defensive Assertion

An `XRPL_ASSERT` at the top of `check()` enforces that the *incoming* `STLedgerEntry` never carries `ltANY` or `ltCHILD` as its own type. These are pseudo-types that exist only for keylet construction and filtering; no real on-ledger serialized object may declare itself as one. Calling `check()` with such an entry would indicate a bug elsewhere in the pipeline — a malformed SLE read from the state map — and the assertion turns it into an immediate, diagnosable failure rather than a silent mis-classification.

## Architectural Role

The two-field design of `Keylet` is intentional: it keeps type information co-located with the lookup key, eliminating a class of bugs where a caller uses the correct hash but forgets to verify the type of the returned entry. The `check()` method is the runtime guardian of that invariant, and the pseudo-types `ltANY` and `ltCHILD` give callers a well-typed escape hatch for the minority of cases where some ambiguity is genuinely necessary.