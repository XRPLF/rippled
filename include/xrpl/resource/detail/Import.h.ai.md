## `Import.h` — Gossip-Originated Consumer Data Container

`Import.h` defines the data structure that bridges the gossip protocol and the internal resource-management accounting within the `xrpl::Resource` subsystem. It is a small but architecturally meaningful type: the container through which one XRPL node's observation of peer load is absorbed into another node's own rate-limiting decisions.

### Context: Why Gossip Imports Exist

The resource management system tracks per-endpoint load using an exponentially decaying balance (see `Entry.h`). Load observed locally decays over a 32-second window. Without any other signal, a node can only act on behavior it witnesses directly. The gossip mechanism lets a cluster of servers share what they know: peer A tells peer B which inbound endpoints are consuming heavy resources. Peer B can then pre-emptively apply pressure or drop those connections before they accumulate enough local balance to trigger thresholds on their own.

`Import` is the in-memory representation of one such gossip snapshot, keyed by the originating peer (`origin` string) in the `importTable_` hash map maintained by `Logic`.

### Structure

`Import` holds two fields: `whenExpires` (a `clock_type::time_point` controlling how long this snapshot stays valid) and `items` (a `std::vector<Item>`).

Each nested `Item` pairs an `int balance` — the raw load score reported by the gossip origin — with a `Consumer` handle into the local `Entry` table. The `Consumer` is not merely a label; it is a live reference-counted handle to a local `Entry`, whose `remote_balance` field is modified in-place when gossip is imported or expired. This direct coupling is what makes imports efficient: rather than re-scanning all entries, `Logic` can debit the old contribution by walking the items of the expiring `Import` and decrementing `remote_balance` directly on each `Consumer`'s backing `Entry`.

### The Dummy `int` Constructor

The `Import(int = 0)` constructor exists to satisfy a subtle requirement from `importTable_.emplace()` in `Logic::importConsumers()`. The call uses `std::piecewise_construct` with `std::make_tuple(m_clock.now().time_since_epoch().count())` as the value arguments. That count is a `long long`, but `Import` only has the `int`-accepting constructor, so the integer implicit conversion path is used. The comment "Dummy argument required for zero-copy construction" signals that this is an in-place construction optimization — the `Import` is constructed directly inside the map node, avoiding a copy. The argument itself is not used; `whenExpires` is set immediately afterwards.

### How `Logic` Uses `Import`

`Logic::importConsumers()` processes each incoming `Gossip` and either creates a new `Import` (first-seen origin) or updates an existing one. The update path is particularly careful: new credits are applied to `remote_balance` first, then the old credits are debited. This avoids a window where an entry's `remote_balance` temporarily reads zero, which could briefly misrepresent load levels to concurrent callers evaluating `disposition()`.

During `periodicActivity()`, any `Import` whose `whenExpires` has passed (30 seconds, per `gossipExpirationSeconds` in `Tuning.h`) has its items walked and their `remote_balance` contributions subtracted before the entry is erased. This rollback is the primary reason each `Item` stores its own `balance` snapshot rather than querying the entry at expiry — the entry's `remote_balance` may have been further modified by subsequent gossip rounds, so the original credited amount must be tracked explicitly to reverse it correctly.

### Relationship to Sibling Types

`Gossip.h` defines the wire-format counterpart: `Gossip::Item` carries a `beast::IP::Endpoint address` rather than a live `Consumer`. The conversion from `Gossip::Item` to `Import::Item` in `Logic::importConsumers()` is where an address is looked up or created as an inbound `Entry`, and the resulting `Consumer` handle is stored in the `Import`. `Import` thus acts as the resolved, live form of raw gossip data — it has resolved addresses to local tracking handles and is ready for direct balance manipulation.

The `Consumer` field in `Import::Item` is intentional: `Consumer` is a reference-counted RAII wrapper around `Entry`. Keeping a `Consumer` alive inside `Import` ensures the underlying `Entry` is not freed during the gossip lifetime window, even if no peer connection is currently active for that endpoint. This is a subtle liveness guarantee: gossip data continues to suppress misbehaving endpoints that may have disconnected locally.