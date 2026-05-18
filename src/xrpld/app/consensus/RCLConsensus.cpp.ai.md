# `RCLConsensus.cpp` — XRPL Consensus Engine Implementation

## Role and Context

`RCLConsensus.cpp` is the implementation file for the XRP Ledger's consensus engine integration layer. It sits at the junction between the protocol-agnostic `Consensus<Adaptor>` template (defined in `xrpld/consensus/`) and all of the concrete XRPL infrastructure — ledger management, transaction sets, overlay networking, fee voting, amendment tables, and validation broadcasting. The file defines two closely related classes: `RCLConsensus` (the externally-facing wrapper) and its inner `RCLConsensus::Adaptor` (which implements the template interface the generic engine requires), plus the helper `RclConsensusLogger`.

## The Adaptor Pattern

The generic `Consensus<Adaptor>` template is algorithm-only; it delegates all protocol interactions through the `Adaptor` type via static dispatch. `RCLConsensus::Adaptor` is that concrete adaptor for the Ripple Consensus Ledger. Rather than exposing the adaptor directly, `RCLConsensus` owns both `adaptor_` and `consensus_` as member fields and provides a mutex-guarded public API that routes every inbound event into the engine under `mutex_`. This separation is intentional: the generic `Consensus` object is single-threaded and stateful, so all entries from external threads (timer, peer proposals, new tx sets) acquire `mutex_` first, while the adaptor callbacks invoked from _within_ the engine's methods already hold that lock implicitly by contract.

The adaptor's observable state — `validating_`, `prevProposers_`, `prevRoundTime_`, `mode_` — is declared `std::atomic` so callers can query them without holding the consensus lock.

## Construction and Identity

The `Adaptor` constructor randomly generates `valCookie_`, a nonzero 64-bit value used to tag every validation this node emits. Tagging validations with a session cookie lets peers detect stale messages from a previous server instance that has since restarted. The value is computed as `1 + rand_int(prng, max-1)`, guaranteeing it is never zero, and this is defended by `XRPL_ASSERT`. If the node holds validator keys, the constructor logs the master public key and, when key rotation is active (master key ≠ ephemeral signing key), also logs the ephemeral key with its rotation sequence number.

## Ledger Acquisition

`acquireLedger` implements a "lazy single-trigger" pattern. If the required ledger isn't already available in `LedgerMaster`, it records the target hash in `acquiringLedger_` to prevent duplicate jobs, then submits a `jtADVANCE` job to `InboundLedgers::acquireAsync`. On the next timer tick, if the ledger has arrived, the flow continues; otherwise the engine remains blocked. Once a ledger is found, two assertions verify it is closed (not `open()`) and immutable before wrapping it in `RCLCxLedger`.

## Ledger Close — `onClose`

When the current ledger closes, `onClose` builds the initial transaction set from the open ledger's snapshot. Critically, pseudo-transactions are injected at this stage, not by generic consensus: fee-vote and amendment-vote pseudo-txs are added when the previous ledger was a flag ledger (only if proposing and synced), and negative-UNL vote pseudo-txs are added when the previous ledger was a voting ledger. After snapshotting the `SHAMap`, if the node has correct LCL, all proposed transaction IDs and their sequence numbers are registered with `censorshipDetector_` for later tracking.

## Accept Path — `onAccept` vs `onForceAccept`

When consensus reaches agreement, `onAccept` **defers** the expensive work: it schedules a `jtACCEPT` job on the application's job queue so the consensus engine's timer thread is not held. The comment in the code explains why this is safe: the generic `Consensus` engine guarantees that the `result` reference and the fields captured from `prevLedger` will remain valid and unchanged until `startRound` is called (which only happens from `endConsensus`, triggered at the end of the accept job). `onForceAccept` is the bypass path for simulations and ledger-skip scenarios; it calls `doAccept` directly without scheduling.

## The Heart — `doAccept`

`doAccept` is where a consensus result becomes a committed ledger. It proceeds through several phases:

**Transaction deserialization**: The consensus tx set (`SHAMap`) is walked to build a `CanonicalTXSet`, which sorts transactions by the hash of their containing set — giving deterministic but unpredictable ordering across validators. Any transaction that fails deserialization is recorded in `failed` and excluded.

**Ledger construction**: `buildLCL` is called, which either replays a captured ledger (for testing/catch-up) or calls `buildLedger` to apply the canonical tx set on top of the previous ledger. `buildLCL` also calls `TxQ::processClosedLedger` to update fee escalation state based on whether the round was long (>5 seconds).

**Censorship detection**: After acceptance, `censorshipDetector_.check` compares what was accepted against what was proposed in `onClose`. Any transaction that has been proposed but not accepted for `censorshipWarnInternal` (15) ledger intervals triggers a warning log. Transactions that actually failed application are exempted from warnings.

**Validation**: If the node is an active validator and consensus did not fail (`ConsensusState::Yes`), `validate` constructs an `STValidation` signed with the ephemeral key. The validation embeds: the ledger hash, the consensus hash (the agreed tx set ID), the ledger sequence, the `valCookie_`, the most recent fully-validated ledger hash, server version (on voting ledgers), load fee, fee vote preferences, and amendment votes. The validation is first added to the local validation store via `handleNewValidation`, then broadcast over the overlay, then published to RPC subscribers.

**Open ledger transition**: Disputed transactions that this node voted NO on (and that are not pseudo-txs) are re-inserted into `retriableTxs` so they get a second chance in the next open ledger. Then `OpenLedger::accept` advances the open ledger under a double-lock on `masterMutex` and `LedgerMaster::peekMutex`. After that, `LedgerMaster::switchLCL` finalizes the closed ledger.

**Clock adjustment**: If the round ended without consensus failure, the node computes a weighted average of peers' close-time votes to estimate its own clock offset and calls `TimeKeeper::adjustCloseTime`. This is the mechanism by which the distributed network converges on a consistent `NetClock`.

## Pre-round Setup — `preStartRound`

Before each round, the adaptor recalculates `validating_`. The guard conditions are: validator keys must be configured, the ledger sequence must be at or past the `maxDisallowedLedger` threshold (protecting against signing stale validations after a restart), the node must not be amendment-blocked, and in live network mode the validator list must not have expired. If any condition fails, the node silently drops to observer mode. The function also notifies `NegativeUNLVote` about any newly trusted validators. It returns `true` (entering as proposer) only when `validating_ && synced`.

## Censorship Detector Reset

`onModeChange` resets `censorshipDetector_` whenever consensus mode transitions away from `proposing` or `observing`. This prevents false-positive censorship warnings that would otherwise fire when the node loses sync and starts operating on a divergent view of the ledger.

## `RclConsensusLogger`

A lightweight RAII timer. It only allocates a `stringstream` when the node is validating or journal info is enabled; otherwise construction is effectively free. On destruction it writes the elapsed wall-clock duration plus any accumulated diagnostic text using `writeAlways` — bypassing the journal's normal severity filter to ensure acceptance timing always appears in logs for operator diagnostics.

## Key Invariants and Failure Modes

The code assumes the generic `Consensus` engine's single-threaded contract: callbacks fire with `mutex_` logically held (the lock is owned by the public entry methods), so `doAccept` and other adaptor callbacks never need to re-acquire it. Violating this contract by calling adaptor methods from outside would cause data races on the internal consensus state. The ledger acquired in `acquireLedger` is asserted to be both immutable and closed before use — an open or mutable ledger reaching this path indicates a serious internal inconsistency. Validation signing silently no-ops (`return` after a warning log) if `validatorKeys_.keys` is null, rather than throwing, to allow non-validator nodes to participate in consensus rounds as observers without special-casing at the call sites.