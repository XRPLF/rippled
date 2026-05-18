# `StrandFlow.h` — Core Payment Flow Execution Engine

This header is the heart of the XRPL payment engine. It implements the two-pass strand execution algorithm and the outer multi-strand search loop that together convert a set of abstract payment paths into concrete ledger mutations. Every XRP payment — whether a direct transfer, a cross-currency payment, or an offer cross — is ultimately executed through the functions defined here.

## Conceptual Model

A **strand** is an ordered list of `Step` objects describing one route through the ledger: a sequence of accounts to ripple through (direct IOU hops) and/or order books to consume. The job of `StrandFlow.h` is to determine, for a requested output amount, how much input a given strand consumes and what state changes it produces — all inside a transactional `PaymentSandbox` that can be rolled back or merged.

## `StrandResult` — Strand Execution Output

`StrandResult<TInAmt, TOutAmt>` bundles everything produced by a single strand execution: the actual in/out amounts, a moved `PaymentSandbox` holding the proposed ledger state, a set of offer IDs to delete (`ofrsToRm`), an offer-consumption count, and an `inactive` flag indicating the strand is exhausted. Two constructors handle the two outcomes: a successful execution (all fields populated) and a failed/dry execution (only the offer-removal set, so bad offers are cleaned up even on failure).

## The Two-Pass `flow()` for a Single Strand

The single-strand `flow<TInAmt, TOutAmt>()` implements a classical reverse-then-forward algorithm that is non-trivial to follow but elegant in purpose.

**Reverse pass (right to left):** Starting from the desired output, each step is called via `rev()` in reverse order. Each step reports how much input it needs to produce the requested output; that input amount becomes the "desired output" for the preceding step. This determines, without committing anything, whether the full `out` is achievable.

**Limiting step detection:** If any step cannot satisfy its requested output exactly, that step is the *limiting step* — the bottleneck. When found, the algorithm discards the partial sandbox (`sb.emplace(&baseView)`), re-executes just the limiting step with the capped amount, records that capped amount as `limitStepOut`, and continues the reverse pass from there leftward. The step at index 0 is treated specially: if it would exceed `maxIn`, it is re-executed forward (`fwd()`) with exactly `maxIn` rather than in reverse.

**Forward pass (left to right from the limiting step):** After the reverse pass establishes what every step to the left of the limiting step will do, the forward pass calls `fwd()` on every step to the right of the limiting step, threading the output of each step as the input to the next. This completes the sandbox state for steps that were not part of the reverse limiting adjustment. If any forward step produces zero output (a "dust" amount such as 10⁻⁸⁰ IOU into an XRP offer), the strand is abandoned.

A debug-only re-validation block (`#ifndef NDEBUG`) re-executes the entire strand forward using `validFwd()` to confirm the final cached values match expectations — a canary that detects inconsistencies in step implementations without affecting production performance.

The function short-circuits early for two edge cases: an empty strand (returns immediately) and a direct XRP-to-XRP strand detected via `isDirectXrpToXrp<TInAmt, TOutAmt>()`, which requires no execution at all. All exceptions of type `FlowException` are caught and converted to a dry result so a bad offer or overflow in one strand does not abort the entire payment.

## `FlowResult` — Multi-Strand Output

`FlowResult<TInAmt, TOutAmt>` accumulates the aggregate result across all strands: total in/out, the merged sandbox, removable offers, and a `TER` error code. Three constructors express success, failure with amounts, and failure without amounts — covering the different completion paths in the outer loop.

## `qualityUpperBound()` — Strand Quality Estimation

Before actually executing a strand, the engine needs to rank candidates. `qualityUpperBound()` computes an upper bound on a strand's exchange rate by calling `qualityUpperBound()` on each step in sequence and composing the results via `composed_quality()`. It propagates `DebtDirection` between steps (distinguishing issuance from redemption, which affects fees) and returns `std::nullopt` if any step is provably dry. This estimate may be optimistic — unfunded offers at the tip of a book can make the actual quality lower — but it serves as a sound ranking heuristic.

## `limitOut()` — AMM Quality-Function Optimization

When a payment has exactly one active strand and a `limitQuality` threshold is set, `limitOut()` can reduce the output request to the amount that exactly satisfies the quality constraint. This matters specifically for AMM-backed strands where quality is not constant: the AMM's spot price is a quadratic function of output, so a smaller output yields a better average quality. The function collects per-step `QualityFunction` objects and combines them into a single strand-level quality function, then solves for the output that achieves the limit quality via `qf->outFromAvgQ(limitQuality)`. A relative-distance guard (`withinRelativeDistance(..., 1e-9)`) absorbs floating-point rounding and avoids spurious adjustments. If the quality function is constant (no AMM steps), the function is a no-op.

## `ActiveStrands` — Lazy Strand Candidate Management

`ActiveStrands` tracks which strands are still eligible to provide liquidity. It maintains two sets: `cur_` (strands being evaluated in the current round) and `next_` (strands to evaluate next round, including any strand that still has liquidity after partial use).

`activateNext()` is called at the start of each outer iteration. It sorts `next_` by `qualityUpperBound` (best quality first) using a `stable_sort` — the stability is required for deterministic ordering across different C++ standard library implementations, which is critical for consensus. Strands whose quality falls below `limitQuality` are pruned here. The sorted result becomes `cur_` for the current round.

The probe-and-push pattern in the outer loop deserves attention: the loop iterates over `cur_` in quality order, calls the single-strand `flow()`, and takes the *first* strand that returns usable liquidity (`best`). All remaining unchecked strands in `cur_` are pushed back to `next_` via `pushRemainingCurToNext()`, and a non-exhausted `best` strand is also pushed back via `push()`. This means only one strand is consumed per outer iteration, ensuring that a high-quality strand offering partial liquidity is given priority in the next round while other strands remain in contention.

## The Outer Multi-Strand `flow()` Loop

The public-facing `flow<TInAmt, TOutAmt>()` for a vector of strands is the top-level payment loop. It tracks `remainingOut` and optionally `remainingIn` (from `sendMax`), iterating until both are satisfied, all strands are dry, or safety limits are hit.

**Safety limits:** `maxTries = 1000` bounds the total number of outer iterations; `maxOffersToConsider = 1500` bounds total offer consumption across all strands. Exceeding either returns `telFAILED_PROCESSING`. These limits prevent adversarial paths from causing unbounded ledger work.

**Precision:** Rather than accumulating a running total (which loses precision in floating-point IOU arithmetic), the engine collects each round's in/out amounts into `flat_multiset` containers (`savedIns`, `savedOuts`) and recomputes the total by summing smallest-to-largest via `std::accumulate`. `remainingOut` is then recomputed as `outReq - sum(savedOuts)` each round, preventing drift.

**Offer cleanup:** Bad offers (`ofrsToRm`) are deleted from the sandbox immediately via `offerDelete()` at the end of each iteration, even if the strand failed. A separate `ofrsToRmOnFail` set accumulates all offers to be removed if the payment ultimately fails — these are propagated back to the caller so the ledger can be cleaned up regardless of payment outcome.

**FillOrKill semantics:** The final section handles offer-crossing edge cases around the `fixFillOrKill` amendment. When crossing without `tfSell`, the engine must deliver the full `TakerPays`; when `tfSell` is set, the engine must consume the entire `TakerGets`. The logic branches on both the amendment flag and the `OfferCrossing` mode to return `tecPATH_PARTIAL` appropriately.

**AMM integration:** `AMMContext` is updated after each successful round via `ammContext.update()`, incrementing the AMM iteration counter if AMM liquidity was used. The `setMultiPath()` call before each round informs the AMM whether it is competing with other strands, which affects how it prices its virtual offers. The `ammContext.clear()` before each strand execution resets the per-strand used flag so a failure in one strand does not incorrectly mark the next strand as having consumed AMM liquidity.