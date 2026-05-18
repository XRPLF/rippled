# `TrafficCount.h` — Overlay Network Traffic Accounting

## Role in the System

`TrafficCount` provides fine-grained, per-category accounting of bytes and message counts flowing through the XRPL overlay network. It sits in the `detail/` subdirectory of the overlay layer and is owned by `OverlayImpl`, which exposes it through `reportInboundTraffic()` and `reportOutboundTraffic()` thin wrappers. The class answers the question: *across all ~60 distinct protocol message categories, how many bytes and messages have entered or left this node?* That data is periodically harvested by `collect_metrics()` and pushed to whatever external monitoring system the node operator configures (e.g., Graphite/StatsD via the `beast::insight` collector).

## `TrafficStats` — The Per-Category Counter

`TrafficStats` is a plain data holder: four `std::atomic<uint64_t>` fields (`bytesIn`, `bytesOut`, `messagesIn`, `messagesOut`) plus a human-readable `name` string derived via `to_string()` at construction. Using atomics avoids a mutex on every message receive/send, which matters because dozens of peer connections update these counters concurrently from different threads.

The copy constructor is non-trivial: it calls `.load()` on each atomic to capture a consistent snapshot. This is what allows `getCounts()` to return the map by `const&` while callers can still take a point-in-time snapshot for reporting without holding any lock. The `operator bool()` conversion returns `true` only if at least one message has been counted in either direction — allowing monitoring code to skip categories with no activity.

## The `category` Enum — Taxonomy of Protocol Traffic

The `category` enum has roughly 60 named entries organized around several axes:

**Protocol message type** — `transaction`, `proposal`, `validation`, `validatorlist`, `manifests`, `overlay`, `squelch`, `base` (ping, status change).

**Sub-category breakdowns** — `transaction_duplicate`, `proposal_untrusted`, `proposal_duplicate`, `validation_untrusted`, `validation_duplicate`. These are incremented *in addition to* the parent category, not instead of it, so operators can see both raw volume and breakdown without needing arithmetic.

**Ledger data exchange** — `TMLedgerData` and `TMGetLedger` messages carry an internal type field (`liTS_CANDIDATE`, `liTX_NODE`, `liAS_NODE`) that determines whether they carry transaction sets, transaction tree nodes, or account state nodes. Each combination plus direction (`_get` vs `_share`) gets its own category, yielding the `ld_*` and `gl_*` families.

**`TMGetObjectByHash`** — similarly subdivided by the object type field (`otLEDGER`, `otTRANSACTION`, `otTRANSACTION_NODE`, `otSTATE_NODE`, `otCAS_OBJECT`, `otFETCH_PACK`, `otTRANSACTIONS`).

**Special sentinel categories**: `total` accumulates raw wire bytes for every recognized message exactly once per send/receive, independently of the detailed categorization. The class comment explicitly notes that `unknown` traffic is *not* rolled into `total`. `unknown` catches any protobuf message type that falls through all the classification logic.

**Squelch categories**: `squelch_suppressed` records bytes that were *not* actually transmitted because the destination peer had been squelched, and `squelch_ignored` records bytes arriving from peers that are ignoring squelch instructions. Both are reported with `bytes = 0` or the suppressed buffer size respectively, giving operators visibility into the effectiveness of the squelch mechanism.

## `categorize()` — The Classification Engine

Implemented in `TrafficCount.cpp`, `categorize()` is a static method that takes a `google::protobuf::Message`, its `protocol::MessageType`, and a direction flag.

For the majority of message types a single static `unordered_map<MessageType, category>` lookup suffices — `mtPING`, `mtSTATUS_CHANGE` → `base`; `mtTRANSACTION` → `transaction`; etc. The remaining message types require inspecting message internals, so the method uses `dynamic_cast` to downcast the base protobuf reference to the specific generated message class:

- For `TMLedgerData` it checks `msg->type()` and `msg->has_requestcookie()`. The presence of a request cookie indicates whether this is a response (share) rather than an unsolicited push (get).
- For `TMGetLedger` the direction semantics are inverted: an inbound message or one bearing a cookie signals a share rather than a get.
- For `TMGetObjectByHash` the internal `query()` flag combined with the direction determines get/share, while `type()` selects the object type subcategory.

This design cleanly separates classification (pure read of the message) from accounting (mutation of atomic counters), enabling callers to invoke `addCount()` multiple times with different categories for the same wire message — once for the specific category, once for a sub-category (e.g., `transaction_duplicate`), and once for `total`.

## Usage Pattern in `PeerImp`

On the inbound path in `PeerImp`, the message category is resolved once via `categorize()`, then `reportInboundTraffic()` is called twice: once with `category::total` (raw wire size) and once with the resolved category. For duplicate transactions, untrusted proposals, and untrusted validations, an additional `reportInboundTraffic()` call with the sub-category fires later in the message handler after the necessary checks.

On the outbound path, if the squelch logic suppresses a message, `reportOutboundTraffic(squelch_suppressed, ...)` is called instead of the normal send, making the suppression visible in metrics without distorting the per-category send counters.

## Storage and Initialization

`counts_` is a `std::unordered_map<category, TrafficStats>` whose entire population is specified as an inline member initializer in the class definition. Every known category including `total` and `unknown` is pre-inserted at construction. `addCount()` performs a map lookup and silently returns if the category key is absent — a safety valve that also conveniently allows callers to pass arbitrary `category` values without risk. The `XRPL_ASSERT` guards against values outside the enum range in debug builds.

`to_string()` uses a static local `unordered_map` to produce monitoring-friendly names like `"ledger_data_Transaction_Set_candidate_get"`. The `unknown` category is intentionally absent from this map; the method returns the literal string `"unknown"` by falling through to a default return, so `TrafficStats` objects in `counts_` for the unknown category still get the correct display name.