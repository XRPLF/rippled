# `STIssue` — Serialized Issue Type

## Role in the Protocol

`STIssue` is the canonical serialized representation of a fungible asset identifier on the XRP Ledger. It bridges two distinct layers of the codebase: the protocol's polymorphic `STBase` serialization framework (the "ST" in its name stands for Serialized Type) and the `Asset` abstraction that unifies the ledger's three asset species — native XRP, IOU tokens, and Multi-Purpose Tokens (MPT). Any time an asset identifier must be embedded in a ledger object or transaction field and travel through the binary codec or JSON API, it passes through `STIssue`.

## What It Wraps

The core data member is a single `Asset asset_` initialized to `xrpIssue()` by default. `Asset` is itself a thin `std::variant<Issue, MPTIssue>`: `Issue` encodes either XRP (currency code = zero) or an IOU (currency code + issuer AccountID), while `MPTIssue` encodes an MPT issuance identified by its 192-bit `MPTID`. This layering means `STIssue` is simultaneously aware of all three asset classes without duplicating their semantics — it delegates all asset-level logic to `Asset` and only adds the serialization machinery required by the `STBase` contract.

## Construction and Validation

Four constructors cover the expected usage patterns. The default constructor produces an XRP `STIssue`. The `SField`-only constructor creates an XRP `STIssue` tagged to a specific protocol field name, which is the typical way fields are named within a parent `STObject`. The templated constructor accepts any type satisfying the `AssetType` concept (`Issue`, `MPTIssue`, `MPTID`, or `Asset`) and performs an explicit consistency check via `isConsistent()` for the `Issue` case — throwing `std::runtime_error` if the currency/account native flag combination is invalid (e.g., XRP currency paired with a non-XRP account). This invariant is the constructor's primary defensive responsibility; MPT issuances are always considered consistent.

The deserialization constructor `STIssue(SerialIter&, SField const&)` implements an important disambiguation protocol in the binary format. It reads a 160-bit field first. If that field is the XRP currency code (all zeros), the asset is XRP and deserialization is complete. Otherwise it reads a second 160-bit field:

- If that second field is the **`noAccount()` sentinel** (the "black hole" address), the asset is an MPT. A 32-bit sequence number follows, and these three values are assembled into an `MPTID` via `memcpy`.
- Otherwise, the two fields form the `(currency, account)` pair of an IOU `Issue`, again verified with `isConsistent()`.

This design embeds the asset type discriminator directly in the wire format without a separate tag byte, relying on the `noAccount()` sentinel as an otherwise-illegal value that would never appear as a real IOU issuer.

## Type-Safe Access

`holds<TIss>()` and `get<TIss>()` expose the same tagged-access pattern as `Asset`, constrained by the `ValidIssueType` concept to only `Issue` or `MPTIssue`. `holds<TIss>()` delegates to `asset_.holds<TIss>()`. `get<TIss>()` throws `std::runtime_error` if the underlying variant does not hold the requested type before returning the typed reference. The raw `Asset` is also accessible via `value()`, which is `noexcept` and returns a `const` reference.

`setIssue()` allows mutation of the underlying asset while re-running the consistency check. However, the check is guarded by `holds<Issue>()` — only IOU issues are validated on set (MPT assignments always pass through), mirroring the constructor logic.

## Serialization

`add(Serializer& s)` serializes by visiting the `Asset` variant. XRP writes only its 160-bit currency code; IOUs write both the currency code and the issuer AccountID; MPTs write the issuer account, the `noAccount()` sentinel, and the 32-bit sequence extracted from the `MPTID`. This produces variable-length output (160, 320, or 352 bits) depending on asset type.

`getSType()` returns `STI_ISSUE`, identifying this type in the protocol's type registry. `isDefault()` returns `true` only when the asset is exactly `xrpIssue()`, consistent with the field's default value being XRP — MPT issuances are never treated as defaults.

## Comparison and JSON

Four comparison operators are defined as `constexpr friend` functions. Two compare `STIssue` to `STIssue` and two compare `STIssue` directly to `Asset`, which avoids the need to unwrap the type manually when comparing against raw `Asset` values elsewhere in the engine. All comparisons delegate entirely to `Asset`'s operators, which in turn use `std::visit` across the variant to handle cross-type ordering (where `Issue` sorts after `MPTIssue`).

`getJson(JsonOptions)` and `getText()` both forward to `Asset`'s equivalent accessors. The free function `issueFromJson(SField const&, Json::Value const&)` constructs an `STIssue` by parsing the JSON into an `Asset` via `assetFromJson()` and wrapping it, providing the canonical entry point when deserializing a ledger object's issue field from an API request.

## Infrastructure Details

`STIssue` inherits `CountedObject<STIssue>` alongside `STBase`, which instruments construction and destruction for runtime diagnostics. The class is declared `final`, preventing inheritance — the `STBase` hierarchy's polymorphism is already fully satisfied without extension. The private `copy()` and `move()` overrides use `STBase::emplace()`, a placement-new helper that places the object into a caller-supplied buffer when it fits, or heap-allocates otherwise. This pattern is used by `detail::STVar` (which is a `friend`) to efficiently store `STBase` subtypes within fixed-size inline storage.