# `STAmount.h` — Unified Serializable Amount for XRP, IOU, and MPT

`STAmount` is the canonical on-ledger amount type in the XRPL codebase. It unifies three fundamentally different quantity representations — XRP drops, IOU floating-point amounts, and Multi-Purpose Token (MPT) integer amounts — behind a single serializable interface that plugs into the ledger's typed-field system via `STBase`. Almost every transaction field that names a quantity (`Amount`, `Fee`, `SendMax`, etc.) resolves to an `STAmount`.

## Internal Representation

The class stores four private members: `mAsset` (an `Asset` variant holding either an `Issue` or `MPTIssue`), an unsigned 64-bit `mValue`, a signed integer `mOffset`, and a boolean `mIsNegative`. The meaning of these fields depends on the asset type.

For **IOU amounts**, value is stored as a normalized scientific notation: `amount = mValue × 10^mOffset`. The mantissa (`mValue`) is constrained to `[cMinValue, cMaxValue]` which is `[10^15, 10^16 − 1]`, and the exponent (`mOffset`) is constrained to `[-96, +80]`. Zero is a special case encoded as `mValue = 0, mOffset = -100`; the −100 sentinel is deliberately chosen so that zero sorts below any small positive IOU whose exponent is large-negative. This 15-digit decimal mantissa gives roughly 16 significant figures of precision over a range spanning 176 orders of magnitude.

For **XRP and MPT** (collectively called "integral" types via `Asset::integral()`), `mOffset` is always 0 and `mValue` directly holds the raw drop or token count. The runtime enforces that XRP does not exceed `cMaxNativeN` (10^17 drops = 100 million XRP) and that MPT does not exceed the 63-bit signed maximum.

## Wire Encoding

The serialization format encodes the amount into a compact 64-bit word (plus trailing bytes for the currency/issuer identifiers if IOU, or a 192-bit MPTID if MPT). The flag bits in the high positions distinguish the three types:

- Bit 63 = 0 → native (XRP or MPT); bit 61 further distinguishes XRP from MPT.
- Bit 63 = 1 → issued currency (IOU).
- Bit 62 encodes the sign (1 = positive) for IOU and MPT.
- For IOU, the next 8 bits encode `mOffset + 97` (shifting into a non-negative range of [0, 255]), and the low 54 bits hold the mantissa.

The constants `cIssuedCurrency`, `cPositive`, `cMPToken`, and `cValueMask` are masks for picking these fields apart. The deserialization constructor (`STAmount(SerialIter&, SField const&)`) performs the full inverse decode including a throw on invalid wire forms (negative zero, mantissa out of range, invalid currency/account).

## The `unchecked` Tag and `canonicalize()`

Every public constructor that sets `mValue`/`mOffset` ends by calling `canonicalize()`, which normalises the mantissa into the `[cMinValue, cMaxValue]` band by repeatedly scaling up or down and adjusting the exponent. It throws `std::runtime_error` on overflow and silently truncates subnormals to zero.

An `unchecked` nested tag struct provides a bypass: constructors tagged `unchecked` set the four fields verbatim without calling `canonicalize()`. This path exists for performance-sensitive code (e.g., reading a quantity from a known-good source, or inside arithmetic helpers that already maintain invariants). Callers must guarantee the representation is already canonical; the public-interface constructors should always be preferred.

## Amendment-Gated Arithmetic Path

`canonicalize()` and the addition operator branch on `getSTNumberSwitchover()`, which reflects whether the `SingleAssetVault` or `LendingProtocol` amendments are active. When enabled, all arithmetic is routed through the `Number` class — a high-precision floating-point type with a "large" mantissa range ([10^18, 10^19 − 1]) that can exactly represent the full int64 range needed by XRP and MPT. On the legacy path, IOU normalization is done with direct 64-bit scaling loops. This dual-path design is how XRPL maintains amendment-gated numerical behaviour: old ledger rules produce identical results to legacy code, while newer amendments unlock higher-precision arithmetic. The `NumberSO` and `NumberMantissaScaleGuard` RAII helpers manage the thread-local switchover state, preventing test interference.

## `Asset` Polymorphism

`STAmount` delegates asset-type queries entirely to the `Asset` variant member. `holds<TIss>()` and `get<TIss>()` are thin template forwarders to `Asset::holds<TIss>()` and `Asset::get<TIss>()`, where `TIss` is constrained by the `ValidIssueType` concept to either `Issue` or `MPTIssue`. `native()` returns true only for XRP. `integral()` returns true for both XRP and MPT. The `operator Number()` conversion dispatches via `Asset::visit()` to the appropriate extraction method (`xrp()`, `iou()`, or `mpt()`), each of which re-materialises the correctly typed amount object and throws if called on the wrong variant.

## Arithmetic and Rounding

The free functions `multiply`, `divide` and their rounding variants accept two `STAmount` operands and a result `Asset`, rather than operating in-place. This design is intentional: cross-currency math (e.g., quality calculations) naturally produces a result in a third currency, and the caller must specify which. Rounding variants come in two flavours: `mulRound`/`divRound` use the legacy fixed-mode approach, while `mulRoundStrict`/`divRoundStrict` honour the current thread-local `Number::rounding_mode` more precisely. The `getRate` function encodes the offer quality (in/out ratio) as a single `uint64_t` with the exponent packed in the high byte and mantissa in the low bits, suitable for offer-book ordering where a lower rate benefits the taker.

The `roundToScale` function rounds an `STAmount` to a given decimal exponent, shedding precision beyond a reference scale. The two `roundToAsset` overloads (one in-place, one returning `Number`) apply asset-appropriate rounding: integral types (XRP, MPT) have their fractional parts dropped by `canonicalize()`; IOU types additionally call `roundToScale` to prevent accumulation of sub-precision dust.

## Safety Pre-flight Checks

`canAdd` and `canSubtract` provide pre-flight arithmetic safety checks without attempting the operation. For XRP and MPT, these perform 64-bit overflow/underflow bounds tests. For IOU, `canAdd` uses a relative-precision metric: it reconstructs both operands after round-tripping through addition and checks that the combined relative error does not exceed `10^-4`. This guards against silently losing significant digits when adding amounts whose exponents differ by more than 15.

## Serialization and JSON

`STAmount` satisfies the full `STBase` contract: `getSType()` returns `STI_AMOUNT`, `add()` writes the wire-format bytes to a `Serializer`, `getJson()` produces either a plain string (for XRP) or a `{value, currency, issuer}` / `{value, mpt_issuance_id}` object (for IOU/MPT). The free function `amountFromJson` parses all three formats — object, array, and slash-delimited string — accepting the ledger's historical string-based representation of XRP alongside the structured IOU and MPT formats. `amountFromJsonNoThrow` wraps this in a non-throwing overload for contexts where invalid input should produce a boolean result rather than an exception. The `Json::getOrThrow` specialisation in the `Json` namespace lets template code extract an `STAmount` from a JSON object by field name uniformly with other serialized types.