# `src/libxrpl/tx/paths/PaySteps.cpp`

## Role in the System

`PaySteps.cpp` is the strand construction layer of XRPL's payment engine. It bridges the gap between the raw payment path data embedded in a transaction — a user-supplied `STPathSet` of account and offer-book waypoints — and the executable `Strand` representation that the inner flow engine consumes. A `Strand` is a `std::vector<std::unique_ptr<Step>>`, where each `Step` is a polymorphic object encoding one hop's worth of liquidity: an account-to-account IOU transfer, an order book crossing, or an XRP/MPT endpoint.

The file's three public interfaces (`toStep`, `toStrand`, `toStrands`) form the construction pipeline. `Flow.cpp` calls `toStrands()` during payment setup; that function calls `toStrand()` per path; which calls the file-local `toStep()` per element pair. Everything below `toStrands()` is internal to this pipeline.

## Path Element Pairs and the `toStep()` Dispatch

`toStep()` is the fundamental factory dispatcher. It receives two adjacent normalized path elements (`e1`, `e2`) and the asset currently flowing through the strand (`curAsset`), and returns a newly allocated `Step`. The dispatch logic encodes every valid hop topology in the XRPL protocol:

- **XRP endpoint**: `e1` is an account with an XRP currency flag and `ctx.isFirst` is set → `make_XRPEndpointStep`. Similarly, when `e1` is the XRP bridge account and `e2` is a regular account on the last hop → another XRP endpoint. These handle the source-side and destination-side XRP terminals.
- **MPT endpoint**: When both `e1` and `e2` are accounts and `curAsset` holds an `MPTIssue` → `make_MPTEndpointStep`. The comments document three sub-cases: direct issuer↔holder payment, inter-holder payment (two steps: holder→issuer→holder1), and cross-token payments where the MPT endpoint appears as the first or last step only.
- **IOU direct step**: When both elements are accounts and `curAsset` holds an `Issue` → `make_DirectStepI`. This is the rippling primitive — two accounts sharing a trust line.
- **Book steps**: When `e2` is an offer node, `toStep` inspects the `curAsset`/`outAsset` combination and dispatches one of eight book step factories: `make_BookStepII`, `make_BookStepIX`, `make_BookStepXI`, `make_BookStepXM`, `make_BookStepMX`, `make_BookStepMI`, `make_BookStepIM`, or `make_BookStepMM`. The naming convention encodes the input/output pair: `I` for IOU, `X` for XRP, `M` for MPT.

One path topology is explicitly rejected: an XRP→XRP offer book hop. That combination — `isXRP(curAsset) && outAsset.isXRP()` — has no valid interpretation and returns `temBAD_PATH`.

## Path Normalization in `toStrand()`

`toStrand()` is the core of this file. Its first task is upfront validation of every element in the raw `STPath`: element types must be valid bit-flag combinations, account nodes cannot carry issuer/currency flags simultaneously, XRP cannot appear as an explicit issuer, and MPT nodes cannot appear next to account-only nodes (MPT has no rippling semantics). Violating any of these returns `temBAD_PATH` immediately.

After validation, the raw path is expanded into `normPath` — a fully explicit sequence of path elements with all implied nodes inserted:

1. **Source endpoint**: Always the first element, typed with `typeAccount | typeIssuer | typeCurrency` (or `typeMPT`). For IOU, the issuer in this implied element is set to `src` itself, representing the sender's own trust line.
2. **Implied SendMax issuer**: If `sendMaxAsset` is specified and its issuer differs from `src`, and the path doesn't begin with an account that *is* that issuer, an extra account node for the SendMax issuer is inserted. This is the "push the IOU to its issuer before crossing" step.
3. **User-supplied path elements**: Copied verbatim into `normPath`.
4. **Implied deliver asset node**: If the last asset in `normPath` doesn't match `deliver` (or, during offer crossing, the issuer differs), an offer node specifying `deliver` is appended. This ensures the path ends with the correct asset type.
5. **Implied deliver issuer**: If the deliver issuer is neither already the last node's account nor the destination itself, an account node for the deliver issuer is inserted. This represents the "pull the IOU from its issuer" step.
6. **Destination endpoint**: Appended unconditionally if the last node isn't already `dst`.

This normalization is the reason `toStep()` never has to reason about missing intermediate accounts — by the time elements reach it, the path is fully explicit.

## Iterating Pairs and Tracking `curAsset`

The main loop processes `normPath` as overlapping pairs `(normPath[i], normPath[i+1])`. A critical subtlety governs offer nodes: when `cur` is an offer and `next` is an account, `continue` skips creating a new step because the step was already created for the offer at the *previous* iteration — offer steps are created when `e2` is an offer, not when `e1` is one.

`curAsset` tracks the asset flowing through the strand as the loop advances. For `Issue` assets, the `.account` field updates at each account node to reflect the current holder (essential for determining whose trust line is in play). For MPT assets, the account embedded in the `MPTID` is immutable, so only the MPTID itself is tracked. The transition from MPT to IOU is handled explicitly: when a `cur` node carries a currency flag and `curAsset` holds an `MPTIssue`, `curAsset` resets to a bare `Issue{}` before the currency is applied.

Two blocks in the loop — handling implied accounts before offers and between pairs of account nodes — are noted in comments as dead code: because `curAsset` always tracks the current account via the normalization above, the issuer is never mismatched at this point. The blocks are retained as defensive fallbacks.

## Duplicate Detection via `StrandContext`

Each call to `toStep()` receives a freshly constructed `StrandContext` that carries mutable references to two accumulating sets: `seenDirectAssets` (a two-element flat_set array, indexed 0 for source-side, 1 for destination-side) and `seenBookOuts` (a flat_set of output assets). These are passed into the step factory constructors where each step can detect and reject loops — an account appearing twice in the same currency on the same side of a direct step, or an offer book producing the same output asset twice in the same strand. The two-slot design for direct assets allows the same account to appear once as a source and once as a destination (a legitimate two-hop pattern) without triggering the loop guard.

`StrandContext` is also the carrier for strand position metadata: `isFirst` (derived as `strand_.empty()`) and `isLast` (passed by the caller), which step factories use to decide endpoint behaviour and fee applicability.

## Post-Construction Verification

After the strand is fully assembled, `checkStrand()` performs an invariant walk. It traces account continuity — the output account of step N must equal the input account of step N+1 — and verifies that the final asset matches `deliver`. For book steps, it validates that the incoming asset matches the book's input side and advances `curAsset` to the book's output. The entire function is wrapped in an `UNREACHABLE` guard: if it fires, there is a logic error in the normalization or dispatch code above. All branches that reach it are excluded from coverage measurement (`LCOV_EXCL_START`).

## `toStrands()` and Error Policy

`toStrands()` wraps `toStrand()` across an entire `STPathSet`. Its error handling has a deliberate asymmetry: `temBAD_PATH` (a malformed transaction error) aborts the entire operation immediately regardless of other paths, because it indicates a protocol-level mistake the sender must fix. A non-`tem` failure on an individual path (e.g., a liquidity or trust-line error) is recorded as `lastFailTer` but does not abort — the remaining paths are still attempted. If at least one path succeeds, `toStrands()` returns `tesSUCCESS` with whatever strands were built. Only when *all* paths fail does the last failure code propagate.

Duplicate strand deduplication is done with a linear scan (`std::find` with `operator==`). Since path count is bounded by the protocol (eight paths maximum), this is acceptable despite the O(n²) nature; strand equality uses the per-step `equal()` virtual, which compares accounts, assets, and book keys.

## `checkNear()` and Floating-Point Tolerance

`checkNear()` answers whether two `IOUAmount` values should be considered equal. IOU amounts use a mantissa+exponent representation capable of expressing values across many orders of magnitude. The function permits a relative tolerance of 0.1% (`ratTol = 0.001`) and requires that exponents differ by at most 1. When the actual amount's exponent is below −20, any expected value is accepted — rounding at extreme scale makes exact equality meaningless. XRP and MPT counterparts (inlined in `Steps.h`) use exact integer equality, since those amount types are fixed-point without the floating-point accumulation behaviour of IOUs.