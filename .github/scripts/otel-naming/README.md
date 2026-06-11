# OTel naming-consistency check

`check_otel_naming.py` enforces the OpenTelemetry span-attribute naming
convention documented in
[CONTRIBUTING.md](../../../CONTRIBUTING.md#telemetry-span-attribute-naming)
across every layer of the telemetry pipeline. The `*SpanNames.h` constants are
the single source of truth (L1); every other layer must agree with them.

## Running locally

```
python .github/scripts/otel-naming/check_otel_naming.py
```

It takes no arguments, can be run from any directory inside the repo, and uses
only the Python standard library (no `pip install`, matching the levelization
check). A non-zero exit code means a violation was found; the output lists each
violation as `RULE | location | token | expected`.

## What it checks

The valid key set is **derived dynamically from the OTel code** — there is no
hardcoded allowlist:

- **L1 keys** come from the `namespace attr { ... }` blocks of every
  `*SpanNames.h`, resolving the `makeStr("x")` / `join(seg::a, seg::b)` DSL
  (cross-file, so `join(seg::rpc, ...)` resolves `seg::rpc` from the base
  `SpanNames.h`).
- **Legitimate dotted keys** = the resource attrs declared in the base
  `SpanNames.h` (`xrpl.network.*`) plus the `semconv::service::*` keys the code
  passes to `Resource::Create()` in `Telemetry.cpp` (`service.*`). A dotted key
  declared in any other header is a violation.

### Rules (each fails the build, when its inputs are present)

| Rule | Check                                                                                                                                                                                             |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A    | No stray dotted span-attribute key (only the derived resource keys may be dotted).                                                                                                                |
| G    | Attribute keys are `lower_snake_case` (`^[a-z][a-z0-9_]*$` per dot-segment) — no camelCase, UPPERCASE, or spaces.                                                                                 |
| F    | No string literals as attribute keys or span-name arguments in `setAttribute`/`addEvent`/`span`/`childSpan`. Attribute _values_ are exempt (runtime data). `*SpanNames.h` definitions are exempt. |
| B    | Every collector `spanmetrics.dimensions` name exists in the L1 key set.                                                                                                                           |
| C    | Every Tempo span-filter tag exists in the L1 key set.                                                                                                                                             |
| D    | Every dashboard PromQL label (non-builtin) exists in the L1 key set.                                                                                                                              |
| E    | Every runbook attribute reference exists in the L1 key set.                                                                                                                                       |

## Presence-gated

Every rule runs **only when the source files it needs are present** in the tree
and is otherwise skipped (printed as `SKIP: <rule> — <reason>`), never failed.
This keeps the check correct no matter how telemetry work is split across PRs —
a stacked chain, one large PR, or independent per-stage PRs where (for example)
the collector config lands before the dashboards. The collector/Tempo/dashboard/
runbook layers are introduced in later phases; on a branch without them, only
the L1-intrinsic rules (A, F) run.
