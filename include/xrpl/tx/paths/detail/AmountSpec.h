/**
 * @file AmountSpec.h
 * @brief Include-compatibility shim for the legacy `AmountSpec` / `EitherAmount` types.
 *
 * This file is intentionally empty. It once defined two structs central to the
 * multi-currency path-payment engine:
 *
 * - **`AmountSpec`** — a tagged union pairing an `XRPAmount` or `IOUAmount`
 *   with optional issuer and currency metadata, distinguishing the two cases
 *   via a `bool native` flag. It acted as a richly-annotated amount description
 *   that carried both the numeric value and the asset denomination in a single
 *   object.
 *
 * - **`EitherAmount`** — a lighter companion union used at `Step` interface
 *   boundaries where asset-type metadata was already implicit in the step's
 *   template parameters.
 *
 * Both structs were overhauled when MPT (Multi-Purpose Token) support was
 * introduced (commit `dfcad6915`):
 *
 * - `EitherAmount` was extracted to `EitherAmount.h` and reimplemented as
 *   `std::variant<XRPAmount, IOUAmount, MPTAmount>`, constrained by the
 *   `StepAmount` concept from `protocol/Concepts.h`. The raw `union` + `bool`
 *   pattern was replaced with a type-safe, three-way discriminated union.
 *
 * - `AmountSpec` was retired. Its role is now fulfilled by the type system
 *   directly: `Step` subclasses are templated on `TIn`/`TOut` (both constrained
 *   to `StepAmount`), and the `Asset` / `Issue` / `MPTIssue` hierarchy carries
 *   issuer and denomination identity without a wrapper struct.
 *
 * The file is kept as an empty stub rather than removed because `StrandFlow.h`
 * and `FlowDebugInfo.h` both `#include` it. Removing it would be a breaking
 * change for any out-of-tree code that transitively relied on the inclusion.
 *
 * @note Any code that `#include`s this header directly should instead include:
 *   - `xrpl/tx/paths/detail/EitherAmount.h` for the `EitherAmount` type, or
 *   - `xrpl/protocol/XRPAmount.h`, `xrpl/protocol/IOUAmount.h`,
 *     `xrpl/protocol/MPTAmount.h` for the concrete amount types.
 */
