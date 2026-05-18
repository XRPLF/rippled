# RCLValidations.h — XRP Ledger Consensus Validation Adaptor Layer

This file is the glue between two separate abstraction levels in rippled's consensus architecture: the protocol-level `STValidation` serialized object and the generic, policy-parameterized `Validations<>` template engine. Rather than coupling the generic template directly to XRPL types, the design uses a classic adaptor pattern: three lightweight types defined here satisfy the concept interface expected by the template, and a `using` alias collapses the whole thing into `RCLValidations`.

## The Adapter Pattern in Context

The generic `Validations<Adaptor>` template in `src/xrpld/consensus/Validations.h` is deliberately XRPL-agnostic. It accepts any adaptor that provides `Mutex`, `Validation`, and `Ledger` type members plus a handful of methods. This allows simulation and testing code to swap in lightweight fakes. The three classes in this file are the production concrete types for the XRP Ledger Consensus (RCL) layer.

## `RCLValidation` — Protocol Message Wrapper

`RCLValidation` wraps a `std::shared_ptr<STValidation>` and exposes the uniform interface that `Validations<>` expects. `STValidation` is a full serialized protocol object — it understands signing, serialization, and field access via the STObject machinery. `RCLValidation` hides all of that behind simple accessors: `ledgerID()`, `seq()`, `signTime()`, `seenTime()`, `key()`, `nodeID()`, `trusted()`, `full()`, and `loadFee()`.

The distinction between `key()` (the ephemeral signing key) and `nodeID()` (derived from the validator's master key via the manifest system) is significant for validators that rotate their signing keys. The generic validation code uses `NodeID` to track the same logical validator across key rotations.

`loadFee()` returning `std::optional<uint32_t>` signals that not every validation carries a fee vote — partial validations in particular may omit it. The `cookie()` accessor provides a per-restart entropy value used to detect duplicate validations from a restarted validator without requiring persistent state.

## `RCLValidatedLedger` — Ancestor-Aware Ledger View

`RCLValidatedLedger` wraps an immutable `Ledger` for use in the `LedgerTrie`. The trie models ledger history as a sequence-to-hash mapping: any two ledgers sharing the same hash at a given sequence share the same complete ancestry up to that point. The trie uses this to determine which ledger branch the network is converging on.

The XRPL ledger structure stores up to 256 prior ancestor hashes in the skip list (`keylet::skip()`, field `sfHashes`). `RCLValidatedLedger` copies these into a `std::vector<uint256>` at construction time, enabling the `operator[](Seq)` lookup. When the requested ancestor falls outside the `[minSeq(), seq()]` window — meaning it's more than 256 ledgers back — the operator returns `ID{0}`, which the trie treats as a distinct leaf, effectively forcing the two ledgers to be considered unrelated. This is a deliberate conservative choice: the code cannot prove a common ancestor, so it assumes divergence.

The `MakeGenesis` constructor tag creates a null ledger at sequence 0, used as the universal root of the trie where all chains eventually converge.

The `mismatch()` free function (declared as a friend to make the symmetry between the two operands clear) walks the overlapping sequence interval backward until it finds the first matching ancestor hash. If the entire overlapping interval mismatches, it returns sequence 1 — the earliest possible divergence point beyond the genesis ledger. This is used by the trie to insert ledgers at the correct branch point.

## `RCLValidationsAdaptor` — XRPL-Specific Policy

`RCLValidationsAdaptor` satisfies the adaptor concept for `Validations<>` by providing the `now()` time source (from `Application::getTimeKeeper().closeTime()`) and the `acquire()` method. These are the only two points where XRPL-specific infrastructure bleeds into the otherwise generic validation machinery.

`acquire()` first checks whether the `LedgerMaster` already has the ledger cached. If it does, it returns an `RCLValidatedLedger` immediately. If it doesn't, it posts a `jtADVANCE` job to the `JobQueue` that calls `InboundLedgers::acquireAsync()` — a non-blocking peer fetch. Critically, `acquire()` returns `std::nullopt` in the miss case rather than blocking. The generic `Validations<>` template handles missing ledgers gracefully by deferring trie insertions until the ledger becomes available.

The performance instrumentation on `getLedgerByHash` (via `perf::measureDurationAndLog` with a 10ms threshold) reflects the operational concern that slow ledger lookups during validation processing could stall consensus; slow calls are logged for operational visibility.

## `handleNewValidation()` — Entry Point and Byzantine Detection

`handleNewValidation()` is the single function called by the network layer when a new validation arrives. It performs three distinct jobs.

First, it determines trust status by consulting the `ValidatorList`. If the signing key maps to a currently trusted master key, the validation is marked trusted before being added. If the key isn't trusted but is listed (known but not currently on the UNL), the master key is still resolved for the purpose of identity continuity — ensuring the trie correctly attributes validations across key rotations.

Second, it calls `validations.add()` using the resolved `NodeID`, which either admits the validation as current or rejects it with a `ValStatus` indicating why. Only `ValStatus::current` validations from trusted validators proceed to `LedgerMaster::checkAccept()`, which re-evaluates whether the node can advance its validated ledger given the new vote tally.

Third, and architecturally notable, validations with `ValStatus::conflicting` or `ValStatus::multiple` are logged via the "Byzantine Behavior Detector" path — error-level for trusted validators, info-level for untrusted — but are **not suppressed from relay**. The comment in the source explains the reasoning: the node should forward these anomalous validations precisely because peers need to independently observe the same misbehavior to alert their operators. Silencing them would undermine the distributed detection mechanism.

The `BypassAccept` parameter allows callers to skip the `checkAccept` call on current trusted validations. This is used in scenarios like historical replay, where advancing the validated ledger would be incorrect even though the validation itself is valid.

## Type Alias

```cpp
using RCLValidations = Validations<RCLValidationsAdaptor>;
```

This single line is the payoff of the entire file. All of the `Validations<>` template machinery — the `LedgerTrie`, the `SeqEnforcer` per-validator monotonicity check, the aged expiry containers, the trust accounting — is instantiated against these three concrete XRPL types. The rest of the application uses `RCLValidations` directly through `Application::getValidations()`.