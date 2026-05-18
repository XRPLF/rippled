# `include/xrpl/protocol/SystemParameters.h`

This header is the canonical home for protocol-wide constants and the validation helpers that depend on them. Everything here is either a fixed property of the XRP Ledger network (total XRP supply, earliest known ledger, governance thresholds) or a convenience function built directly on those values. The file is intentionally small: it avoids heavy dependencies and is included across virtually the entire codebase, so keeping it lightweight matters.

## Network Identity

`systemName()` and `systemCurrencyCode()` both use the Meyers singleton pattern — a function-local `static const` — to return the strings `"xrpld"` and `"XRP"` respectively. This avoids the static initialization order fiasco that would arise from file-scope globals, while the `inline` keyword lets each translation unit include the header without ODR violations. Neither function is performance-sensitive; they exist to give the rest of the codebase a single authoritative source for these names rather than scattering string literals.

## XRP Supply and Amount Validation

`INITIAL_XRP` represents the entire XRP supply at ledger genesis: 100 billion XRP expressed in drops (the smallest indivisible unit). The calculation `100'000'000'000 * DROPS_PER_XRP` where `DROPS_PER_XRP = 1'000'000` produces exactly 100,000,000,000,000,000 drops (10¹⁷). Two `static_assert`s verify this at compile time: the first confirms the raw bit value, and the second asserts that `Number::maxRep` — the upper bound of XRPL's internal arbitrary-precision scalar type — can hold this value. If anyone changes either `Number`'s representation or the XRP total, the build fails immediately rather than silently producing wrong results at runtime.

`isLegalAmount()` and `isLegalAmountSigned()` build directly on this constant. The unsigned variant simply checks `amount <= INITIAL_XRP`, while the signed variant extends the range to `[-INITIAL_XRP, INITIAL_XRP]` to accommodate deltas and fee calculations that may temporarily represent negative adjustments. Both appear in hot paths: `Transactor.cpp` calls `isLegalAmount` to reject transactions whose fee field would exceed the total XRP supply, and `InvariantCheck.cpp` uses them as post-transaction guards to ensure no ledger operation manufactures XRP out of thin air.

## Historical Sequence Boundaries

`XRP_LEDGER_EARLIEST_SEQ = 32570` encodes a historical accident of the XRP Ledger mainnet: ledgers 1 through 32569 were lost in an early incident and are no longer available anywhere. The `Database` class uses this as the default for the `earliest_seq` configuration parameter, clamping any node that does not configure a custom lower bound to refuse requests for pre-genesis sequences.

`XRP_LEDGER_EARLIEST_FEES = 562177` marks the first mainnet ledger that contains a `FeeSettings` object. This constant appears in `XRPL_ASSERT` calls scattered across `BuildLedger.cpp`, `LedgerPersistence.cpp`, `InboundLedger.cpp`, and `Application.cpp`, always in the form:

```cpp
XRPL_ASSERT(
    ledger->header().seq < XRP_LEDGER_EARLIEST_FEES || ledger->read(keylet::fees()),
    "...");
```

The pattern means: if we are processing a modern ledger, a `FeeSettings` entry must exist; if we are replaying very old ledgers (before the object was introduced), we skip the check. Without this boundary, asserting the invariant unconditionally would always fail for historical replay of early ledgers.

## Amendment Governance Constants

`amendmentMajorityCalcThreshold` is declared as `std::ratio<80, 100>` rather than the obvious `constexpr double threshold = 0.8`. The reason is precision: `AmendmentTable.cpp` computes the required support count with `(trustedValidations_ * amendmentMajorityCalcThreshold.num) / amendmentMajorityCalcThreshold.den`, performing the multiplication before the division using plain integer arithmetic. A floating-point constant would introduce rounding error in what is effectively a consensus-critical gate; an off-by-one in the vote count threshold could enable or block an amendment incorrectly.

`defaultAmendmentMajorityTime` is `weeks{2}` expressed as `std::chrono::seconds`. The `weeks` alias comes from `xrpl/basics/chrono.h` since the standard library did not provide it at the time (C++20 added it, but this alias predates that). This value seeds the `Config` field `AMENDMENT_MAJORITY_TIME`, which operators can override. An amendment must hold 80% validator support continuously for two weeks before it activates on mainnet.

## Peer Port

`DEFAULT_PEER_PORT = 2459` is the IANA-registered port for XRPL peer-to-peer connections. It lives **outside** the `xrpl` namespace, unlike every other constant in this file. This is intentional: networking code that constructs socket addresses often operates in a context where `xrpl::` namespace qualifications are absent, and the port value is generic enough to be treated as a plain system-level constant rather than a protocol abstraction. The `OverlayImpl` peer-discovery code and the `peer_connect` RPC handler both reference it directly as a fallback when no explicit port is given.