# `include/xrpl/resource/detail/Entry.h`

## Role in the Resource Management System

`Entry` is the per-endpoint accounting record at the heart of XRPL's resource control subsystem. When a remote peer connects — whether inbound, outbound, or an administrative "unlimited" connection — the `Logic` table allocates exactly one `Entry` for it, keyed by `Kind` and IP address. Every charge assessed against that peer is posted here, and every disconnect or warning decision consults the values stored here. The struct is intentionally kept in the `detail` subdirectory because it is an implementation artifact of `Logic`; callers interact with the higher-level `Consumer` handle instead.

## Intrusive List Membership

`Entry` inherits from `beast::List<Entry>::Node`, making it a node in a doubly-linked intrusive list. `Logic` maintains four such lists: `inbound_`, `outbound_`, `admin_`, and `inactive_`. Because the list is intrusive, a given `Entry` can occupy **at most one** list at a time — moving an entry from active to inactive requires explicit removal before re-insertion. This design avoids separate heap allocations for list nodes and gives O(1) traversal and migration, which matters for the periodic expiration sweep that runs across potentially thousands of connections. The comment in `Logic.h` calling out this single-membership invariant is a meaningful warning: violating it corrupts the list structure silently.

## Balance Tracking: Local vs. Remote

The core accounting uses two independent components that are summed to produce an effective load level:

**`local_balance`** is a `DecayingSample<decayWindowSeconds, clock_type>` — an exponentially decaying accumulator templated on the 32-second window defined in `Tuning.h`. Each call to `add(charge, now)` ages the existing value by the elapsed time, then adds the new charge. The raw internal value is divided by the window size on every read, yielding a normalized "load rate" rather than a raw cumulative sum. The decay ensures that a burst of activity gradually fades; an entry that goes quiet for more than four window widths (≈128 seconds) is reset to zero entirely. This design correctly models a leaky-bucket rate limit without requiring periodic batch resets.

**`remote_balance`** is a plain `int` representing load contributed by **gossip imports** from peer nodes. When the local node receives gossip about a misbehaving remote address, it inflates that address's effective balance by writing directly to `remote_balance`. Keeping this separate from `local_balance` is intentional: remote data arrives as a normalized snapshot (not a time-series), so it cannot be fed into the decaying accumulator meaningfully. The two are added together only at query time in `balance()` and `add()`, keeping the accounting models cleanly separated.

## Key Back-Pointer and Reference Counting

`key` is a raw pointer back to the `Key` stored as the hash-map key in `Logic::table_`. The comment calls this "a bit of a hack" — the pointer exists solely to make `to_string()` and `isUnlimited()` work without requiring a separate copy of the address and kind. Callers must ensure the `Entry` does not outlive its owning map entry, which is enforced by the `refcount`: the entry stays in the table as long as any `Consumer` holds a reference (`refcount > 0`), and is only eligible for expiration once the count reaches zero and `whenExpires` has passed.

## `isUnlimited()` and the `kindUnlimited` Exemption

`isUnlimited()` returns `true` when `key->kind == kindUnlimited`. Entries of this kind represent trusted administrative connections (e.g., local admin RPC callers) that bypass the warning and disconnect thresholds in `Tuning.h` — `warningThreshold = 5000` and `dropThreshold = 25000`. The exemption is checked at the `Logic` layer; `Entry` itself just reports the flag. Notably, even unlimited entries can still be blocked from certain RPC commands based on `Role`, which is a separate authorization layer.

## Lifecycle Fields

`lastWarningTime` is stamped when `Logic` emits a load warning to the peer, ensuring warnings are rate-limited and not repeated on every single charge. `whenExpires` is set when an entry's `refcount` drops to zero, marking it for cleanup after `secondsUntilExpiration` (300 seconds) of inactivity. Until that deadline passes, the entry lingers in the `inactive_` intrusive list so that a reconnecting peer can resume its accumulated balance rather than starting fresh — a deliberate anti-abuse measure that prevents short-disconnect-and-reconnect cycles from resetting load state.

## Relationship to Sibling Files

- **`Key.h`**: Defines the composite `{Kind, IP::Endpoint}` key that uniquely identifies an entry in the table. `Entry` holds a raw `const Key*` back-pointer.
- **`Kind.h`**: The `kindInbound / kindOutbound / kindUnlimited` enum that drives `isUnlimited()`.
- **`Tuning.h`**: Defines `decayWindowSeconds = 32` (used as the `DecayingSample` template argument), the warning and drop thresholds, and expiration timing constants.
- **`Logic.h`**: The owning component; allocates `Entry` objects into its `hash_map<Key, Entry>` and moves them between the four intrusive lists as connection state changes.
- **`DecayingSample.h`**: The time-aware accumulator powering `local_balance`. Its integer decay algorithm (`m_value -= (m_value + Window - 1) / Window` per elapsed second) performs ceiling-division decay, ensuring the value reaches zero in finite time rather than asymptotically approaching it.