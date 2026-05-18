# `xrpl::Resource::Charge` — Consumption Charge Value Type

`Charge` is the fundamental unit of resource accounting in the XRPL server's rate-limiting system. It pairs an integer cost value with a human-readable label, forming the atomic token that callers pass to `Consumer::charge()` whenever server work is performed on behalf of an external endpoint.

## Role in the Resource Management System

The broader `xrpl::Resource` module (documented in `README.md`) tracks load imposed by every inbound websocket client and peer-overlay connection. When any significant server operation is performed — validating a signature, executing an RPC call, processing peer data — the responsible handler applies one of the pre-defined `Charge` instances from `Fees.h`. The `Consumer` accumulates these charges into a running balance; when that balance crosses heuristic thresholds, the `ResourceManager` warns or disconnects the endpoint.

`Charge` itself is intentionally passive: it carries no behaviour beyond holding a cost and a name. All of the policy logic — decaying balances, threshold evaluation, gossip propagation — lives in `Consumer`, `Logic`, and `Entry`. The separation keeps policy change isolated from the charge vocabulary.

## Design Decisions

**Default construction is deleted.** A `Charge()` with no cost and no label would be a meaningless value that could silently do nothing when applied. By deleting the default constructor, every `Charge` object must be created with an explicit integer cost, preventing accidental zero-cost charges from slipping through. The comment in the header notes that a default-constructed object would have no way to produce a meaningful label, reinforcing that this is by intent.

**Comparison ignores the label.** The implementations of `operator==` and `operator<=>` in `Charge.cpp` compare only `m_cost`, completely ignoring `m_label`. Two charges with the same numeric cost but different labels are equal by this ordering. This is deliberate: the label is purely diagnostic — a convenience for logging and operator display — while the cost is what matters for rate-limiting arithmetic. Treating them as equal when costs match simplifies callers that need to sort or deduplicate charges by severity.

**`operator*` scales cost, preserves label.** The multiplication operator produces a new `Charge` with `m_cost * m` but the original `m_label`. This allows a caller to express "twice the standard heavy RPC fee" without inventing a new named charge, while log output still identifies the charge family by its original label.

## The Canonical Charge Schedule

`Fees.h` declares sixteen `extern Charge const` objects that form the server-wide vocabulary of costs:

- **Protocol charges** — `feeMalformedRequest`, `feeInvalidSignature`, `feeUselessData`, `feeInvalidData`, `feeRequestNoReply` — applied when peers send bad or unserviceable protocol messages.
- **RPC charges** — ranging from `feeReferenceRPC` (a baseline default) up through `feeMediumBurdenRPC` and `feeHeavyBurdenRPC`, with `feeMalformedRPC` and `feeExceptionRPC` for error cases.
- **Peer charges** — `feeTrivialPeer`, `feeModerateBurdenPeer`, `feeHeavyBurdenPeer` — for peer-overlay work that does not map neatly to RPC semantics.
- **Administrative signals** — `feeWarning` and `feeDrop` — applied when the `ResourceManager` issues a warning or forcibly drops a connection; these encode the cost of the administrative action itself in the same accounting ledger.

By declaring these as `const` objects rather than plain integer constants, the type system ensures that every application of load carries a label traceable through logs and diagnostics.

## Display and Diagnostics

`to_string()` formats a `Charge` as `"label ($cost)"`, using the dollar-sign metaphor to signal that the number is a unitless resource credit value, not actual currency. The stream `operator<<` delegates to `to_string()`, making `Charge` objects directly printable in any `beast::Journal` or `std::ostream` context without further formatting helpers.

## Integration Point

The primary consumer of `Charge` outside this module is `Consumer::charge(Charge const& fee, ...)`, which applies the charge's cost to the endpoint's running balance and returns a `Disposition` indicating whether the endpoint should be warned, dropped, or allowed to continue. Because `Charge` is a simple value type, it is always passed by `const&` and imposes no ownership or lifetime concerns on callers.