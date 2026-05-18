# `include/xrpl/resource/Gossip.h`

## Role in the System

`Gossip.h` defines the plain data structure used to share resource-consumption intelligence between nodes in an XRPL cluster. It is the interchange format for the resource manager's distributed rate-limiting mechanism: when a server is seeing heavy traffic from a particular IP address, it can warn its peers so they can proactively watch or shed load from the same source.

The struct sits at the boundary between local accounting and inter-node communication. It carries no logic, no mutex, and no lifetime management — it is purely a typed envelope for serialization and transfer.

## The Data Model

`Gossip` holds a `std::vector<Item>`, where each `Item` pairs a `beast::IP::Endpoint` (the IP address of a consumer) with an `int balance`. The balance is a snapshot of that consumer's local load score as maintained by the `Entry` tracking objects inside `Logic`. A value of zero means no significant load; the scale is defined by `Tuning.h`, where `minimumGossipBalance = 1000` sets the entry bar for inclusion, `warningThreshold = 5000` triggers a warning, and `dropThreshold = 25000` causes disconnection.

The choice to carry only the endpoint address and an integer score — and nothing else — is deliberate. The receiving node already has its own `Entry` for every address it talks to directly; gossip only augments that entry's `remote_balance` field, reflecting load being observed elsewhere in the cluster. No state machine, no sequence numbers, no acknowledgments: gossip is advisory and expires automatically.

## Export: Selecting What to Share

`Logic::exportConsumers()` iterates the `inbound_` intrusive list (tracking connections that originated from external peers) and builds a `Gossip` by copying out the current `local_balance` for each entry. Entries whose balance falls below `minimumGossipBalance` are silently excluded — sharing noise about lightly-loaded consumers would waste bandwidth and complicate the receiving node's accounting. The result is a compact list of only the endpoints that are causing meaningful stress.

## Import: Applying Received Gossip

`Logic::importConsumers()` takes a `Gossip` and an `origin` string (the sending peer's identifier) and applies the contained balances as `remote_balance` adjustments on the local `Entry` objects for those endpoints. If gossip from the same origin has been received before, the previous contribution is first subtracted before the new values are added — ensuring the remote-balance figure always reflects the *current* view from that peer, not an accumulating sum. Imported gossip expires after `gossipExpirationSeconds` (30 seconds), so a peer going silent automatically drains its influence.

This import/export cycle is what connects `Gossip` to the rest of the rate-limiting system. An endpoint's total "threat score" in `Logic` combines both `local_balance` (what *this* node sees) and `remote_balance` (what peers reported via gossip). A botnet client that spreads its requests across many cluster nodes may never trigger a warning on any single node, but gossip aggregates the distributed signal so every node can act on it.

## Wire Encoding

`Gossip` itself has no serialization logic. The actual network transport happens in `PeerImp.cpp` using the `TMLoadSource` Protobuf message. Received load-source records are decoded into `Gossip::Item` instances (using `beast::IP::Endpoint::from_string` on the `name` field and mapping the `cost` field to `balance`) before being passed into `importConsumers()`. This keeps `Gossip` transport-neutral and easily testable in isolation — the `Logic_test.cpp` unit tests use it directly without any network layer.

## Design Notes

Both `Gossip` and `Gossip::Item` declare `explicit` default constructors, which prevents accidental aggregate-style initialization and makes the zero-initialized `balance` field intentional rather than implicit. The struct requires no destructor, copy constructor, or move semantics beyond defaults, since it holds only a vector of trivially-copyable-like items and a value-type endpoint.