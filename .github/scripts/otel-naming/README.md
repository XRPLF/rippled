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
  `SpanNames.h`). Each constant is resolved against **its own** header, so two
  headers that define a same-named constant (e.g. a base `attr::ledgerHash` and
  a domain `attr::ledgerHash`) each contribute their real wire key — a later
  header cannot clobber an earlier one's value in a flat table.
- **Legitimate dotted keys** = ONLY the keys the code actually sets as resource
  attributes, i.e. the entries inside `Telemetry.cpp`'s `Resource::Create({...})`
  call: the `semconv::service::*` keys (`service.*`) plus any `attr::<name>`
  constants passed there (`xrpl.network.*`). A dotted key that is _declared_ in a
  header but never set as a resource attr is a span attribute in resource
  clothing — a Rule-A violation, even if it lives in the base `SpanNames.h`.

### Rules (each fails the build, when its inputs are present)

| Rule | Check                                                                                                                                                                                                                                                                                    |
| ---- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A    | No stray dotted span-attribute key (only the derived resource keys may be dotted).                                                                                                                                                                                                       |
| G    | Attribute keys are `lower_snake_case` (`^[a-z][a-z0-9_]*$` per dot-segment) — no camelCase, UPPERCASE, or spaces.                                                                                                                                                                        |
| F    | No string literals as attribute keys or span-name arguments in `setAttribute`/`addEvent`/`span`/`rootSpan`/`childSpan` (`rootSpan` shares `span`'s `(cat, prefix, name)` signature). Attribute _values_ are exempt (runtime data); `*SpanNames.h` definitions and test files are exempt. |
| B    | Every collector `spanmetrics.dimensions` name exists in the L1 key set.                                                                                                                                                                                                                  |
| C    | Every Tempo span-filter tag exists in the L1 key set.                                                                                                                                                                                                                                    |
| D    | Every dashboard label resolves to an L1 span attribute, a native-metric label (L6, emitted by MetricsRegistry), or a Prometheus/Grafana builtin. TraceQL scope prefixes (`span.`/`resource.`/…) are stripped before the L1 lookup.                                                       |
| E    | No dotted `xrpl.<domain>.<field>` attribute key in the runbook (only the L1 resource attrs `xrpl.network.*` may be dotted). Span names, filenames, OTel-standard keys, and metric labels are not flagged.                                                                                |

Rule F runs **unconditionally** (it is a purely syntactic check on the
call-sites and needs no `*SpanNames.h`), so a code path that calls
`SpanGuard::span`/`setAttribute` directly without ever defining a header is
still caught.

### Warnings (printed, never fail the build)

| Rule | Check                                                                                                                                                                                                                                                                                                                                                                                      |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| H    | A namespace-qualified constant (e.g. `foo::bar::myKey`) used at a telemetry call-site is not defined in any `*SpanNames.h`. The constant should live in the proper header; defining it in-place bypasses rules A/G/F. Warns rather than fails — the argument may be a legitimately dynamic value, and the header may live on a later branch. Bare locals and `std::` names are not warned. |

## Presence-gated

Every rule runs **only when the source files it needs are present** in the tree
and is otherwise skipped (printed as `SKIP: <rule> — <reason>`), never failed.
This keeps the check correct no matter how telemetry work is split across PRs —
a stacked chain, one large PR, or independent per-stage PRs where (for example)
the collector config lands before the dashboards. The collector/Tempo/dashboard/
runbook layers are introduced in later phases; on a branch without them, only
the L1-intrinsic rules (A, G, F) run.
