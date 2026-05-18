# `Issue.cpp` — Currency/Issuer Pair Implementation

## Role in the System

`Issue.cpp` implements the `Issue` class, which is the fundamental representation of a fungible asset on the XRP Ledger that isn't a Multi-Purpose Token (MPT). An `Issue` encodes exactly two things: a `Currency` (a 160-bit tagged hash, defined in `UintTypes.h`) and an `AccountID` (the issuing account). Every IOU balance, offer, and trust line in the ledger is denominated in some `Issue`. XRP itself is also modeled as a special-case `Issue` whose currency and account fields are both the zero/XRP sentinel values.

`Issue` sits at the centre of the protocol type hierarchy. The newer `Asset` type (`Asset.h`) is a `std::variant<Issue, MPTIssue>` that generalises across all three ledger asset kinds (XRP, IOU, MPT). `Issue` handles the first two. `MPTIssue` handles the third and shares the same interface contract — `getText()`, `setJson()`, `native()`, `integral()` — making static polymorphism possible without virtual dispatch.

## The XRP Sentinel Convention

XRP is not truly a separate type; it is an `Issue` whose `currency` field equals `beast::zero` (tested by `isXRP(currency)`). The matching `account` field is `xrpAccount()`, also the zero account. The `isConsistent()` free function enforces the critical invariant that both fields must agree: either both are the XRP sentinel or neither is. A cross-contamination (e.g., XRP currency with a real account, or a real currency with the XRP account sentinel) would silently corrupt amount comparisons and offer-book matching, so `isConsistent` acts as a sanity guard for newly constructed issues.

`Issue::native()` checks `*this == xrpIssue()` — a full equality comparison that respects the special equality semantics defined in the header: for XRP, the account field is ignored in the comparison (`isXRP(lhs.currency) || lhs.account == rhs.account`). This short-circuit is necessary because the zero currency uniquely identifies XRP without any account qualifier.

`Issue::integral()` delegates entirely to `native()`. For `Issue`, only XRP is integral (indivisible, stored as drops). This mirrors `MPTIssue::integral()` which always returns `true`, because MPT amounts are also integers. The shared method name enables generic code operating on either type to query precision behaviour without knowing which asset kind it holds.

## Serialisation: Two Different String Formats

The file exposes two string-rendering paths with subtly different shapes:

`getText()` produces `currency_string/account_string`, using compact sentinel substitutions: `isXRP(account)` becomes the literal `"0"` and `noAccount()` becomes `"1"`. This format is primarily for human-readable diagnostics and logging.

`to_string()` produces `account_string/currency_string` — the order is reversed — and only activates the slash form when the account is not the XRP sentinel. The asymmetry exists for historical reasons rooted in how offer-book keys and log lines have been formatted in the ledger engine for years; both formats are in active use in different parts of the codebase.

`setJson()` and its wrapper `to_json()` produce the canonical wire format: a JSON object with a `"currency"` string field and, for non-XRP issues, an `"issuer"` field encoded as Base58Check. XRP issues emit only `"currency"` — omitting `"issuer"` entirely — which is the authoritative representation in transaction JSON, RPC responses, and the binary codec.

## `issueFromJson`: Layered Validation at the Boundary

`issueFromJson()` is the only place in this file that accepts untrusted input, and it validates in strict order:

1. The input must be a JSON object, not a string or array.
2. The presence of `mpt_issuance_id` is rejected immediately — this field belongs to `MPTIssue` and its appearance signals that the caller has routed MPT data into the wrong parser. The explicit rejection prevents silent misinterpretation.
3. `"currency"` must be a JSON string, then must parse to something other than `badCurrency()` or `noCurrency()`. `badCurrency()` is the three-letter sequence `"XRP"` spelled out literally, which the ledger has historically forbidden as a currency code because it collides with native XRP. `noCurrency()` is returned by `to_currency()` on parse failure.
4. If the currency is XRP, the `"issuer"` field must be absent (`issStr.isNull()`). Providing an issuer for XRP is a protocol violation, and the error is thrown rather than silently ignored.
5. For non-XRP currencies, `"issuer"` must be a string and must decode via `parseBase58<AccountID>()`. Failure at any step throws either `std::runtime_error` or `Json::error` via the XRPL `Throw<>` macro, which ensures the exception carries file/line metadata for diagnostics.

The use of two distinct exception types is intentional: `Json::error` is thrown for malformed JSON values (wrong type, missing fields), while `std::runtime_error` guards against structural misuse (passing an MPT JSON blob, passing a non-object). Callers can catch the narrower type if they only care about format errors.

## Relationship to `MPTIssue`

`MPTIssue` (`MPTIssue.h`) mirrors the `Issue` interface — `getText()`, `setJson()`, `native()` (always `false`), `integral()` (always `true`) — specifically to satisfy the same concept constraints used by `Asset` and amount types. The deliberate boundary between `issueFromJson` (rejects `mpt_issuance_id`) and `mptIssueFromJson` (expects it) means deserialization always produces the correct concrete type and the `Asset` variant is populated correctly. There is no runtime dispatch or `dynamic_cast`; the type is known at the point of JSON parsing.