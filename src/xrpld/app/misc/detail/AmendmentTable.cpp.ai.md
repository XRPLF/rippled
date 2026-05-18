# `src/xrpld/app/misc/detail/AmendmentTable.cpp`

## Role and Purpose

This file is the sole implementation of the XRP Ledger's amendment governance system. Amendments are named 256-bit feature flags that change transaction-processing rules once a supermajority of the network's trusted validators vote for them over a sustained window. `AmendmentTableImpl` implements the abstract `AmendmentTable` interface from `xrpl/ledger/AmendmentTable.h` and is the engine that drives every step of that lifecycle: collecting validator votes, tallying them, tracking majority windows, injecting governance pseudo-transactions into candidate ledgers, and persisting per-node vote preferences across restarts.

## Internal Class Hierarchy

Four cooperating types are defined entirely within this translation unit.

### `parseSection()`

A file-local helper that converts a `Section` (a key-value block from the config file) into a vector of `(uint256 id, string name)` pairs. It uses a compiled `boost::regex` to enforce the exact format expected — 64 hex digits, whitespace, a non-whitespace description — throwing `std::runtime_error` on any malformed line. Two validations are layered: the regex rejects anything that isn't structurally correct, and `uint256::parseHex` independently validates the hex string, making the config parser fail early with informative messages rather than silently producing wrong state.

### `TrustedVotes`

This class addresses a subtle liveness problem called "flapping." During consensus, validators broadcast `STValidation` messages that include the set of amendments they support. Near a flag ledger (where majority is computed), a validator that temporarily loses synchronisation will not broadcast. Without caching, its "yes" votes disappear, making an amendment appear to lose support even though the validator hasn't changed its opinion.

`TrustedVotes` maintains a `hash_map<PublicKey, UpvotesAndTimeout>`, keyed on validator identity. Each entry stores the most recent upvotes alongside an expiry timestamp. When `recordVotes()` is called at each voting round, it updates entries from incoming validations and sets their timeout 24 hours into the future. Entries that have not been refreshed within 24 hours are expired to `empty` — the code calls this "losing confidence." The 24h window is deliberately long: it means a flaky validator can be offline for up to 24 hours without disturbing the amendment vote record, so actual flapping can happen no more frequently than once per day from any single validator.

The `trustChanged()` method rebuilds the map when the UNL changes, preserving existing vote history for validators still in the UNL and dropping data for validators that have been removed.

Critically, every public method requires a `std::lock_guard<std::mutex> const&` parameter, passing the lock by reference. This is a deliberate API contract: the caller must hold `AmendmentTableImpl::mutex_` before calling these methods. The lock is taken externally, not internally, which prevents lock inversion and makes the synchronisation relationship visible at compile time.

### `AmendmentSet`

A snapshot of one voting round. Its constructor calls `TrustedVotes::getVotes()` to get the per-amendment vote counts and the number of validators with active (non-expired) votes. The threshold is derived from `amendmentMajorityCalcThreshold`, which is `std::ratio<80, 100>` — 80% of active validators must vote yes.

The `passes()` predicate applies a strict "greater than threshold" test (`votes > threshold`), with a single exception: when there is exactly one trusted validator, `>=` is used instead of `>`, since achieving more than 100% is mathematically impossible. This edge-case handling ensures single-validator test networks and early bootstrap configurations still work correctly.

### `AmendmentState`

A plain struct holding the current local knowledge about one amendment: whether it is `enabled` (a one-way flag — once true, never reset), whether this server has code `supported` for it, the local `vote` preference (`up`, `down`, or `obsolete`), and the human-readable name. `enabled` being one-way reflects the protocol guarantee that amendments are irreversible once activated.

## `AmendmentTableImpl` — Core Logic

### Construction and Persistence Migration

The constructor integrates with `wallet.db` through three functions from `xrpl/server/Wallet.h`: `createFeatureVotes`, `readAmendments`, and `voteAmendment`. On startup, `createFeatureVotes` is called to create the `FeatureVotes` table if it does not already exist and returns whether the table previously existed. This drives a one-time migration: if the table is new, config-file `[amendments]` and `[veto_amendments]` sections are parsed and written into the database. If the table already exists, the config sections are silently ignored (with a warning) because the database is the authoritative source. This migration avoids double-applying config on restart and moves toward a fully database-driven governance state.

An invariant respected throughout: once an amendment's `vote` is set to `obsolete` (for features being phased out), no subsequent `veto()`, `unVeto()`, or DB read can override it. The `obsolete` state also suppresses the amendment from appearing in `doValidation()` output, so the server will never broadcast a vote for it.

### `doValidation()` — Populating Outbound Validations

Called by consensus during validation construction, this method returns the list of amendment IDs that should be included in the local `STValidation` broadcast. The filter is: amendments that are `supported`, not vetoed (`vote == up`), and not already `enabled` on the current ledger. The result is sorted for determinism. Callers embed this list in the `sfAmendments` field of the validation message.

### `doVoting()` — The Core Tally

Called at every flag ledger, this is where `TrustedVotes::recordVotes()` is invoked to update cached validator votes, an `AmendmentSet` is constructed to snapshot the tally, and then a decision is reached for each known amendment:

- If validators reach supermajority and the ledger does not yet record majority: `tfGotMajority`
- If the ledger records majority but validators no longer agree: `tfLostMajority`
- If the ledger records majority *and* the majority window has elapsed (closeTime ≥ majorityTime + `majorityTime_`) and the local node votes yes: flag `0` (trigger enablement)

The return is a `std::map<uint256, std::uint32_t>` of amendment IDs to flag values. The caller in `AmendmentTable.h`'s non-virtual `doVoting()` adapter translates each entry into a `ttAMENDMENT` pseudo-transaction injected into the initial ledger position before consensus begins. Keeping the map construction separate from the pseudo-transaction injection is a deliberate layering: `AmendmentTableImpl` stays independent of the ledger and SHAMap code.

### `doValidatedLedger()` and `needValidatedLedger()`

After consensus, `doValidatedLedger()` is called with the full set of enabled amendments and the current majority-in-progress set from the validated ledger. `enable()` is called for each newly enabled amendment; if any is unsupported, `unsupportedEnabled_` is set to `true`. The method also recomputes `firstUnsupportedExpected_` — the earliest wall-clock time at which an unsupported amendment will be forcibly enabled. This lets the server surface a degradation warning well before the event.

`needValidatedLedger()` avoids processing every ledger by checking whether the current and previous sequences fall within the same 256-ledger band `((seq - 1) / 256)`. Flag ledgers occur at multiples of 256, so amendment state can only change when that quotient changes.

## Concurrency Model

All mutable state in `AmendmentTableImpl` is protected by a single `mutable std::mutex mutex_`. Read-only query methods (`isEnabled`, `isSupported`, `hasUnsupportedEnabled`, `firstUnsupportedExpected`, `getJson`) acquire the mutex using `std::lock_guard` on entry and hold it for the duration. Methods that must delegate to `TrustedVotes` acquire `mutex_` first and then pass the `lock_guard` reference into `TrustedVotes` methods — ensuring that `TrustedVotes` internal state is always accessed under the outer lock and never acquires its own, eliminating any possibility of nested locking or deadlock.

The `lastVote_` pointer, a `std::unique_ptr<AmendmentSet>`, is replaced atomically under the lock at the end of `doVoting()`. It is read under the lock in `injectJson()` to populate vote counts in admin-facing JSON responses.