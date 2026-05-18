# `FlowDebugInfo.h` — Payment Flow Diagnostics

**Location:** `include/xrpl/tx/paths/detail/FlowDebugInfo.h`  
**Namespace:** `xrpl::path::detail`

## Role and Purpose

This header defines the diagnostic scaffolding used to instrument payment flow execution in the XRPL pathfinding engine. It lives in the `detail` subdirectory of the paths module because it is not part of the public payment API — it exists purely to give engineers visibility into how the flow engine consumed liquidity during a single payment, and how long each phase took.

The file's contents divide into two concerns: the `FlowDebugInfo` struct (which accumulates telemetry during a single call to `flow()`), and a set of free functions (`balanceDiffs`, `balanceDiffsToString`, `writeDiffElement`, `writeDiffs`) that snapshot `PaymentSandbox` balance changes for post-hoc analysis.

## `FlowDebugInfo` — Per-Payment Telemetry

`FlowDebugInfo` is constructed once per payment execution and threaded down the call stack as a raw pointer. The `flow()` entry point in `Flow.cpp` accepts `path::detail::FlowDebugInfo* flowDebugInfo = nullptr`, making the entire instrumentation path opt-in with zero overhead when the pointer is null. `RippleCalc::rippleCalculate` currently passes `nullptr`, confirming that this data is never gathered in production consensus paths — it exists for testing, benchmarking, or developer tools.

The struct stores two flat maps (from `boost::container`) keyed by string tags: `timePoints` mapping tags to `(start, end)` timestamp pairs, and `counts` mapping tags to occurrence counts. Using `boost::container::flat_map` rather than `std::map` is a deliberate performance choice — both maps are reserved upfront (`reserve(16)`) and are accessed by short string keys, so the cache-friendly flat layout pays off at the small sizes typical in one payment execution.

### Timing with RAII: `timeBlock()`

The most architecturally interesting piece is `timeBlock(std::string name)`, which returns a local `Stopper` object. On construction, `Stopper` records `clock::now()` as both start and end of the tag's entry. On destruction, it overwrites the end with a fresh `clock::now()`. This RAII pattern means a caller can write:

```cpp
auto _ = flowDebugInfo->timeBlock("main");
// ... rest of function ...
```

and the duration is captured automatically when the scope exits. Using `std::chrono::high_resolution_clock` gives nanosecond-resolution timing suitable for profiling individual payment passes. The `Stopper` is move-constructible (required because it's returned by value) but not copy-constructible, and it stores a raw pointer back to the parent `FlowDebugInfo` — callers must ensure the parent outlives the stopper.

### Pass Tracking: `PassInfo`

The nested `PassInfo` struct records one data point per "liquidity pass" — each iteration of the outer loop in `StrandFlow.h` where the engine selects the best-quality strand and routes an increment of the payment through it. For every pass, `push_back()` records the `EitherAmount` consumed (`in`) and delivered (`out`), as well as how many strands remained active. This lets a developer reconstruct the exact sequence of incremental fills the engine performed to complete the payment.

Within each pass, `liquiditySrcIn` and `liquiditySrcOut` track per-strand contributions — `newLiquidityPass()` opens a new inner vector before each pass begins, and `pushLiquiditySrc()` appends the individual strand's amounts to that vector. The result is a nested structure: `liquiditySrcIn[pass][strand]` gives the amount consumed from each strand in each iteration.

`PassInfo` uses `nativeIn` and `nativeOut` boolean flags (set at construction and declared `const`) to record whether the payment's source and destination currencies are XRP. These flags govern how amounts are serialized in `to_string()` — XRP amounts call `get<XRPAmount>()` while IOU amounts call `get<IOUAmount>()`. The `EitherAmount` type (defined in `EitherAmount.h`) is a `std::variant<XRPAmount, IOUAmount, MPTAmount>`, so this branching is required for correct extraction.

### Serialization: `to_string()`

`to_string(bool writePassInfo)` always emits the total duration of the `"main"` timed block and the pass count. When `writePassInfo` is true, it additionally emits the full sequence of per-pass in/out amounts, active strand counts, and per-strand liquidity amounts in a bracket-and-semicolon delimited format designed for log parsing. The nested liquidity arrays use `|` as the inner delimiter and `;` as the outer, making them machine-readable without a full JSON parser.

### Latent Bug in `inc()`

`inc()` contains a subtle defect: when a tag is not yet in `counts`, it inserts `counts[tag] = 1` but then attempts `++i->second` using the pre-insertion iterator `i`, which for `flat_map`'s vector-backed storage is now invalid (insertion can relocate elements). This results in undefined behavior on first use of any new tag. Since the struct is used only in diagnostic code paths, this has not caused observable failures, but it is worth noting.

## Balance Diff Utilities

The free functions at the bottom of the file operate on `PaymentSandbox` snapshots to produce human-readable balance change reports. `balanceDiffs()` calls `sb.balanceChanges(rv)` and `sb.xrpDestroyed()`, bundling the IOU trust-line changes (keyed by `(account, account, currency)` tuples) and the net XRP burned into a `BalanceDiffs` pair. `balanceDiffsToString()` wraps this in an `optional` and serializes it using `writeDiffs()`, which iterates over the map and calls `writeDiffElement()` for each entry. Each element is formatted as `[sender|receiver|currency|amount]`, providing a compact audit trail of every trust-line mutation the payment caused.

## Relationship to Other Files

- **`StrandFlow.h`** is the primary consumer: it calls `newLiquidityPass()`, `pushLiquiditySrc()`, and `pushPass()` inside the liquidity-selection loop, always guarded by `if (flowDebugInfo)` null checks.
- **`Flow.cpp`** receives `FlowDebugInfo*` as a parameter and passes it through to `StrandFlow.h`'s templated flow function.
- **`RippleCalc.cpp`** is the top-level caller and currently always passes `nullptr`, meaning production payments collect no telemetry.
- **`EitherAmount.h`** provides the `EitherAmount` variant type that `PassInfo` uses throughout — its `get<T>()` method throws `std::logic_error` if the wrong type is requested, which the `to_string()` code avoids by checking `nativeIn`/`nativeOut` before dispatching.
- **`PaymentSandbox.h`** provides the `balanceChanges()` and `xrpDestroyed()` methods used by `balanceDiffs()`.