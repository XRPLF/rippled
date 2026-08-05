# OpenTelemetry Tracing for xrpld

This document explains how to build xrpld with OpenTelemetry distributed tracing support, configure the runtime telemetry options, and set up the observability backend to view traces.

- [OpenTelemetry Tracing for xrpld](#opentelemetry-tracing-for-xrpld)
  - [Overview](#overview)
  - [Building with Telemetry](#building-with-telemetry)
    - [Summary](#summary)
    - [Build steps](#build-steps)
      - [Install dependencies](#install-dependencies)
      - [Call CMake](#call-cmake)
      - [Build](#build)
  - [Building without telemetry](#building-without-telemetry)
  - [Troubleshooting](#troubleshooting)
    - [Conan lockfile error](#conan-lockfile-error)
    - [CMake target not found](#cmake-target-not-found)
  - [Conditional compilation](#conditional-compilation)
  - [Span lifetime and cross-thread handling](#span-lifetime-and-cross-thread-handling)
    - [`SpanGuard` versus `ScopedSpanGuard`](#spanguard-versus-scopedspanguard)
    - [Coroutine-aware context storage](#coroutine-aware-context-storage)
    - [Handing a span to a job](#handing-a-span-to-a-job)
    - [Why are unrelated spans in my trace?](#why-are-unrelated-spans-in-my-trace)

## Overview

xrpld supports optional [OpenTelemetry](https://opentelemetry.io/) distributed tracing.
When enabled, it instruments RPC requests with trace spans that are exported via
OTLP/HTTP to an OpenTelemetry Collector, which forwards them to a tracing backend
such as Grafana Tempo.

Telemetry is **off by default** at both compile time and runtime:

- **Compile time**: The Conan option `telemetry` and CMake option `telemetry` must be set to `True`/`ON`.
  When disabled, all `SpanGuard` calls compile to inline no-ops (defined in `SpanGuard.h`)
  with zero overhead — no OTel SDK dependency required.
- **Runtime**: The `[telemetry]` config section must set `enabled=1`.
  When disabled at runtime, a no-op implementation is used.

## Building with Telemetry

### Summary

Follow the same instructions as mentioned in [BUILD.md](../../BUILD.md) but with the following changes:

1. Pass `-o telemetry=True` to `conan install` to pull the `opentelemetry-cpp` dependency.
2. CMake will automatically pick up `telemetry=ON` from the Conan-generated toolchain.
3. Build as usual.

---

### Build steps

```bash
cd /path/to/xrpld
rm -rf .build
mkdir .build
cd .build
```

#### Install dependencies

The `telemetry` option adds `opentelemetry-cpp/1.28.0` as a dependency.
If the Conan lockfile does not yet include this package, bypass it with `--lockfile=""`.

```bash
conan install .. \
    --output-folder . \
    --build missing \
    --settings build_type=Debug \
    -o telemetry=True \
    -o tests=True \
    -o xrpld=True \
    --lockfile=""
```

> **Note**: The first build with telemetry may take longer as `opentelemetry-cpp`
> and its transitive dependencies are compiled from source.

#### Call CMake

The Conan-generated toolchain file sets `telemetry=ON` automatically.
No additional CMake flags are needed beyond the standard ones.

```bash
cmake .. -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Debug \
    -Dtests=ON -Dxrpld=ON
```

You should see in the CMake output:

```
-- OpenTelemetry tracing enabled
```

#### Build

```bash
cmake --build . --parallel $(nproc)
```

## Building without telemetry

Omit the `-o telemetry=True` option (or pass `-o telemetry=False`).
The `opentelemetry-cpp` dependency will not be downloaded,
the `XRPL_ENABLE_TELEMETRY` preprocessor define will not be set,
and all tracing macros will compile to no-ops.
The resulting binary is identical to one built before telemetry support was added.

## Troubleshooting

### Conan lockfile error

If you see `ERROR: Requirement 'opentelemetry-cpp/1.28.0' not in lockfile 'requires'`,
the lockfile was generated without the telemetry dependency.
Pass `--lockfile=""` to bypass the lockfile, or regenerate it with telemetry enabled.

### CMake target not found

If CMake reports that `opentelemetry-cpp` targets are not found,
ensure you ran `conan install` with `-o telemetry=True` and that the
Conan-generated toolchain file is being used.
The Conan package provides a single umbrella target
`opentelemetry-cpp::opentelemetry-cpp` (not individual component targets).

## Conditional compilation

All OpenTelemetry SDK types are hidden behind the pimpl idiom in `SpanGuard.cpp`.
When `XRPL_ENABLE_TELEMETRY` is not defined, `SpanGuard.h` provides an all-inline
no-op stub class with zero overhead and zero OTel dependencies.
At runtime, if `enabled=0` is set in config (or the section is omitted), a
`NullTelemetry` implementation is used that returns no-op spans.
This two-layer approach ensures zero overhead when telemetry is not wanted.

## Span lifetime and cross-thread handling

Telemetry exposes two RAII guards with split responsibilities, plus a
non-owning activation helper. Picking the right one is what keeps a trace's
parent/child nesting and its per-line log correlation correct.

### `SpanGuard` versus `ScopedSpanGuard`

- **`SpanGuard`** owns a span and nothing else. It is _thread-free_: it never
  touches the active-context stack, so it carries no thread affinity and may be
  moved to and ended on any thread. It is movable and move-assignable. Create
  one with `SpanGuard::span(cat, prefix, name)`, or with
  `SpanGuard::freshRoot(...)` to start a fresh trace root that ignores whatever
  span is currently ambient. Reach for a plain `SpanGuard` whenever the span
  must leave the context store that created it — for example when it is handed
  into a job.

- **`ScopedSpanGuard`** owns a `SpanGuard` _plus_ an active OTel scope that
  pushes the span onto the current context store. While it lives, the span is
  the ambient parent for child spans created on that store, and log lines
  emitted under it carry its `trace_id`. It is non-copyable and non-movable —
  short-lived stack RAII. It offers the same factories (`freshRoot(...)`,
  `childSpan(...)`). When the span must outlive the scope, convert it with
  `operator SpanGuard() &&`: that pops the scope on the origin store and yields
  the bare, thread-free `SpanGuard`.

Rule of thumb: use `ScopedSpanGuard` for same-thread (or same-coroutine)
nesting and log correlation; use the plain `SpanGuard` whenever the span
crosses out of the store that created it.

```mermaid
flowchart TD
    SG["SpanGuard<br/>(unscoped, thread-free)<br/>owns span only; movable across threads and coroutines"]
    SSG["ScopedSpanGuard<br/>(scoped, store-bound)<br/>owns a SpanGuard plus an active scope on the current store"]
    SA["ScopedActivation<br/>(non-owning)<br/>activates a borrowed span; never owns or ends it"]

    SSG -->|"handoff: pop scope, yield bare span"| SG
    SG -->|"activate / activateIfLive"| SA
    SA -.->|"borrows span, no ownership"| SG

    classDef box fill:#e8f0fe,stroke:#3b5bdb,color:#111827;
    class SG,SSG,SA box;
```

### Coroutine-aware context storage

The active-context stack is not a plain `thread_local`. At telemetry start
xrpld installs `CoroAwareContextStorage`, which keeps the stack in an
`xrpl::LocalValue`. Because `JobQueue::Coro::resume()` swaps the coroutine's
`LocalValue` store in and out with the coroutine, the ambient context _follows
the coroutine_ across every yield and resume — even when it resumes on a
different worker thread. A `ScopedSpanGuard` held across a coroutine yield is
therefore safe: its scope rides the coroutine and pops on the same store it was
pushed onto, so it never pops the wrong stack. Off a coroutine the `LocalValue`
transparently gives each thread its own store, so behaviour matches OTel's
default thread-local storage. This is what lets the RPC entry, process, and
command spans be scoped — for correct nesting and per-line log-trace
correlation — even though the RPC path yields.

### Handing a span to a job

The hand-off pattern is: create a thread-free `SpanGuard` at the origin (or
convert a `ScopedSpanGuard` with `operator SpanGuard() &&`), move it into the
job closure, and inside the worker body activate it non-destructively with
`telemetry::activateIfLive(handle)`. That call takes no ownership and returns a
`ScopedActivation` (a no-op if the handle is empty or the span inactive) which
makes the span the ambient context so log lines in the worker body carry its
`trace_id`. The activation neither owns nor ends the span — the owning
`SpanGuard` still controls its lifetime and ends it when the closure is
destroyed. Keep the activation confined to a synchronous, non-yielding block.
There is no detach step: a `SpanGuard` is already thread-free.

### Why are unrelated spans in my trace?

Historically a scoped guard destroyed off its origin thread popped the wrong
context stack, leaving a stale ambient span in place that later work inherited.
The current design removes that failure mode in two ways:

- **Coroutine-aware storage** makes a scope held across a coroutine yield pop on
  the same store it was pushed onto, so a coroutine that resumes on another
  worker never pops the wrong stack.

- **A same-store assertion** in `ScopedSpanGuard` (and `ScopedActivation`)
  records the `LocalValue` store its scope was pushed onto and checks, in
  debug/test/fuzzing builds, that destruction, hand-off, and discard all happen
  while that same store is active — turning a genuine cross-store misuse into an
  immediate assertion failure rather than a silently corrupted trace.

If a trace still shows unrelated spans nested under one operation, the usual
cause is an inbound entry point that inherited an ambient parent it should not
have. Start such an operation with `freshRoot()` so it begins a clean trace
root and never adopts whatever span happened to be active. To move a span
across a store boundary, keep it in a thread-free `SpanGuard` (or convert via
`operator SpanGuard() &&`) rather than holding a `ScopedSpanGuard` across the
boundary.
