# ProtocolVersion.cpp — Overlay Protocol Version Negotiation

This file implements the protocol version negotiation machinery used when two XRPL nodes establish a peer connection over the overlay network. During the HTTP upgrade handshake that initiates a peer session, each node advertises which protocol versions it supports, and the two sides must agree on the highest mutually supported version. This file owns the entire lifecycle of that negotiation: declaring what the local node supports, advertising it in HTTP headers, parsing what a peer sends, and picking the winner.

## The Supported Protocol Table

The canonical source of truth is `supportedProtocolList`, a `constexpr` array of `ProtocolVersion` pairs (currently `{2,1}` and `{2,2}`). The design choice to make this a compile-time constant rather than a runtime configuration is intentional — the set of versions a given binary speaks is a fixed property of that build, not something that should vary between runs or be configurable.

The compile-time `static_assert` that immediately follows the declaration is a notable defensive pattern. Because `std::is_sorted` is not `constexpr`-compatible prior to C++20, the code rolls its own loop inside a lambda to verify that the list is strictly ascending and non-empty. If a developer accidentally adds a duplicate or out-of-order entry, the build fails with a clear message. The comment explicitly notes this can be simplified once the codebase moves to C++20.

## Parsing: Defense in Depth

`parseProtocolVersions()` accepts an RFC 2616-style comma-separated header value (the HTTP `Upgrade` header) and returns only those tokens that are valid protocol version strings. The parsing applies three layers of validation before accepting any entry:

1. **Regex format check** — a `boost::regex` compiled once as a static local validates the exact lexical form: `XRPL/` prefix, a major version that is either a single digit `2–9` or a multi-digit number with no leading zeroes, a literal `.`, and a minor version that is either `0` or a non-zero-leading number. This also implicitly rejects the legacy `RTXP/x.y` format used by older nodes.

2. **Lexical cast** — even after the regex passes, `beast::lexicalCastChecked` converts each captured group to `uint16_t`. This guards against numbers that are syntactically valid but would overflow a 16-bit integer (e.g., `XRPL/99999.0` passes the regex but fails the cast).

3. **Round-trip sanity check** — after constructing the `ProtocolVersion` struct via `make_protocol()`, the code converts it back to string with `to_string()` and verifies it equals the original token. This eliminates any edge case where the regex or integer conversion could produce a value whose canonical string form differs from the input — for instance catching phantom discrepancies introduced by leading zeros that somehow survived earlier checks.

After collection, the result is sorted and deduplicated. This guarantees the contract stated in the header comment: the returned vector is always in strictly ascending order with no duplicates, which is a prerequisite for the set-intersection logic in `negotiateProtocolVersion`.

## Negotiation: Highest Common Version

`negotiateProtocolVersion()` is overloaded. The string-view overload delegates to `parseProtocolVersions` first; the vector overload contains the actual logic. The goal is to find the highest version present in both the peer's list and the local `supportedProtocolList`.

The implementation uses `std::set_intersection` with a `boost::make_function_output_iterator` wrapping a lambda that simply overwrites a single `std::optional<ProtocolVersion>` with each item it receives. Because `set_intersection` produces output in sorted order, the final value written into the optional is always the greatest element in the intersection. This avoids allocating a temporary container just to read `back()` — an elegant pattern that trades readability for allocation efficiency.

If no intersection exists (the peer speaks only versions this node doesn't recognize), the optional remains empty, and the caller can close the connection.

## Integration with the Handshake

Looking at `Handshake.cpp`, the outbound connection path calls `supportedProtocolVersions()` to populate the HTTP `Upgrade` header before sending the connection request. `supportedProtocolVersions()` returns a reference to a `static` `std::string` that is built exactly once via a lazy initializer — iterating `supportedProtocolList` and joining the entries as `"XRPL/2.1, XRPL/2.2"`. This avoids repeated string allocation on every outgoing handshake.

In `ConnectAttempt.cpp`, when processing the peer's HTTP 101 response, the code takes a different path than `negotiateProtocolVersion`: it explicitly calls `parseProtocolVersions` on the `Upgrade` header of the response, checks that the peer selected exactly one version, and then calls `isProtocolSupported()` to verify that the peer's selection is actually in the local table. This asymmetry exists because in the upgrade response the peer selects a single version; using `negotiateProtocolVersion` here would silently accept a fraudulently advertised version that the local node doesn't actually speak. `isProtocolSupported()` performs a linear search through the small `supportedProtocolList` array — entirely appropriate given the list will never have more than a handful of entries.

The overall design enforces a strict separation between what is acceptable to send (the static table) and what might arrive from untrusted peers (the parser), with the negotiation logic operating in the clean, validated domain of sorted vectors.