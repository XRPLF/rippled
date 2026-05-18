# LedgerCleaner.cpp

`LedgerCleaner.cpp` implements `LedgerCleanerImp`, a background maintenance service that detects and repairs two classes of database corruption that can accumulate in a long-running rippled node: inconsistencies between the node store (key-value SHAMap storage) and the SQLite account/transaction databases, and gaps or missing nodes in ledger history. It was originally motivated by bugs in older versions of rippled that could leave the SQL databases in a state that diverged from the node store. The implementation lives behind the `LedgerCleaner` abstract interface declared in `LedgerCleaner.h`, which the rest of the application holds as a `std::unique_ptr<LedgerCleaner>`. The concrete class is created exclusively through `make_LedgerCleaner()`.

## Threading Model

The cleaner owns a dedicated `std::thread` (`thread_`) that it starts via `start()` and tears down via `stop()`. The destructor enforces a strict lifecycle invariant: if `thread_` is still joinable at destruction time, `LogicError` is thrown. This is intentional — it makes improper shutdown a loud hard fault rather than a silent resource leak or undefined behavior from a detached thread accessing a destroyed object.

Shared state between the calling thread (which invokes `clean()`, `stop()`, and `onWrite()`) and the cleaning thread is protected by a single `std::mutex` combined with a `std::condition_variable` (`wakeup_`). The `run()` loop blocks on `wakeup_` until either the `State::cleaning` flag is set or `shouldExit_` becomes true, then calls `doLedgerCleaner()`. After `doLedgerCleaner()` returns, the state flips back to `notCleaning` and the loop waits again. This means re-invoking `clean()` while a previous run is still in progress simply updates the parameters atomically — the next iteration of `doLedgerCleaner()`'s inner loop will pick them up naturally.

## Parameter Handling

The `clean()` method accepts a `Json::Value` with several optional fields: `ledger` (single-ledger shortcut that also implies both `fix_txns` and `check_nodes`), `min_ledger`/`max_ledger` for range targeting, `full` as a shorthand that enables both modes simultaneously, `fix_txns` to rewrite SQL rows for transactions, `check_nodes` to verify node store completeness, and `stop` to cancel ongoing work by zeroing the range. When no explicit range is given, the cleaner defaults to the current fully-validated ledger range obtained from `LedgerMaster::getFullValidatedRange()`. Setting `stop` to true cleverly zeroes both `minRange_` and `maxRange_`, which the main loop detects as a terminal condition without needing a separate cancel flag.

## The Cleaning Loop

`doLedgerCleaner()` iterates from `maxRange_` downward toward `minRange_`, processing one ledger per iteration. Before each ledger it checks `getFeeTrack().isLoadedLocal()` — if the server is under elevated local load, the cleaner backs off entirely for five seconds. This makes it a genuinely low-priority background task that yields to foreground consensus and RPC work.

For each ledger, the loop calls `getHash()` to resolve the correct ledger hash, then `doLedger()` to actually validate and repair. After a successful pass, `maxRange_` is decremented (and `minRange_` is incremented if we reach the floor), then the loop sleeps 100 ms to throttle I/O pressure. On failure, `failures_` is incremented and the loop sleeps two seconds — long enough for `InboundLedgers` to make progress acquiring the missing data before the next attempt.

## Hash Resolution: `getHash()` and `getLedgerHash()`

Finding the authoritative hash for a target ledger is non-trivial when operating over potentially deep history. `getHash()` maintains a `referenceLedger` — a known-good subsequent ledger whose skip list can be traversed backward. It calls `hashOfSeq()` on the reference ledger to look up the target index. If `hashOfSeq()` throws `SHAMapMissingNode`, `getLedgerHash()` catches the exception and immediately triggers a new inbound ledger acquisition, returning `beast::zero` (all-bits-zero) as a sentinel. This lazy-repair pattern means transient gaps in the node store self-heal as fetches complete.

If the validated ledger's skip list doesn't reach the target directly (the hash comes back zero without an exception), `getHash()` calls `getCandidateLedger()` to compute the sequence number of an intermediate ledger whose skip list is guaranteed to contain the target. That intermediate ledger is acquired via `InboundLedgers::acquire()`, and if successful, the lookup is retried through it. This two-hop approach handles arbitrarily deep history without needing to walk ledger-by-ledger all the way back.

## Per-Ledger Repair: `doLedger()`

Given a hash, `doLedger()` performs up to four checks:

1. **Node store availability**: `InboundLedgers::acquire()` returns the live ledger object. If it's not yet available, the index is cleared from `LedgerMaster`'s cache and a fresh acquisition is triggered — a probe-and-kick pattern.

2. **SQL database consistency**: `loadByIndex()` loads the ledger from SQLite and compares the hash and parent hash against the node-store version. Any mismatch (including the common case from legacy bugs where the SQL row exists but contains wrong data) sets `doTxns = true` to force a rewrite.

3. **History index consistency**: `LedgerMaster::fixIndex()` verifies and corrects the ledger's entry in the history table. A mismatch here also forces `doTxns = true`.

4. **Node completeness** (when `doNodes` is true): `walkLedger()` traverses the entire SHAMap tree. If any node is missing, the ledger is cleared and re-acquired. This is the most expensive check and is only triggered by explicit user request via `check_nodes` or `full`.

If `doTxns` ended up true (either by request or by detecting a mismatch), `pendSaveValidated()` is called with `isSynchronous = true` to rewrite the SQL rows for this ledger synchronously before the cleaner advances to the next.

## Observability

`onWrite()` exposes the current state to `beast::PropertyStream`, which the application uses for status reporting (e.g., via the `server_info` RPC). When `maxRange_` is zero the cleaner reports `"idle"`; otherwise it reports the current range, both mode flags, and the failure count if nonzero. This gives operators a live view of cleaning progress without needing a separate monitoring thread.