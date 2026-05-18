# `TrafficCount.cpp` — P2P Message Categorization for Network Traffic Telemetry

This file implements a single static method: `TrafficCount::categorize()`. Its purpose is to inspect an incoming or outgoing XRPL overlay protocol message and assign it to one of roughly fifty fine-grained traffic `category` values, enabling the node's traffic monitoring infrastructure to track bandwidth and message counts broken down by message type and data-flow direction.

## Why This Exists

The XRPL overlay network multiplex many distinct data-exchange patterns through a small number of protobuf message types. A single `protocol::mtLEDGER_DATA` wire type can carry ledger data in response to a direct request, in response to a forwarded request, or as an unsolicited share — and these scenarios have very different implications for network load analysis. Similarly, `mtGET_OBJECTS` can be a request for a ledger, a transaction, a state node, a CAS object, or a fetch pack. A flat message-type counter would be useless for diagnosing bottlenecks or capacity planning. `categorize()` bridges the gap between the coarse wire protocol and the granular telemetry the operators need.

## Two-Stage Classification Strategy

The function uses a two-stage approach that reflects the different structural complexity of the message types it handles.

For messages whose type alone is sufficient to determine the traffic category, a file-scope `const std::unordered_map<protocol::MessageType, TrafficCount::category>` named `type_lookup` handles classification in O(1). This covers sixteen types including `mtPING`, `mtTRANSACTION`, `mtVALIDATION`, `mtPROPOSE_LEDGER`, `mtSQUELCH`, and the newer `mtHAVE_TRANSACTIONS`/`mtTRANSACTIONS` pair. The map is a `const` at file scope, initialized once, and never modified — this is the correct design because the lookup is called on every received and sent message.

`mtHAVE_SET` is handled as a one-line special case immediately after the map lookup. Its category depends purely on the `inbound` flag: `get_set` if the node received it (it's learning what a peer holds) versus `share_set` if the node sent it (it's announcing what it holds). This direction split is common enough to be worth its own named pair.

## Protobuf-Level Inspection for Complex Types

The remaining complexity in the file handles three message types that carry sub-type or role information embedded in protobuf fields, not in the message type discriminator.

**`TMLedgerData`** (ledger data responses) is the most nuanced. The code `dynamic_cast<protocol::TMLedgerData const*>(&message)` tests the runtime type and, on success, inspects `msg->type()` to determine the ledger data variety (transaction set candidate, transaction node, account state node, or generic), then uses a two-condition expression to pick between a `*_get` and `*_share` category:

```cpp
(inbound && !msg->has_requestcookie()) ? ld_tsc_get : ld_tsc_share
```

The `requestcookie` test is the subtle part. In the XRPL overlay, a `TMLedgerData` carrying a `requestcookie` is a response that was routed through an intermediate peer — even if the local node received it (inbound), the cookie marks it as data that was originally requested by someone else and forwarded. Without the cookie, an inbound `TMLedgerData` represents data the local node actively fetched. This semantic distinction maps to `*_get` vs `*_share` in the category names.

**`TMGetLedger`** (ledger data requests) reverses the cookie logic slightly:

```cpp
(inbound || msg->has_requestcookie()) ? gl_tsc_share : gl_tsc_get
```

An inbound request means a peer is asking for data the local node holds — that's the node "sharing." An outbound `TMGetLedger` without a cookie is the local node actively requesting. Presence of a cookie again signals a forwarded request, which is categorized under the "share" branch since the node is acting as a relay.

**`TMGetObjectByHash`** handles object hash requests and responses. Here the `query()` field plays the role that the message direction plays for ledger data: a true `query` flag marks a request (the node wants data), false marks a response (the node is providing data). The expression `msg->query() == inbound` evaluates to true precisely when the message is a request flowing in the expected direction — a query arriving inbound means a peer is requesting, which is a "share" operation for the local node. This bidirectional role-detection applies across six object sub-types: ledger, transaction, transaction node, account state node, CAS object, and fetch pack. An `otTRANSACTIONS` object type has no get/share split and always maps to `get_transactions`.

## Integration with `Message` and `OverlayImpl`

`categorize()` is called at two points in the call graph. In `Message.cpp`, the `Message` constructor invokes it immediately with `inbound = false` to classify outbound messages at construction time, storing the result in `category_`. This means categorization cost is paid once per outbound message, not per recipient peer. In `OverlayImpl`, `reportInboundTraffic()` and `reportOutboundTraffic()` call `TrafficCount::addCount()` with the category, which updates atomically-maintained per-category byte and message counters inside the `TrafficCount` object that `OverlayImpl` owns as `m_traffic`.

If none of the static map lookup, the `mtHAVE_SET` check, or the three `dynamic_cast` branches match, `categorize()` returns `TrafficCount::category::unknown`. This is a safe fallback; callers that receive `unknown` can still increment the `unknown` counter rather than crashing or silently discarding the traffic. The `unknown` category is always registered in the `counts_` map, so `addCount()` will always find it.