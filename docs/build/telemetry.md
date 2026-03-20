# OpenTelemetry Tracing for Rippled

This document explains how to build rippled with OpenTelemetry distributed tracing support, configure the runtime telemetry options, and set up the observability backend to view traces.

- [OpenTelemetry Tracing for Rippled](#opentelemetry-tracing-for-rippled)
  - [Overview](#overview)
  - [Building with Telemetry](#building-with-telemetry)
    - [Summary](#summary)
    - [Build steps](#build-steps)
      - [Install dependencies](#install-dependencies)
      - [Call CMake](#call-cmake)
      - [Build](#build)
    - [Building without telemetry](#building-without-telemetry)
  - [Runtime Configuration](#runtime-configuration)
    - [Configuration options](#configuration-options)
  - [Observability Stack](#observability-stack)
    - [Start the stack](#start-the-stack)
    - [Verify the stack](#verify-the-stack)
    - [View traces in Jaeger](#view-traces-in-jaeger)
  - [Running Tests](#running-tests)
  - [Troubleshooting](#troubleshooting)
    - [No traces appear in Jaeger](#no-traces-appear-in-jaeger)
    - [Conan lockfile error](#conan-lockfile-error)
    - [CMake target not found](#cmake-target-not-found)
  - [Architecture](#architecture)
    - [Key files](#key-files)
    - [Conditional compilation](#conditional-compilation)

## Overview

Rippled supports optional [OpenTelemetry](https://opentelemetry.io/) distributed tracing.
When enabled, it instruments RPC requests with trace spans that are exported via
OTLP/HTTP to an OpenTelemetry Collector, which forwards them to a tracing backend
such as Jaeger.

Telemetry is **off by default** at both compile time and runtime:

- **Compile time**: The Conan option `telemetry` and CMake option `telemetry` must be set to `True`/`ON`.
  When disabled, all tracing macros compile to `((void)0)` with zero overhead.
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
cd /path/to/rippled
rm -rf .build
mkdir .build
cd .build
```

#### Install dependencies

The `telemetry` option adds `opentelemetry-cpp/1.18.0` as a dependency.
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

### Building without telemetry

Omit the `-o telemetry=True` option (or pass `-o telemetry=False`).
The `opentelemetry-cpp` dependency will not be downloaded,
the `XRPL_ENABLE_TELEMETRY` preprocessor define will not be set,
and all tracing macros will compile to no-ops.
The resulting binary is identical to one built before telemetry support was added.

## Runtime Configuration

Add a `[telemetry]` section to your `xrpld.cfg` file:

```ini
[telemetry]
enabled=1
service_name=rippled
endpoint=http://localhost:4318/v1/traces
sampling_ratio=1.0
trace_rpc=1
trace_transactions=1
trace_consensus=1
trace_peer=0
```

### Configuration options

| Option                | Type   | Default                           | Description                                        |
| --------------------- | ------ | --------------------------------- | -------------------------------------------------- |
| `enabled`             | int    | `0`                               | Enable (`1`) or disable (`0`) telemetry at runtime |
| `service_name`        | string | `rippled`                         | Service name reported in traces                    |
| `service_instance_id` | string | node public key                   | Unique instance identifier                         |
| `exporter`            | string | `otlp_http`                       | Exporter type                                      |
| `endpoint`            | string | `http://localhost:4318/v1/traces` | OTLP/HTTP collector endpoint                       |
| `use_tls`             | int    | `0`                               | Enable TLS for the exporter connection             |
| `tls_ca_cert`         | string | (empty)                           | Path to CA certificate for TLS                     |
| `sampling_ratio`      | double | `1.0`                             | Fraction of traces to sample (`0.0` to `1.0`)      |
| `batch_size`          | uint32 | `512`                             | Maximum spans per export batch                     |
| `batch_delay_ms`      | uint32 | `5000`                            | Maximum delay (ms) before flushing a batch         |
| `max_queue_size`      | uint32 | `2048`                            | Maximum spans queued in memory                     |
| `trace_rpc`           | int    | `1`                               | Enable RPC request tracing                         |
| `trace_transactions`  | int    | `1`                               | Enable transaction lifecycle tracing               |
| `trace_consensus`     | int    | `1`                               | Enable consensus round tracing                     |
| `trace_peer`          | int    | `0`                               | Enable peer message tracing (high volume)          |
| `trace_ledger`        | int    | `1`                               | Enable ledger close tracing                        |

## Observability Stack

A Docker Compose stack is provided in `docker/telemetry/` with three services:

| Service            | Port                                           | Purpose                                              |
| ------------------ | ---------------------------------------------- | ---------------------------------------------------- |
| **OTel Collector** | `4317` (gRPC), `4318` (HTTP), `13133` (health) | Receives OTLP spans, batches, and forwards to Jaeger |
| **Jaeger**         | `16686` (UI)                                   | Trace storage and visualization                      |
| **Grafana**        | `3000`                                         | Dashboards (Jaeger pre-configured as datasource)     |

### Start the stack

```bash
docker compose -f docker/telemetry/docker-compose.yml up -d
```

### Verify the stack

```bash
# Collector health
curl http://localhost:13133

# Jaeger UI
open http://localhost:16686

# Grafana
open http://localhost:3000
```

### View traces in Jaeger

1. Open `http://localhost:16686` in a browser.
2. Select the service name (e.g. `rippled`) from the **Service** dropdown.
3. Click **Find Traces**.
4. Click into any trace to see the span tree and attributes.

Traced RPC operations produce a span hierarchy like:

```
rpc.request
  └── rpc.command.server_info  (xrpl.rpc.command=server_info, xrpl.rpc.status=success)
```

Each span includes attributes:

- `xrpl.rpc.command` — the RPC method name
- `xrpl.rpc.version` — API version
- `xrpl.rpc.role` — `admin` or `user`
- `xrpl.rpc.status` — `success` or `error`

## Running Tests

Unit tests run with the telemetry-enabled build regardless of whether the
observability stack is running. When no collector is available, the exporter
silently drops spans with no impact on test results.

```bash
# Run all RPC tests
./xrpld --unittest=RPCCall,ServerInfo,AccountTx,LedgerRPC,Transaction --unittest-jobs $(nproc)

# Run the full test suite
./xrpld --unittest --unittest-jobs $(nproc)
```

To generate traces during manual testing, start rippled in standalone mode:

```bash
./xrpld --conf /path/to/xrpld.cfg --standalone --start
```

Then send RPC requests:

```bash
curl -s -X POST http://127.0.0.1:5005/ \
    -H "Content-Type: application/json" \
    -d '{"method":"server_info","params":[{}]}'
```

## Troubleshooting

### No traces appear in Jaeger

1. Confirm the OTel Collector is running: `docker compose -f docker/telemetry/docker-compose.yml ps`
2. Check collector logs for errors: `docker compose -f docker/telemetry/docker-compose.yml logs otel-collector`
3. Confirm `[telemetry] enabled=1` is set in the rippled config.
4. Confirm `endpoint` points to the correct collector address (`http://localhost:4318/v1/traces`).
5. Wait for the batch delay to elapse (default `5000` ms) before checking Jaeger.

### Conan lockfile error

If you see `ERROR: Requirement 'opentelemetry-cpp/1.18.0' not in lockfile 'requires'`,
the lockfile was generated without the telemetry dependency.
Pass `--lockfile=""` to bypass the lockfile, or regenerate it with telemetry enabled.

### CMake target not found

If CMake reports that `opentelemetry-cpp` targets are not found,
ensure you ran `conan install` with `-o telemetry=True` and that the
Conan-generated toolchain file is being used.
The Conan package provides a single umbrella target
`opentelemetry-cpp::opentelemetry-cpp` (not individual component targets).

## Architecture

### Key files

| File                                           | Purpose                                                     |
| ---------------------------------------------- | ----------------------------------------------------------- |
| `include/xrpl/telemetry/Telemetry.h`           | Abstract telemetry interface and `Setup` struct             |
| `include/xrpl/telemetry/SpanGuard.h`           | RAII span guard (activates scope, ends span on destruction) |
| `src/libxrpl/telemetry/Telemetry.cpp`          | OTel-backed implementation (`TelemetryImpl`)                |
| `src/libxrpl/telemetry/TelemetryConfig.cpp`    | Config parser (`setup_Telemetry()`)                         |
| `src/libxrpl/telemetry/NullTelemetry.cpp`      | No-op implementation (used when disabled)                   |
| `src/xrpld/telemetry/TracingInstrumentation.h` | Convenience macros (`XRPL_TRACE_RPC`, etc.)                 |
| `src/xrpld/rpc/detail/ServerHandler.cpp`       | RPC entry point instrumentation                             |
| `src/xrpld/rpc/detail/RPCHandler.cpp`          | Per-command instrumentation                                 |
| `docker/telemetry/docker-compose.yml`          | Observability stack (Collector + Jaeger + Grafana)          |
| `docker/telemetry/otel-collector-config.yaml`  | OTel Collector pipeline configuration                       |

### Conditional compilation

All OpenTelemetry SDK headers are guarded behind `#ifdef XRPL_ENABLE_TELEMETRY`.
The instrumentation macros in `TracingInstrumentation.h` compile to `((void)0)` when
the define is absent.
At runtime, if `enabled=0` is set in config (or the section is omitted), a
`NullTelemetry` implementation is used that returns no-op spans.
This two-layer approach ensures zero overhead when telemetry is not wanted.
