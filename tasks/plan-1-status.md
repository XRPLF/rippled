# Plan 1 (parallel transaction apply) — implementation status

Branch: `feat/parallel-apply-access-set`. This file tracks what is implemented,
verified, and what remains, so any engineer can continue without re-deriving.

## Implemented & verified

### Phase 1 foundation (commit "static access-set extraction + DEBUG assertion gate")
- `include/xrpl/tx/AccessSet.h` — the declared per-tx footprint (categorised key
  sets + `touchesGlobal`), `keys()`, `conflictsWith()`.
- `Transactor::accessSetOf(STTx, ReadView)` — name-hidden dispatch hook, base
  default `touchesGlobal=true` (fail-safe). Free `accessSetOf` dispatcher in
  `applySteps.cpp`. `Transactor::commonAccountFootprint` helper.
- DEBUG touched-key instrumentation in `detail::ApplyStateTable` (the single
  chokepoint for read/exists/peek/insert/update/erase), exposed up through
  `ApplyViewBase`/`ApplyContext`.
- DEBUG subset assertion + `XRPL_ACCESS_AUDIT` footprint dump in
  `Transactor::operator()` (success path only).
- Tests: `src/tests/libxrpl/tx/AccessSet.cpp` (18). Verified: full
  `xrpl.test.protocol_autogen` (495 tests) passes with the assertion live.

### Phase 2 scheduler (commit "access-set scheduler ... independent groups")
- `include/xrpl/tx/Schedule.h` + `src/libxrpl/tx/Schedule.cpp` — `scheduleApply`
  partitions a canonical tx set into independent `ConflictGroup`s via union-find
  over an inverted key→tx index. Any `touchesGlobal` tx ⇒ conservative
  fully-serial fallback (flag-ledger handling).
- Tests: `src/tests/libxrpl/tx/Schedule.cpp` (5), incl. the core invariant
  **GroupsArePairwiseIndependent** (txns in different groups never conflict).

### Migrated transactors (13) — `accessSetOf` declared, assertion-verified
AccountSet, SetRegularKey, DepositPreauth, SignerListSet, TicketCreate, TrustSet,
DIDSet, DIDDelete, Payment (XRP→XRP only), OracleSet, OracleDelete, DelegateSet,
PermissionedDomainDelete.

## Load-bearing design rules (apply to every future migration)
1. **Directory exclusion + owner declaration.** The assertion excludes
   `ltDIR_NODE` entries (owner-dir pages are derived bookkeeping). This is sound
   ONLY if, for every owner directory a transactor modifies, that owner's
   `keylet::account` is declared. So: whenever `doApply` does `dirInsert`/
   `dirRemove` on `ownerDir(X)`, declare `account(X)` — even if X's root isn't
   otherwise written (e.g. CredentialCreate touches the subject's owner dir).
2. **Shared (non-owner) directories ⇒ global.** Book directories and NFT
   buy/sell directories (`nftSells`/`nftBuys`, keyed by NFTokenID) are shared
   across accounts; they are genuine cross-account conflict surfaces with no
   single owning account, so any transactor touching them stays `touchesGlobal`.
3. **`succ()`/range scans ⇒ not statically declarable.** A footprint discovered
   by walking a chain (NFT page chains) is not a static superset ⇒ global.
4. **Snapshot reads are allowed.** `accessSetOf(tx, base)` may read `base` to
   resolve a field stored inside an object SLE (e.g. an escrow's Destination).
5. The DEBUG subset assertion is the gate: migrate, run `xrpl.test.protocol_autogen`
   in Debug; any under-declaration aborts the relevant `*Tests` suite.

## Remaining Phase-1 migrations — turnkey (footprints analysed, verdicts fixed)

STATIC (derivable from tx body):
- CredentialCreate → `credential(sfSubject, src, sfCredentialType)` + `account(sfSubject)`
- MPTokenIssuanceCreate → `mptIssuance(tx.getSeqValue(), src)`
- CheckCreate → `check(src, tx.getSeqValue())` + `account(sfDestination)`
- PaymentChannelCreate → `payChan(src, sfDestination, tx.getSeqValue())` + `account(sfDestination)`
- Clawback → `account(src)` + `account(sfHolder)` + (IOU: `line(holder, issuer, ccy)`;
  MPT: `mptIssuance(id)` + `mptoken(id, holder)`)
- EscrowCreate (XRP) → `escrow(src, tx.getSeqValue())` + `account(sfDestination)`
  (IOU/MPT variant: + issuer account + `line(src,issuer)` / mpt objects, or keep global)

STATIC_WITH_SNAPSHOT (read the object SLE in `base` to resolve owners):
- CredentialAccept → `credential(src, sfIssuer, type)` + `account(sfIssuer)`
- CredentialDelete → `credential(subject|src, issuer|src, type)` + `account(issuer)` + `account(subject)`
- MPTokenIssuanceDestroy → `mptIssuance(id)` + `account(issuer-from-SLE)`
- MPTokenIssuanceSet → `mptIssuance(id)` or `mptoken(id, sfHolder)` (+ `permissionedDomain(sfDomainID)`)
- MPTokenAuthorize → `mptIssuance(id)` + `account(holder)` + `mptoken(id, holder)` (holder = src or sfHolder)
- CheckCancel → `check(sfCheckID)` + `account(check.Account)` + `account(check.Destination)`
- PaymentChannelFund → `payChan(sfChannel)` + `account(chan.Account)` + `account(chan.Destination)`
- PaymentChannelClaim → `payChan(sfChannel)` + `account(chan.Account)` + `account(chan.Destination)`
  + `depositPreauth(chan.Destination, src)` + each `sfCredentialIDs` key
- EscrowFinish (XRP; else global) → `escrow(sfOwner, sfOfferSequence)` + escrow.Account + escrow.Destination
  + `depositPreauth(dst, src)` + each `sfCredentialIDs` key
- EscrowCancel (XRP; else global) → `escrow(sfOwner, sfOfferSequence)` + escrow.Account + escrow.Destination

## DYNAMIC — must stay `touchesGlobal` in v1 (reason)
- OfferCreate, OfferCancel — shared book directory + offer crossing modifies
  counterparty accounts not in the tx.
- All AMM* (Create/Deposit/Withdraw/Vote/Bid/Delete/Clawback) — pool
  pseudo-account + crossing.
- Payment with paths / cross-currency / IOU / MPT, CheckCash — flow engine
  (unbounded trustline/offer traversal).
- All NFToken* — NFT page-chain `succ()` search (Mint/Burn/Modify/AcceptOffer)
  and shared NFT offer directories (CreateOffer/CancelOffer).
- AccountDelete — deletes every src-owned object (unbounded footprint).
- Batch — meta-transaction; footprint is the union of its inner txs.
- Vault*, LoanBroker*/Loan* (lending), XChain*/bridge — pseudo-accounts and
  cross-chain/compound state; unaudited dynamic footprints.
- PermissionedDomainSet — references arbitrary credential-issuer accounts in
  `sfAcceptedCredentials`; keep global pending a deeper audit.
- Change (SetFee/EnableAmendment/UNLModify), LedgerStateFix — pseudo/global.

## Phase 3 core — BUILT & verified
- `applyScheduled` (`Schedule.h`/`.cpp`): schedules a tx set, applies each
  independent group in an isolated `OpenView` over the immutable closed
  snapshot, and merges the disjoint write-sets into the target ledger.
- `src/tests/libxrpl/tx/ScheduledApply.cpp`: the **serial-vs-scheduled
  differential** — both ledgers built by the test itself (bypassing the
  canonicalising `TxTest::close`), asserting a byte-identical, non-trivial
  account-state root. This is the determinism-critical contract; it passes.

## Phase 3 — threaded executor + server integration — BUILT
- **Threaded execution.** `applyScheduled(..., unsigned workers)` applies the
  independent groups across a thread pool (each group in its own view over the
  immutable closed snapshot; disjoint write-sets merged sequentially in fixed
  group order). `ScheduledApply.ThreadedMatchesSerialAcrossManyGroups` runs 12
  groups across 8 threads, 8 iterations, each byte-identical to serial.
- **Server integration.** `BuildLedger.cpp::applyTransactions` has a flag-gated
  branch (`XRPL_PARALLEL_APPLY`, default OFF → unchanged default behaviour) that
  schedules the canonical set and applies it via `applyScheduled`, merging into
  the close `OpenView`. Works because `Application` *is-a* `ServiceRegistry` and
  `OpenView` *is-a* `TxsRawView`. Compiles under `xrpld=ON`.
  `XRPL_PARALLEL_APPLY_WORKERS` overrides the worker count
  (default `clamp(cores-2, 2, 8)`).

## Network determinism test via xrpld-lab (the live oracle)
A local multi-validator network is itself a determinism check: if parallel apply
were non-deterministic, validators would compute different ledger hashes and
fail to validate. Procedure:
1. Build: `cmake --build build --target xrpld` (xrpld=ON).
2. `cp build/xrpld /Users/infinityworks/projects/xrplf/xrpld-lab/xrpld`
3. `cd xrpld-lab && xrpld-lab create:network --protocol xrpl --local --num_validators 3 --num_peers 1 --genesis True`
4. `export XRPL_PARALLEL_APPLY=1` then run the cluster's `start.sh` (the env var
   propagates to all `nohup ./xrpld` children).
5. Push disjoint-payment load at `ws://127.0.0.1:6016`.
6. Assert all validators agree on `ledger_hash`/`account_hash` each round (e.g.
   poll `server_info`/`ledger` across nodes). Divergence ⇒ a determinism bug.

## Network determinism test — RUN & PASSED (xrpld-lab, 3 validators)
Built the full `xrpld` binary from this branch and ran a 3-validator local network
via xrpld-lab with `XRPL_PARALLEL_APPLY=1` (4 workers) on every node:
- Consensus advanced normally (proposers=2, ~2s converge), parallel path active
  on all nodes (logs: `Parallel apply: N applied across K group(s)`).
- Under disjoint-payment load, every validator independently scheduled identical
  groups — `5 applied across 5 group(s)`, `10 across 1` (same-source funding) —
  and produced **byte-identical `ledger_hash` AND `account_hash` on every ledger**.
- This is the within-run determinism oracle: 3 independent validators each
  scheduling + thread-pool-applying the same tx sets agreed on every state root.
  A nondeterministic apply would have diverged and stalled validation. It didn't.

(The earlier standalone cross-restart hash comparison is an INVALID method —
two pure-serial runs also differ because standalone ledger composition varies
with submit/accept timing. The multi-validator within-run agreement above is the
valid test; the unit `ScheduledApply` differential is the controlled complement.)

## Measured speedup (Release, apply engine in isolation) — MODEST, overhead-bound
`ScheduledApply.ThroughputBenchmark`, 400 disjoint payments, best of 5, Release
(NDEBUG → access-set assertion + instrumentation compiled out):

| workers | us/tx | speedup |
|---|---|---|
| 1 | 35.4 | 1.00x |
| 2 | 30.4 | 1.17x |
| 4 | 24.9 | 1.42x (peak) |
| 8 | 35.2 | 1.01x (regressed) |

Honest reading — this is NOT the headline 5–10x:
1. **Overhead-bound.** The current engine spawns `std::thread`s *per ledger* and
   builds an `OpenView` per group with a sequential merge. For cheap payments
   (~35 us/tx serial) that fixed overhead rivals the work, so it peaks ~1.4x at
   4 workers and goes net-negative by 8. A persistent thread pool + lower
   per-group overhead is required to scale; the per-ledger spawn is the first
   thing to fix.
2. **Apply isn't the payment bottleneck.** ~35 us/tx serial ≈ 28k apply/s on one
   core — far above the ~159 TPS network ceiling. For payment-dominated load the
   ceiling is elsewhere in the pipeline (consensus, relay, admission), so
   parallelizing *apply* alone yields limited end-to-end gain. The plan's big
   numbers require parallelizing the EXPENSIVE transactors (AMM/DEX/paths) — and
   those are exactly the ones still `touchesGlobal` (serial) in v1.
3. **Worker default needs rethinking.** `clamp(cores-2, 2, 8)` over-threads this
   workload; ~4 was best here. Tune per measurement, not a fixed default.

Net: the parallelism is real and deterministic (verified), but v1's economic case
is weak — modest payment speedup, with the large wins gated behind parallelizing
the dynamic-footprint transactors and a lower-overhead executor.

## What remains genuinely uncertifiable in a coding run
- **Network/production certification.** A green lab run is strong evidence but
  not proof. Shipping parallel apply to mainnet still needs: ThreadSanitizer-clean
  runs, adversarial scheduling (Antithesis), and a 12-month mainnet-replay
  differential CI gate (hundreds of GB, out-of-repo). A determinism bug forks the
  network, so these are non-negotiable before the flag becomes an amendment.
- **Phase 5 (amendment).** `ParallelApply` amendment + validator governance vote.
- **Productionization of the flag** (Config stanza / amendment gate instead of an
  env var) and faithful failed/retry-set parity for adversarial (invalid-tx)
  workloads — the current branch clears the set after a successful grouped apply,
  which is correct for valid load but not yet a full match of the serial path's
  failure bookkeeping.
- **Phase 4 (testnet load).** Operational: testnet + load harness + weeks of
  runtime to hit the ≥1000 TPS target. Cannot be done from the repo.
- **Phase 5 (amendment).** `ParallelApply` amendment + governance vote.

## How to verify locally
Debug build, `-Dtests=ON -Dxrpld=OFF`; build `xrpl.test.tx` and
`xrpl.test.protocol_autogen`; run both. The DEBUG assertion validates every
migrated transactor against real apply tests. `XRPL_ACCESS_AUDIT=1` logs the
measured footprint of `touchesGlobal` transactors (Phase-1.5 audit data).
