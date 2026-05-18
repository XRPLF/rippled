# `include/xrpl/tx/paths/detail/AmountSpec.h`

This file is currently **empty** — it contains no declarations, definitions, or preprocessor directives.

## Historical Context

The file's name and its position within the payment-flow subsystem (`xrpl/tx/paths/detail/`) reveal that it once held two important structs central to XRPL's multi-currency path-payment engine. In older revisions (visible in git history up to commit `29e49abd3`), `AmountSpec.h` defined:

- **`AmountSpec`** — a manual tagged union holding either an `XRPAmount` or an `IOUAmount`, distinguished by a `bool native` flag, plus optional `issuer` and `currency` metadata. This acted as a richly annotated amount description: it could represent both the numeric value *and* the asset denomination together, making it useful when a step needed to know not just "how much" but "of what."

- **`EitherAmount`** — a lighter companion union also differentiating XRP from IOU amounts, used at the `Step` interface boundary where type metadata was already implicit in the step's template parameters.

## What Replaced This File

The introduction of MPT (Multi-Purpose Token) support (`feat: Add MPT support to DEX`, commit `dfcad6915`) drove a structural refactor. Both structs were overhauled:

- `EitherAmount` was extracted to its own file (`EitherAmount.h`) and reimplemented using `std::variant<XRPAmount, IOUAmount, MPTAmount>` constrained by the `StepAmount` concept from `protocol/Concepts.h`. The raw `union` + `bool` pattern was replaced with a type-safe, three-way discriminated union, and `#ifndef NDEBUG` guards on the `native` flag were eliminated entirely.

- `AmountSpec` was retired. Its role — pairing a numeric value with issuer/currency identity — is now handled through the type system directly: `Step` subclasses are templated on `TIn`/`TOut` (both constrained to `StepAmount`), and the `Asset` / `Issue` / `MPTIssue` hierarchy carries issuer and denomination identity without a wrapper struct.

## Why the File Remains

`AmountSpec.h` is still `#include`d by two sibling headers, `StrandFlow.h` and `FlowDebugInfo.h`, both of which included it when it was substantive. Rather than removing the `#include` directives — a change that could break any out-of-tree code that transitively relied on the inclusion — the file was left as an empty stub. It compiles harmlessly, introducing no symbols, and serves purely as an include compatibility shim.

Any code that `#include`s `AmountSpec.h` directly should instead include `EitherAmount.h` for the `EitherAmount` type, or the appropriate protocol headers (`IOUAmount.h`, `XRPAmount.h`, `MPTAmount.h`) for concrete amount types.