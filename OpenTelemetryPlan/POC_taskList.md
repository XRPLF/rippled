# OpenTelemetry POC Task List

> **Goal**: Build a minimal end-to-end proof of concept that demonstrates distributed tracing in rippled. A successful POC will show RPC request traces flowing from rippled through an OTel Collector into Jaeger, viewable in a browser UI.
>
> **Scope**: RPC tracing only (highest value, lowest risk per the [CRAWL phase](./06-implementation-phases.md#6102-quick-wins-immediate-value) in the implementation phases). No cross-node P2P context propagation or consensus tracing in the POC.

### Related Plan Documents

| Document                                                         | Relevance to POC                                                                                                                                          |
| ---------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [00-tracing-fundamentals.md](./00-tracing-fundamentals.md)       | Core concepts: traces, spans, context propagation, sampling                                                                                               |
| [01-architecture-analysis.md](./01-architecture-analysis.md)     | RPC request flow (§1.5), key trace points (§1.6), instrumentation priority (§1.7)                                                                         |
| [02-design-decisions.md](./02-design-decisions.md)               | SDK selection (§2.1), exporter config (§2.2), span naming (§2.3), attribute schema (§2.4), coexistence with PerfLog/Insight (§2.6)                        |
| [03-implementation-strategy.md](./03-implementation-strategy.md) | Directory structure (§3.1), key principles (§3.2), performance overhead (§3.3-3.6), conditional compilation (§3.7.3), code intrusiveness (§3.9)           |
| [04-code-samples.md](./04-code-samples.md)                       | Telemetry interface (§4.1), SpanGuard (§4.2), macros (§4.3), RPC instrumentation (§4.5.3)                                                                 |
| [05-configuration-reference.md](./05-configuration-reference.md) | rippled config (§5.1), config parser (§5.2), Application integration (§5.3), CMake (§5.4), Collector config (§5.5), Docker Compose (§5.6), Grafana (§5.8) |
| [06-implementation-phases.md](./06-implementation-phases.md)     | Phase 1 core tasks (§6.2), Phase 2 RPC tasks (§6.3), quick wins (§6.10), definition of done (§6.11)                                                       |
| [07-observability-backends.md](./07-observability-backends.md)   | Jaeger dev setup (§7.1), Grafana dashboards (§7.6), alert rules (§7.6.3)                                                                                  |

---

## Task 0: Docker Observability Stack Setup

**Objective**: Stand up the backend infrastructure to receive, store, and display traces.

**What to do**:

- Create `docker/telemetry/docker-compose.yml` in the repo with three services:
  1. **OpenTelemetry Collector** (`otel/opentelemetry-collector-contrib:latest`)
     - Expose ports `4317` (OTLP gRPC) and `4318` (OTLP HTTP)
     - Expose port `13133` (health check)
     - Mount a config file `docker/telemetry/otel-collector-config.yaml`
  2. **Jaeger** (`jaegertracing/all-in-one:latest`)
     - Expose port `16686` (UI) and `14250` (gRPC collector)
     - Set env `COLLECTOR_OTLP_ENABLED=true`
  3. **Grafana** (`grafana/grafana:latest`) — optional but useful
     - Expose port `3000`
     - Enable anonymous admin access for local dev (`GF_AUTH_ANONYMOUS_ENABLED=true`, `GF_AUTH_ANONYMOUS_ORG_ROLE=Admin`)
     - Provision Jaeger as a data source via `docker/telemetry/grafana/provisioning/datasources/jaeger.yaml`

- Create `docker/telemetry/otel-collector-config.yaml`:

  ```yaml
  receivers:
    otlp:
      protocols:
        grpc:
          endpoint: 0.0.0.0:4317
        http:
          endpoint: 0.0.0.0:4318

  processors:
    batch:
      timeout: 1s
      send_batch_size: 100

  exporters:
    logging:
      verbosity: detailed
    otlp/jaeger:
      endpoint: jaeger:4317
      tls:
        insecure: true

  service:
    pipelines:
      traces:
        receivers: [otlp]
        processors: [batch]
        exporters: [logging, otlp/jaeger]
  ```

- Create Grafana Jaeger datasource provisioning file at `docker/telemetry/grafana/provisioning/datasources/jaeger.yaml`:
  ```yaml
  apiVersion: 1
  datasources:
    - name: Jaeger
      type: jaeger
      access: proxy
      url: http://jaeger:16686
  ```

**Verification**: Run `docker compose -f docker/telemetry/docker-compose.yml up -d`, then:

- `curl http://localhost:13133` returns healthy (Collector)
- `http://localhost:16686` opens Jaeger UI (no traces yet)
- `http://localhost:3000` opens Grafana (optional)

**Reference**:

- [05-configuration-reference.md §5.5](./05-configuration-reference.md) — Collector config (dev YAML with Jaeger exporter)
- [05-configuration-reference.md §5.6](./05-configuration-reference.md) — Docker Compose development environment
- [07-observability-backends.md §7.1](./07-observability-backends.md) — Jaeger quick start and backend selection
- [05-configuration-reference.md §5.8](./05-configuration-reference.md) — Grafana datasource provisioning and dashboards

---

## Task 1: Add OpenTelemetry C++ SDK Dependency

**Objective**: Make `opentelemetry-cpp` available to the build system.

**What to do**:

- Edit `conanfile.py` to add `opentelemetry-cpp` as an **optional** dependency. The gRPC otel plugin flag (`"grpc/*:otel_plugin": False`) in the existing conanfile may need to remain false — we pull the OTel SDK separately.
  - Add a Conan option: `with_telemetry = [True, False]` defaulting to `False`
  - When `with_telemetry` is `True`, add `opentelemetry-cpp` to `self.requires()`
  - Required OTel Conan components: `opentelemetry-cpp` (which bundles api, sdk, and exporters). If the package isn't in Conan Center, consider using `FetchContent` in CMake or building from source as a fallback.
- Edit `CMakeLists.txt`:
  - Add option: `option(XRPL_ENABLE_TELEMETRY "Enable OpenTelemetry tracing" OFF)`
  - When ON, `find_package(opentelemetry-cpp CONFIG REQUIRED)` and add compile definition `XRPL_ENABLE_TELEMETRY`
  - When OFF, do nothing (zero build impact)
- Verify the build succeeds with `-DXRPL_ENABLE_TELEMETRY=OFF` (no regressions) and with `-DXRPL_ENABLE_TELEMETRY=ON` (SDK links successfully).

**Key files**:

- `conanfile.py`
- `CMakeLists.txt`

**Reference**:

- [05-configuration-reference.md §5.4](./05-configuration-reference.md) — CMake integration, `FindOpenTelemetry.cmake`, `XRPL_ENABLE_TELEMETRY` option
- [03-implementation-strategy.md §3.2](./03-implementation-strategy.md) — Key principle: zero-cost when disabled via compile-time flags
- [02-design-decisions.md §2.1](./02-design-decisions.md) — SDK selection rationale and required OTel components

---

## Task 2: Create Core Telemetry Interface and NullTelemetry

**Objective**: Define the `Telemetry` abstract interface and a no-op implementation so the rest of the codebase can reference telemetry without hard-depending on the OTel SDK.

**What to do**:

- Create `include/xrpl/telemetry/Telemetry.h`:
  - Define `namespace xrpl::telemetry`
  - Define `struct Telemetry::Setup` holding: `enabled`, `exporterEndpoint`, `samplingRatio`, `serviceName`, `serviceVersion`, `serviceInstanceId`, `traceRpc`, `traceTransactions`, `traceConsensus`, `tracePeer`
  - Define abstract `class Telemetry` with:
    - `virtual void start() = 0;`
    - `virtual void stop() = 0;`
    - `virtual bool isEnabled() const = 0;`
    - `virtual nostd::shared_ptr<Tracer> getTracer(string_view name = "rippled") = 0;`
    - `virtual nostd::shared_ptr<Span> startSpan(string_view name, SpanKind kind = kInternal) = 0;`
    - `virtual nostd::shared_ptr<Span> startSpan(string_view name, Context const& parentContext, SpanKind kind = kInternal) = 0;`
    - `virtual bool shouldTraceRpc() const = 0;`
    - `virtual bool shouldTraceTransactions() const = 0;`
    - `virtual bool shouldTraceConsensus() const = 0;`
  - Factory: `std::unique_ptr<Telemetry> make_Telemetry(Setup const&, beast::Journal);`
  - Config parser: `Telemetry::Setup setup_Telemetry(Section const&, std::string const& nodePublicKey, std::string const& version);`

- Create `include/xrpl/telemetry/SpanGuard.h`:
  - RAII guard that takes an `nostd::shared_ptr<Span>`, creates a `Scope`, and calls `span->End()` in destructor.
  - Convenience: `setAttribute()`, `setOk()`, `setStatus()`, `addEvent()`, `recordException()`, `context()`
  - See [04-code-samples.md](./04-code-samples.md) §4.2 for the full implementation.

- Create `src/libxrpl/telemetry/NullTelemetry.cpp`:
  - Implements `Telemetry` with all no-ops.
  - `isEnabled()` returns `false`, `startSpan()` returns a noop span.
  - This is used when `XRPL_ENABLE_TELEMETRY` is OFF or `enabled=0` in config.

- Guard all OTel SDK headers behind `#ifdef XRPL_ENABLE_TELEMETRY`. The `NullTelemetry` implementation should compile without the OTel SDK present.

**Key new files**:

- `include/xrpl/telemetry/Telemetry.h`
- `include/xrpl/telemetry/SpanGuard.h`
- `src/libxrpl/telemetry/NullTelemetry.cpp`

**Reference**:

- [04-code-samples.md §4.1](./04-code-samples.md) — Full `Telemetry` interface with `Setup` struct, lifecycle, tracer access, span creation, and component filtering methods
- [04-code-samples.md §4.2](./04-code-samples.md) — Full `SpanGuard` RAII implementation and `NullSpanGuard` no-op class
- [03-implementation-strategy.md §3.1](./03-implementation-strategy.md) — Directory structure: `include/xrpl/telemetry/` for headers, `src/libxrpl/telemetry/` for implementation
- [03-implementation-strategy.md §3.7.3](./03-implementation-strategy.md) — Conditional instrumentation and zero-cost compile-time disabled pattern

---

## Task 3: Implement OTel-Backed Telemetry

**Objective**: Implement the real `Telemetry` class that initializes the OTel SDK, configures the OTLP exporter and batch processor, and creates tracers/spans.

**What to do**:

- Create `src/libxrpl/telemetry/Telemetry.cpp` (compiled only when `XRPL_ENABLE_TELEMETRY=ON`):
  - `class TelemetryImpl : public Telemetry` that:
    - In `start()`: creates a `TracerProvider` with:
      - Resource attributes: `service.name`, `service.version`, `service.instance.id`
      - An `OtlpGrpcExporter` pointed at `setup.exporterEndpoint` (default `localhost:4317`)
      - A `BatchSpanProcessor` with configurable batch size and delay
      - A `TraceIdRatioBasedSampler` using `setup.samplingRatio`
    - Sets the global `TracerProvider`
    - In `stop()`: calls `ForceFlush()` then shuts down the provider
    - In `startSpan()`: delegates to `getTracer()->StartSpan(name, ...)`
    - `shouldTraceRpc()` etc. read from `Setup` fields

- Create `src/libxrpl/telemetry/TelemetryConfig.cpp`:
  - `setup_Telemetry()` parses the `[telemetry]` config section from `xrpld.cfg`
  - Maps config keys: `enabled`, `exporter`, `endpoint`, `sampling_ratio`, `trace_rpc`, `trace_transactions`, `trace_consensus`, `trace_peer`

- Wire `make_Telemetry()` factory:
  - If `setup.enabled` is true AND `XRPL_ENABLE_TELEMETRY` is defined: return `TelemetryImpl`
  - Otherwise: return `NullTelemetry`

- Add telemetry source files to CMake. When `XRPL_ENABLE_TELEMETRY=ON`, compile `Telemetry.cpp` and `TelemetryConfig.cpp` and link against `opentelemetry-cpp::api`, `opentelemetry-cpp::sdk`, `opentelemetry-cpp::otlp_grpc_exporter`. When OFF, compile only `NullTelemetry.cpp`.

**Key new files**:

- `src/libxrpl/telemetry/Telemetry.cpp`
- `src/libxrpl/telemetry/TelemetryConfig.cpp`

**Key modified files**:

- `CMakeLists.txt` (add telemetry library target)

**Reference**:

- [04-code-samples.md §4.1](./04-code-samples.md) — `Telemetry` interface that `TelemetryImpl` must implement
- [05-configuration-reference.md §5.2](./05-configuration-reference.md) — `setup_Telemetry()` config parser implementation
- [02-design-decisions.md §2.2](./02-design-decisions.md) — OTLP/gRPC exporter config (endpoint, TLS options)
- [02-design-decisions.md §2.4.1](./02-design-decisions.md) — Resource attributes: `service.name`, `service.version`, `service.instance.id`, `xrpl.network.id`
- [03-implementation-strategy.md §3.4](./03-implementation-strategy.md) — Per-operation CPU costs and overhead budget for span creation
- [03-implementation-strategy.md §3.5](./03-implementation-strategy.md) — Memory overhead: static (~456 KB) and dynamic (~1.2 MB) budgets

---

## Task 4: Integrate Telemetry into Application Lifecycle

**Objective**: Wire the `Telemetry` object into `Application` so all components can access it.

**What to do**:

- Edit `src/xrpld/app/main/Application.h`:
  - Forward-declare `namespace xrpl::telemetry { class Telemetry; }`
  - Add pure virtual method: `virtual telemetry::Telemetry& getTelemetry() = 0;`

- Edit `src/xrpld/app/main/Application.cpp` (the `ApplicationImp` class):
  - Add member: `std::unique_ptr<telemetry::Telemetry> telemetry_;`
  - In the constructor, after config is loaded and node identity is known:
    ```cpp
    auto const telemetrySection = config_->section("telemetry");
    auto telemetrySetup = telemetry::setup_Telemetry(
        telemetrySection,
        toBase58(TokenType::NodePublic, nodeIdentity_.publicKey()),
        BuildInfo::getVersionString());
    telemetry_ = telemetry::make_Telemetry(telemetrySetup, logs_->journal("Telemetry"));
    ```
  - In `start()`: call `telemetry_->start()` early
  - In `stop()` or destructor: call `telemetry_->stop()` late (to flush pending spans)
  - Implement `getTelemetry()` override: return `*telemetry_`

- Add `[telemetry]` section to the example config `cfg/rippled-example.cfg`:
  ```ini
  # [telemetry]
  # enabled=1
  # endpoint=localhost:4317
  # sampling_ratio=1.0
  # trace_rpc=1
  ```

**Key modified files**:

- `src/xrpld/app/main/Application.h`
- `src/xrpld/app/main/Application.cpp`
- `cfg/rippled-example.cfg` (or equivalent example config)

**Reference**:

- [05-configuration-reference.md §5.3](./05-configuration-reference.md) — `ApplicationImp` changes: member declaration, constructor init, `start()`/`stop()` wiring, `getTelemetry()` override
- [05-configuration-reference.md §5.1](./05-configuration-reference.md) — `[telemetry]` config section format and all option defaults
- [03-implementation-strategy.md §3.9.2](./03-implementation-strategy.md) — File impact assessment: `Application.cpp` ~15 lines added, ~3 changed (Low risk)

---

## Task 5: Create Instrumentation Macros

**Objective**: Define convenience macros that make instrumenting code one-liners, and that compile to zero-cost no-ops when telemetry is disabled.

**What to do**:

- Create `src/xrpld/telemetry/TracingInstrumentation.h`:
  - When `XRPL_ENABLE_TELEMETRY` is defined:

    ```cpp
    #define XRPL_TRACE_SPAN(telemetry, name) \
        auto _xrpl_span_ = (telemetry).startSpan(name); \
        ::xrpl::telemetry::SpanGuard _xrpl_guard_(_xrpl_span_)

    #define XRPL_TRACE_RPC(telemetry, name) \
        std::optional<::xrpl::telemetry::SpanGuard> _xrpl_guard_; \
        if ((telemetry).shouldTraceRpc()) { \
            _xrpl_guard_.emplace((telemetry).startSpan(name)); \
        }

    #define XRPL_TRACE_SET_ATTR(key, value) \
        if (_xrpl_guard_.has_value()) { \
            _xrpl_guard_->setAttribute(key, value); \
        }

    #define XRPL_TRACE_EXCEPTION(e) \
        if (_xrpl_guard_.has_value()) { \
            _xrpl_guard_->recordException(e); \
        }
    ```

  - When `XRPL_ENABLE_TELEMETRY` is NOT defined, all macros expand to `((void)0)`

**Key new file**:

- `src/xrpld/telemetry/TracingInstrumentation.h`

**Reference**:

- [04-code-samples.md §4.3](./04-code-samples.md) — Full macro definitions for `XRPL_TRACE_SPAN`, `XRPL_TRACE_RPC`, `XRPL_TRACE_CONSENSUS`, `XRPL_TRACE_SET_ATTR`, `XRPL_TRACE_EXCEPTION` with both enabled and disabled branches
- [03-implementation-strategy.md §3.7.3](./03-implementation-strategy.md) — Conditional instrumentation pattern: compile-time `#ifndef` and runtime `shouldTrace*()` checks
- [03-implementation-strategy.md §3.9.7](./03-implementation-strategy.md) — Before/after code examples showing minimal intrusiveness (~1-3 lines per instrumentation point)

---

## Task 6: Instrument RPC ServerHandler

**Objective**: Add tracing to the HTTP RPC entry point so every incoming RPC request creates a span.

**What to do**:

- Edit `src/xrpld/rpc/detail/ServerHandler.cpp`:
  - `#include` the `TracingInstrumentation.h` header
  - In `ServerHandler::onRequest(Session& session)`:
    - At the top of the method, add: `XRPL_TRACE_RPC(app_.getTelemetry(), "rpc.request");`
    - After the RPC command name is extracted, set attribute: `XRPL_TRACE_SET_ATTR("xrpl.rpc.command", command);`
    - After the response status is known, set: `XRPL_TRACE_SET_ATTR("http.status_code", static_cast<int64_t>(statusCode));`
    - Wrap error paths with: `XRPL_TRACE_EXCEPTION(e);`
  - In `ServerHandler::processRequest(...)`:
    - Add a child span: `XRPL_TRACE_RPC(app_.getTelemetry(), "rpc.process");`
    - Set method attribute: `XRPL_TRACE_SET_ATTR("xrpl.rpc.method", request_method);`
  - In `ServerHandler::onWSMessage(...)` (WebSocket path):
    - Add: `XRPL_TRACE_RPC(app_.getTelemetry(), "rpc.ws.message");`

- The goal is to see spans like:
  ```
  rpc.request
    └── rpc.process
  ```
  in Jaeger for every HTTP RPC call.

**Key modified file**:

- `src/xrpld/rpc/detail/ServerHandler.cpp` (~15-25 lines added)

**Reference**:

- [04-code-samples.md §4.5.3](./04-code-samples.md) — Complete `ServerHandler::onRequest()` instrumented code sample with W3C header extraction, span creation, attribute setting, and error handling
- [01-architecture-analysis.md §1.5](./01-architecture-analysis.md) — RPC request flow diagram: HTTP request -> attributes -> jobqueue.enqueue -> rpc.command -> response
- [01-architecture-analysis.md §1.6](./01-architecture-analysis.md) — Key trace points table: `rpc.request` in `ServerHandler.cpp::onRequest()` (Priority: High)
- [02-design-decisions.md §2.3](./02-design-decisions.md) — Span naming convention: `rpc.request`, `rpc.command.*`
- [02-design-decisions.md §2.4.2](./02-design-decisions.md) — RPC span attributes: `xrpl.rpc.command`, `xrpl.rpc.version`, `xrpl.rpc.role`, `xrpl.rpc.params`
- [03-implementation-strategy.md §3.9.2](./03-implementation-strategy.md) — File impact: `ServerHandler.cpp` ~40 lines added, ~10 changed (Low risk)

---

## Task 7: Instrument RPC Command Execution

**Objective**: Add per-command tracing inside the RPC handler so each command (e.g., `submit`, `account_info`, `server_info`) gets its own child span.

**What to do**:

- Edit `src/xrpld/rpc/detail/RPCHandler.cpp`:
  - `#include` the `TracingInstrumentation.h` header
  - In `doCommand(RPC::JsonContext& context, Json::Value& result)`:
    - At the top: `XRPL_TRACE_RPC(context.app.getTelemetry(), "rpc.command." + context.method);`
    - Set attributes:
      - `XRPL_TRACE_SET_ATTR("xrpl.rpc.command", context.method);`
      - `XRPL_TRACE_SET_ATTR("xrpl.rpc.version", static_cast<int64_t>(context.apiVersion));`
      - `XRPL_TRACE_SET_ATTR("xrpl.rpc.role", (context.role == Role::ADMIN) ? "admin" : "user");`
    - On success: `XRPL_TRACE_SET_ATTR("xrpl.rpc.status", "success");`
    - On error: `XRPL_TRACE_SET_ATTR("xrpl.rpc.status", "error");` and set the error message

- After this, traces in Jaeger should look like:
  ```
  rpc.request  (xrpl.rpc.command=account_info)
    └── rpc.process
          └── rpc.command.account_info  (xrpl.rpc.version=2, xrpl.rpc.role=user, xrpl.rpc.status=success)
  ```

**Key modified file**:

- `src/xrpld/rpc/detail/RPCHandler.cpp` (~15-20 lines added)

**Reference**:

- [04-code-samples.md §4.5.3](./04-code-samples.md) — `ServerHandler::onRequest()` code sample (includes child span pattern for `rpc.command.*`)
- [02-design-decisions.md §2.3](./02-design-decisions.md) — Span naming: `rpc.command.*` pattern with dynamic command name (e.g., `rpc.command.server_info`)
- [02-design-decisions.md §2.4.2](./02-design-decisions.md) — RPC attribute schema: `xrpl.rpc.command`, `xrpl.rpc.version`, `xrpl.rpc.role`, `xrpl.rpc.status`
- [01-architecture-analysis.md §1.6](./01-architecture-analysis.md) — Key trace points table: `rpc.command.*` in `RPCHandler.cpp::doCommand()` (Priority: High)
- [02-design-decisions.md §2.6.5](./02-design-decisions.md) — Correlation with PerfLog: how `doCommand()` can link trace_id with existing PerfLog entries
- [03-implementation-strategy.md §3.4.4](./03-implementation-strategy.md) — RPC request overhead budget: ~1.75 μs total per request

---

## Task 8: Build, Run, and Verify End-to-End

**Objective**: Prove the full pipeline works: rippled emits traces -> OTel Collector receives them -> Jaeger displays them.

**What to do**:

1. **Start the Docker stack**:

   ```bash
   docker compose -f docker/telemetry/docker-compose.yml up -d
   ```

   Verify Collector health: `curl http://localhost:13133`

2. **Build rippled with telemetry**:

   ```bash
   # Adjust for your actual build workflow
   conan install . --build=missing -o with_telemetry=True
   cmake --preset default -DXRPL_ENABLE_TELEMETRY=ON
   cmake --build --preset default
   ```

3. **Configure rippled**:
   Add to `rippled.cfg` (or your local test config):

   ```ini
   [telemetry]
   enabled=1
   endpoint=localhost:4317
   sampling_ratio=1.0
   trace_rpc=1
   ```

4. **Start rippled** in standalone mode:

   ```bash
   ./rippled --conf rippled.cfg -a --start
   ```

5. **Generate RPC traffic**:

   ```bash
   # server_info
   curl -s -X POST http://localhost:5005 \
     -H "Content-Type: application/json" \
     -d '{"method":"server_info","params":[{}]}'

   # ledger
   curl -s -X POST http://localhost:5005 \
     -H "Content-Type: application/json" \
     -d '{"method":"ledger","params":[{"ledger_index":"current"}]}'

   # account_info (will error in standalone, that's fine — we trace errors too)
   curl -s -X POST http://localhost:5005 \
     -H "Content-Type: application/json" \
     -d '{"method":"account_info","params":[{"account":"rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"}]}'
   ```

6. **Verify in Jaeger**:
   - Open `http://localhost:16686`
   - Select service `rippled` from the dropdown
   - Click "Find Traces"
   - Confirm you see traces with spans: `rpc.request` -> `rpc.process` -> `rpc.command.server_info`
   - Click into a trace and verify attributes: `xrpl.rpc.command`, `xrpl.rpc.status`, `xrpl.rpc.version`

7. **Verify zero-overhead when disabled**:
   - Rebuild with `XRPL_ENABLE_TELEMETRY=OFF`, or set `enabled=0` in config
   - Run the same RPC calls
   - Confirm no new traces appear and no errors in rippled logs

**Verification Checklist**:

- [ ] Docker stack starts without errors
- [ ] rippled builds with `-DXRPL_ENABLE_TELEMETRY=ON`
- [ ] rippled starts and connects to OTel Collector (check rippled logs for telemetry messages)
- [ ] Traces appear in Jaeger UI under service "rippled"
- [ ] Span hierarchy is correct (parent-child relationships)
- [ ] Span attributes are populated (`xrpl.rpc.command`, `xrpl.rpc.status`, etc.)
- [ ] Error spans show error status and message
- [ ] Building with `XRPL_ENABLE_TELEMETRY=OFF` produces no regressions
- [ ] Setting `enabled=0` at runtime produces no traces and no errors

**Reference**:

- [06-implementation-phases.md §6.11.1](./06-implementation-phases.md) — Phase 1 definition of done: SDK compiles, runtime toggle works, span creation verified in Jaeger, config validation passes
- [06-implementation-phases.md §6.11.2](./06-implementation-phases.md) — Phase 2 definition of done: 100% RPC coverage, traceparent propagation, <1ms p99 overhead, dashboard deployed
- [06-implementation-phases.md §6.8](./06-implementation-phases.md) — Success metrics: trace coverage >95%, CPU overhead <3%, memory <5 MB, latency impact <2%
- [03-implementation-strategy.md §3.9.5](./03-implementation-strategy.md) — Backward compatibility: config optional, protocol unchanged, `XRPL_ENABLE_TELEMETRY=OFF` produces identical binary
- [01-architecture-analysis.md §1.8](./01-architecture-analysis.md) — Observable outcomes: what traces, metrics, and dashboards to expect

---

## Task 9: Document POC Results and Next Steps

**Objective**: Capture findings, screenshots, and remaining work for the team.

**What to do**:

- Take screenshots of Jaeger showing:
  - The service list with "rippled"
  - A trace with the full span tree
  - Span detail view showing attributes
- Document any issues encountered (build issues, SDK quirks, missing attributes)
- Note performance observations (build time impact, any noticeable runtime overhead)
- Write a short summary of what the POC proves and what it doesn't cover yet:
  - **Proves**: OTel SDK integrates with rippled, OTLP export works, RPC traces visible
  - **Doesn't cover**: Cross-node P2P context propagation, consensus tracing, protobuf trace context, W3C traceparent header extraction, tail-based sampling, production deployment
- Outline next steps (mapping to the full plan phases):
  - [Phase 2](./06-implementation-phases.md) completion: [W3C header extraction](./02-design-decisions.md) (§2.5), WebSocket tracing, all [RPC handlers](./01-architecture-analysis.md) (§1.6)
  - [Phase 3](./06-implementation-phases.md): [Protobuf `TraceContext` message](./04-code-samples.md) (§4.4), [transaction relay tracing](./04-code-samples.md) (§4.5.1) across nodes
  - [Phase 4](./06-implementation-phases.md): [Consensus round and phase tracing](./04-code-samples.md) (§4.5.2)
  - [Phase 5](./06-implementation-phases.md): [Production collector config](./05-configuration-reference.md) (§5.5.2), [Grafana dashboards](./07-observability-backends.md) (§7.6), [alerting](./07-observability-backends.md) (§7.6.3)

**Reference**:

- [06-implementation-phases.md §6.1](./06-implementation-phases.md) — Full 5-phase timeline overview and Gantt chart
- [06-implementation-phases.md §6.10](./06-implementation-phases.md) — Crawl-Walk-Run strategy: POC is the CRAWL phase, next steps are WALK and RUN
- [06-implementation-phases.md §6.12](./06-implementation-phases.md) — Recommended implementation order (14 steps across 9 weeks)
- [03-implementation-strategy.md §3.9](./03-implementation-strategy.md) — Code intrusiveness assessment and risk matrix for each remaining component
- [07-observability-backends.md §7.2](./07-observability-backends.md) — Production backend selection (Tempo, Elastic APM, Honeycomb, Datadog)
- [02-design-decisions.md §2.5](./02-design-decisions.md) — Context propagation design: W3C HTTP headers, protobuf P2P, JobQueue internal
- [00-tracing-fundamentals.md](./00-tracing-fundamentals.md) — Reference for team onboarding on distributed tracing concepts

---

## Summary

| Task | Description                          | New Files | Modified Files | Depends On |
| ---- | ------------------------------------ | --------- | -------------- | ---------- |
| 0    | Docker observability stack           | 4         | 0              | —          |
| 1    | OTel C++ SDK dependency              | 0         | 2              | —          |
| 2    | Core Telemetry interface + NullImpl  | 3         | 0              | 1          |
| 3    | OTel-backed Telemetry implementation | 2         | 1              | 1, 2       |
| 4    | Application lifecycle integration    | 0         | 3              | 2, 3       |
| 5    | Instrumentation macros               | 1         | 0              | 2          |
| 6    | Instrument RPC ServerHandler         | 0         | 1              | 4, 5       |
| 7    | Instrument RPC command execution     | 0         | 1              | 4, 5       |
| 8    | End-to-end verification              | 0         | 0              | 0-7        |
| 9    | Document results and next steps      | 1         | 0              | 8          |

**Parallel work**: Tasks 0 and 1 can run in parallel. Tasks 2 and 5 have no dependency on each other. Tasks 6 and 7 can be done in parallel once Tasks 4 and 5 are complete.

---

## Next Steps (Post-POC)

### Metrics Pipeline for Grafana Dashboards

The current POC exports **traces only**. Grafana's Explore view can query Jaeger for individual traces, but time-series charts (latency histograms, request throughput, error rates) require a **metrics pipeline**. To enable this:

1. **Add a `spanmetrics` connector** to the OTel Collector config that derives RED metrics (Rate, Errors, Duration) from trace spans automatically:

   ```yaml
   connectors:
     spanmetrics:
       histogram:
         explicit:
           buckets: [1ms, 5ms, 10ms, 25ms, 50ms, 100ms, 250ms, 500ms, 1s, 5s]
       dimensions:
         - name: xrpl.rpc.command
         - name: xrpl.rpc.status

   exporters:
     prometheus:
       endpoint: 0.0.0.0:8889

   service:
     pipelines:
       traces:
         receivers: [otlp]
         processors: [batch]
         exporters: [debug, otlp/jaeger, spanmetrics]
       metrics:
         receivers: [spanmetrics]
         exporters: [prometheus]
   ```

2. **Add Prometheus** to the Docker Compose stack to scrape the collector's metrics endpoint.

3. **Add Prometheus as a Grafana datasource** and build dashboards for:
   - RPC request latency (p50/p95/p99) by command
   - RPC throughput (requests/sec) by command
   - Error rate by command
   - Span duration distribution

### Additional Instrumentation

- **W3C `traceparent` header extraction** in `ServerHandler` to support cross-service context propagation from external callers
- **WebSocket RPC tracing** in `ServerHandler::onWSMessage()`
- **Transaction relay tracing** across nodes using protobuf `TraceContext` messages
- **Consensus round and phase tracing** for validator coordination visibility
- **Ledger close tracing** to measure close-to-validated latency

### Production Hardening

- **Tail-based sampling** in the OTel Collector to reduce volume while retaining error/slow traces
- **TLS configuration** for the OTLP exporter in production deployments
- **Resource limits** on the batch processor queue to prevent unbounded memory growth
- **Health monitoring** for the telemetry pipeline itself (collector lag, export failures)

### POC Lessons Learned

Issues encountered during POC implementation that inform future work:

| Issue                                                                                              | Resolution                                                                    | Impact on Future Work                                            |
| -------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------- | ---------------------------------------------------------------- |
| Conan lockfile rejected `opentelemetry-cpp/1.18.0`                                                 | Used `--lockfile=""` to bypass                                                | Lockfile must be regenerated when adding new dependencies        |
| Conan package only builds OTLP HTTP exporter, not gRPC                                             | Switched from gRPC to HTTP exporter (`localhost:4318/v1/traces`)              | HTTP exporter is the default; gRPC requires custom Conan profile |
| CMake target `opentelemetry-cpp::api` etc. don't exist in Conan package                            | Use umbrella target `opentelemetry-cpp::opentelemetry-cpp`                    | Conan targets differ from upstream CMake targets                 |
| OTel Collector `logging` exporter deprecated                                                       | Renamed to `debug` exporter                                                   | Use `debug` in all collector configs going forward               |
| Macro parameter `telemetry` collided with `::xrpl::telemetry::` namespace                          | Renamed macro params to `_tel_obj_`, `_span_name_`                            | Avoid common words as macro parameter names                      |
| `opentelemetry::trace::Scope` creates new context on move                                          | Store scope as member, create once in constructor                             | SpanGuard move semantics need care with Scope lifecycle          |
| `TracerProviderFactory::Create` returns `unique_ptr<sdk::TracerProvider>`, not `nostd::shared_ptr` | Use `std::shared_ptr` member, wrap in `nostd::shared_ptr` for global provider | OTel SDK factory return types don't match API provider types     |
