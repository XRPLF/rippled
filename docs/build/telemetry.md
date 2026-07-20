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

The `telemetry` option adds `opentelemetry-cpp/1.26.0` as a dependency.
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

If you see `ERROR: Requirement 'opentelemetry-cpp/1.26.0' not in lockfile 'requires'`,
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

`SpanGuard` is an RAII wrapper: it activates a span on the current thread when
constructed and ends it when destroyed. A normal (scoped) guard binds an OTel
`Scope` to the constructing thread's thread-local context stack. Because of
this, a scoped guard must be used and destroyed on the thread that created it.

Two entry points cover the cases where the default scoped behaviour is wrong:

- **`SpanGuard::rootSpan(cat, prefix, name)`** starts a span as a fresh trace
  root, ignoring whatever span is already active on the current thread. Use it
  at an inbound entry point that runs on a shared worker thread (for example a
  peer message received while unrelated work is still on the stack), so those
  unrelated spans do not become parents of the new trace.

- **`std::move(guard).detached()`** returns a new guard that holds the same
  span with **no** thread-local `Scope`. Call it on the origin thread before
  handing the guard to a job that runs on another thread. The returned guard
  can be moved freely across threads and ended anywhere; only its final
  destruction ends the span.

### Why are unrelated spans in my trace?

If a trace shows hundreds of unrelated spans nested under one operation, a
scoped guard was likely moved to another thread (for example stored in a job
and destroyed on a worker thread). Destroying a scoped guard off its origin
thread pops the wrong context stack, leaving the origin thread's active span
in place so later spans inherit it. Fix it by calling `.detached()` on the
origin thread before the hand-off, or by starting the operation with
`rootSpan()` so it never inherits an ambient parent.
