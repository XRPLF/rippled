# `include/xrpl/tx/paths/detail/Steps.h`

This header is the architectural backbone of XRPL's payment flow engine. It defines the `Step` polymorphic interface, the `Strand` type alias, the factory functions for every concrete step variant, and the context and exception types that tie the whole system together. Every transaction that moves value through more than one trust line or offer book — payments, offer crossings, AMM swaps — goes through these abstractions.

## The `Step` Abstract Interface

`Step` defines a bidirectional evaluation protocol. Payment paths on XRPL are resolved in two passes: a reverse pass determines how much input is needed to produce a desired output, and a forward pass confirms the output produced by a given input. This matches the two pure virtual methods:

- `rev(sb, afView, ofrsToRm, out)` — given a desired output amount, return the `(actual_in, actual_out)` pair that is achievable given current liquidity.
- `fwd(sb, afView, ofrsToRm, in)` — given an available input, return the `(actual_in, actual_out)` that the step can produce.

Both pass two views into the ledger: `sb` (`PaymentSandbox`) carries the *running* state as the strand executes, while `afView` holds the ledger state *before* the strand started. The distinction matters for offer funding checks — whether an offer was already unfunded before this payment started, or became unfunded because of it. Unfunded or error-state offers are collected in `ofrsToRm` for later deletion regardless of whether the payment succeeds.

Returning a pair rather than throwing allows the caller (`StrandFlow.h`) to detect the *limiting step* — the step where liquidity first runs out. That step is re-executed to establish the correct amounts, then the forward pass resumes from there. `cachedIn()` and `cachedOut()` expose the amounts stored by the most recent `rev()` call so the forward pass can seed itself from them.

## The `StepImp` CRTP Mixin

The five concrete step classes (`DirectStepI`, `BookStepII/IX/XI`, `XRPEndpointStep`, `MPTEndpointStep`, and MPT book variants) are typed in their input and output amounts — a `BookStepIX` always consumes `IOUAmount` and produces `XRPAmount`, for instance. At the strand level, however, everything must be type-erased to let `Step` hold them uniformly.

`EitherAmount` (from `EitherAmount.h`) provides the type erasure: it is a `std::variant<XRPAmount, IOUAmount, MPTAmount>` with a checked `get<T>()` accessor. The `StepAmount` concept (from `Concepts.h`) constrains template parameters to these three types.

`StepImp<TIn, TOut, TDerived>` bridges the gap via CRTP. It implements `rev()`, `fwd()`, `isZero()`, `equalOut()`, and `equalIn()` by unwrapping the variant (checked at each call boundary) and forwarding to the concrete class's `revImp()` and `fwdImp()` methods which receive strongly-typed amounts. The pattern avoids scattering variant unwrapping into every concrete class, and catches type mismatches at runtime with a clear `std::logic_error` rather than undefined behavior.

## Directionality Enums

`DebtDirection` (`issues` / `redeems`) is not merely metadata — it drives transfer fee logic. When an account *redeems* (receives back its own IOU to cancel debt), no transfer fee is assessed on that side of the hop; when an account *issues* new credit, a fee may apply. `debtDirection()` is queried by `qualityUpperBound()` at each step so the quality estimate reflects actual fee impacts. `QualityDirection` (`in`/`out`) and `StrandDirection` (`forward`/`reverse`) similarly parametrize how quality is computed depending on which pass is running.

## Quality Estimation and the AMM Extension

`qualityUpperBound(v, prevStepDir)` computes a theoretical best-case quality (out/in ratio) for each step, propagating the `DebtDirection` through the chain. The `StrandFlow.h` engine uses this to sort competing strands from best to worst before committing to any execution, enabling best-quality-first liquidity selection without executing expensive steps speculatively.

For AMM book steps, quality is not constant but a function of output amount (due to the constant-product pricing formula). `getQualityFunc()` provides the richer `QualityFunction` that encodes this non-linearity. All non-AMM steps inherit a default implementation that wraps `qualityUpperBound`'s result into a constant `QualityFunction{quality, CLOBLikeTag{}}`. This distinction is critical for the `limitOut()` optimization in `StrandFlow.h`: when a single strand contains AMM liquidity and a `limitQuality` constraint is active, the engine back-calculates the precise output required to exactly hit the quality limit, rather than executing to dryness and checking after the fact.

## Strand Construction

`Strand` is a `std::vector<std::unique_ptr<Step>>`. Building one requires two steps:

1. `normalizePath()` fills in implied nodes — XRPL allows callers to omit obvious intermediate accounts (e.g., the issuer of a currency) and this function inserts them, ensuring the path is unambiguous.
2. `toStrand()` iterates the normalized path and calls the appropriate `make_*` factory for each hop, threading `StrandContext` through each call. `toStrands()` applies this to an entire `STPathSet` plus an optional default (direct) path.

## `StrandContext`: Construction-Time Safety

`StrandContext` bundles all inputs needed to construct and validate a single step. Its two loop-detection sets are worth noting: `seenDirectAssets` (a two-element array of flat sets, tracking assets seen in direct hops at src and dst positions) and `seenBookOuts` (assets output by offer book hops). These enforce the invariant that a strand may not pass through the same account+currency node or output the same issue from two book steps — cycles that would allow value to circulate indefinitely.

`StrandContext` also carries `prevStep` so each factory can query the preceding step's `debtDirection()` when checking the `noRipple` constraint: a path that enters and exits an account through two trust lines both marked `noRipple` must be rejected.

## `FlowException` and Error Handling

`FlowException` wraps a `TER` in a `std::runtime_error`. It is the signal for truly unexpected failures inside a step — states that cannot be handled by returning a zero amount. The single-strand `flow()` in `StrandFlow.h` catches it and returns a failed `StrandResult`, which the multi-strand engine treats as a dry strand rather than propagating the exception further.

## `checkNear` and Numeric Precision

`checkNear()` is overloaded for `IOUAmount`, `XRPAmount`, and `MPTAmount`. For `IOUAmount` it applies a tolerance check — IOU arithmetic uses a floating-point mantissa/exponent representation where accumulated rounding can produce near-but-not-exactly-equal values. For XRP and MPT, which are 64-bit integers, it compares exactly. This asymmetry is exposed to concrete step implementations that validate their forward-pass results in debug builds.

## Factory Declarations

The `make_*` functions declared at the bottom of the file — `make_DirectStepI`, `make_BookStepII`, `make_BookStepIX`, `make_BookStepXI`, `make_XRPEndpointStep`, `make_MPTEndpointStep`, and the MPT/IOU cross-book variants — each return `std::pair<TER, std::unique_ptr<Step>>`. Their implementations live in `DirectStep.cpp`, `BookStep.cpp`, `XRPEndpointStep.cpp`, and `PaySteps.cpp`. The `test::` namespace helpers (`directStepEqual`, `bookStepEqual`, etc.) provide white-box inspection for unit tests without exposing internal state through the production API.

The `isDirectXrpToXrp<InAmt, OutAmt>()` template closes the file with a compile-time short-circuit: a two-step XRP→XRP strand is detected at instantiation time via `if constexpr`, and the flow engine skips executing it entirely since it cannot change value.