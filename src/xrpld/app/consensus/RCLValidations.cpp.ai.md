# RCLValidations.cpp

This file provides the XRPL-specific (RCL — Ripple Consensus Ledger) concrete implementations that wire the generic, protocol-agnostic `Validations<>` template into the running `xrpld` application. It defines three interlocking components: a ledger ancestry abstraction (`RCLValidatedLedger`), the adaptor bridging generic validation machinery to XRPL's application layer (`RCLValidationsAdaptor`), and the central intake function for incoming validations (`handleNewValidation`).

## RCLValidatedLedger: Ancestry Without Loading History

The generic `LedgerTrie` used by `Validations<>` needs to answer one question for any two candidate ledgers: do they share a common ancestor, and if so, at what sequence did they diverge? `RCLValidatedLedger` answers this by exploiting the skip list embedded in every closed XRPL ledger — the `sfHashes` field under `keylet::skip()`, which stores up to 256 recent ancestor hashes in sequence order.

On construction from a real `Ledger`, `RCLValidatedLedger` reads this skip list and stashes it in `ancestors_`. The `operator[](Seq s)` method then provides O(1) lookup of any ancestor hash within the available window (`minSeq()` to `seq()`). If the requested sequence falls outside the window — meaning more than 256 ledgers separate the query point from the known history — the method returns `ID{0}`, a sentinel that the trie interprets as "unknown/distinct." This is a deliberate design choice: rather than attempting expensive ledger fetches just to determine ancestry, the system conservatively treats widely separated ledgers as unrelated.

The `MakeGenesis` constructor tag produces a zero-sequence, zero-hash genesis placeholder used to root the trie without any real ledger data.

The `mismatch()` free function (a required interface for the generic trie) finds the earliest sequence at which two ledger chains diverge. It computes the overlap window between the two ledgers' known ancestor ranges, then walks backward from the overlap ceiling until it finds a sequence where both report the same hash. If the entire window disagrees, it falls back to `Seq{1}` — one after genesis — reflecting the worst-case assumption that the chains forked as early as possible.

## RCLValidationsAdaptor: Bridging Generic and Concrete

`RCLValidationsAdaptor` satisfies the adaptor concept required by `Validations<Adaptor>`. It specifies `std::mutex` for thread safety, `RCLValidation` as the validation type, and `RCLValidatedLedger` as the ledger type. `RCLValidations` is then just a type alias for `Validations<RCLValidationsAdaptor>`.

The `acquire()` method handles the case where the generic validation machinery needs to actually load a ledger (e.g., when building the trie after receiving validations for a ledger the node doesn't have locally). It first tries `LedgerMaster::getLedgerByHash()` wrapped in a `perf::measureDurationAndLog` call to surface unexpectedly slow lookups. If the ledger isn't found, rather than blocking or failing, `acquire()` fires an async job via `JobQueue` with type `jtADVANCE` that calls `InboundLedgers::acquireAsync()` — the normal mechanism for fetching missing ledgers from peers. The function returns `std::nullopt` immediately, and the validation machinery will retry the lookup when the ledger eventually arrives. This design keeps the critical consensus path non-blocking: the adaptor is a thin shim, and all real I/O is deferred to the job queue.

The `now()` method delegates to `TimeKeeper::closeTime()`, returning the network-adjusted clock time used to determine whether validations are current or stale.

## handleNewValidation: Trust Resolution and Intake

`handleNewValidation` is the single entry point for all validations, whether locally produced or received from peers. It performs three distinct operations in sequence.

**Trust resolution.** The validation arrives carrying only a signing key. The function queries `ValidatorList::getTrustedKey()` to see whether that signing key maps to a trusted master key. If so, it marks the `STValidation` as trusted. This step exists because validators may rotate their ephemeral signing keys; what matters for trust is the master identity, not the ephemeral key used to sign a particular message. If the signing key isn't trusted, the function also checks `getListedKey()` — unlisted validators still get tracked in the `Validations<>` state, which is important for monitoring. The `calcNodeID(masterKey.value_or(signingKey))` call normalizes both cases into a stable `NodeID` before passing the validation to `Validations<>::add()`.

**Acceptance triggering.** When `add()` returns `ValStatus::current` and the validation is trusted, the function calls `LedgerMaster::checkAccept()` with the validated ledger's hash and sequence. This is the mechanism by which accumulating trusted validations can push the node to declare a ledger fully validated. The `BypassAccept::yes` path skips this call and is used when replaying validations during startup catch-up to avoid spurious or redundant accept checks.

**Byzantine behavior logging.** Validations that fail with `ValStatus::conflicting` (same validator, same sequence, different ledger hash — or different sign times) or `ValStatus::multiple` (same validator, same sequence and hash, but different cookies) are logged with a "Byzantine Behavior Detector" prefix. Trusted-validator violations are logged at ERROR; unlisted-validator violations at INFO. The code explicitly chooses *not* to suppress forwarding of these problematic validations. As the comment notes, this seems counterintuitive but is intentional: peers need to independently observe Byzantine validators so their operators can react. Suppressing the evidence would undermine the network's ability to detect and respond to misbehavior.

## Invariants and Error Handling

The `XRPL_ASSERT` in the `RCLValidatedLedger` constructor checks that `sfLastLedgerSequence` in the skip list equals `seq() - 1`, catching any corruption in the ledger's own metadata. The assert in `RCLValidationsAdaptor::acquire()` verifies that a successfully retrieved ledger is both closed (not `open()`) and immutable — a double check against receiving a mutable working ledger by mistake. Out-of-range ancestor lookups in `operator[]` degrade gracefully with a warning log and return `ID{0}`, never accessing the `ancestors_` vector out of bounds.