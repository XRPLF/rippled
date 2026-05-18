# `include/xrpl/beast/net/IPEndpoint.h`

## Role in the System

`IPEndpoint.h` defines `beast::IP::Endpoint`, the canonical representation of a network address and port used throughout the XRPL node. It pairs an IP address with a 16-bit port number in a single, version-independent value type — the primary unit exchanged when the node discovers, stores, connects to, and classifies peers. The `Resolver` interface, PeerFinder subsystem, and resource-tracking components all traffic in `vector<beast::IP::Endpoint>`, making this a foundational type in the peer-to-peer networking stack.

## Address Abstraction

`Address` is a `using` alias for `boost::asio::ip::address`, which transparently represents both IPv4 and IPv6 addresses without requiring template polymorphism or inheritance. This choice is deliberate: `Endpoint` gains dual-stack capability at zero cost while keeping its API simple. The two convenience accessors `is_v4()` / `is_v6()` and the casting helpers `to_v4()` / `to_v6()` inline straight through to the underlying Boost.Asio address, ensuring no overhead at call sites that need to branch on protocol family. `Port` is simply `std::uint16_t`, mirroring the TCP/UDP port range exactly.

## Construction and String Parsing

There are three construction paths. The default constructor produces an unspecified endpoint (zero port, default `Address`). The explicit constructor takes an `Address` and an optional `Port` defaulting to `0`. String construction is handled by two static factories: `from_string_checked()` returns `std::optional<Endpoint>` — the preferred path when the input is untrusted — while `from_string()` returns an unspecified default endpoint on failure rather than propagating an error, which is appropriate for contexts that have already validated input or can tolerate a silent fallback.

The actual parsing is implemented in the companion `.cpp` file via `operator>>` on a `std::istream`. It handles three formats:

- **IPv4**: `192.168.0.1:8080` — colon is the address/port separator detected after the first `.` character.
- **IPv6 (bracketed)**: `[fe80::1]:8080` — opening `[` signals IPv6 mode; parsing consumes until the matching `]` then expects an optional `:port`.
- **Legacy space-separated**: `192.168.0.1 8080` — a space terminates the address field, with the port read as an integer from the remaining stream. This format is preserved specifically for backward compatibility with stored peer data.

`from_string_checked()` adds an outer guard: strings longer than 64 characters are rejected before touching the stream, providing a lightweight denial-of-service barrier. The stream parser also bounds-checks against `INET6_ADDRSTRLEN` internally and sets `failbit` on any invalid character or oversized token, ensuring parse failures are communicated cleanly.

## Immutability and the `at_port()` Pattern

`Endpoint` is a value type: it carries its data by value, and all accessors return by value or const reference. There is no `set_port()` mutator. Instead, `at_port(Port)` returns a new `Endpoint` with the same address but a different port. This functional style prevents accidental partial mutation and makes endpoint transformations explicit at the call site — useful, for example, when normalising an endpoint from a peer advertisement to a canonical port.

## String Formatting

`to_string()` mirrors the parsing contract: IPv4 endpoints format as `addr:port` and IPv6 endpoints as `[addr]:port`, in both cases omitting the port suffix when `port() == 0`. Buffer capacity is pre-reserved using `INET6_ADDRSTRLEN` to avoid reallocations for the common case.

## Comparison and Ordering

The full six-operator comparison suite is provided. Only `operator==` (exact address and port equality) and `operator<` (lexicographic: address first, then port) are defined in the `.cpp`; the remaining four operators are synthesised inline from those two following standard practice. The ordering makes `Endpoint` usable as a `std::map` key or in sorted containers without any additional comparator.

## Hashing

`Endpoint` participates in the beast `hash_append` protocol: the `hash_append` friend function feeds both `m_addr` and `m_port` into a `Hasher` object. The `Address` hash already handles the IPv4/IPv6 branch by hashing the respective byte array (`to_bytes()`). This is then wired to both `std::hash<Endpoint>` and `boost::hash<Endpoint>` through `beast::uhash<>`, which defaults to `xxhasher`. The dual specialisation allows `Endpoint` to be used as a key in `std::unordered_map`, `boost::unordered_map`, or any container using `uhash` directly, covering the full range of hash-table variants used in the rippled codebase.

## Property Predicates

The free functions `is_loopback`, `is_unspecified`, `is_multicast`, `is_private`, and `is_public` are thin inline delegates that call the corresponding `Address`-level predicate. They exist so that call sites holding only an `Endpoint` do not need to extract the address first, keeping peer classification code concise. `is_private` and `is_public` branch internally on IPv4 vs IPv6 to apply the correct RFC-defined private range checks.