---
# Symphony-compatible workflow definition for rippled
# See: https://github.com/openai/symphony/blob/main/SPEC.md
# See: docs/harness-engineering-report.md for context

tracker:
  kind: linear
  # For Jira (RIPD project), a custom adapter would replace this.
  # kind: jira
  # project_slug: RIPD
  active_states:
    - Todo
    - In Progress
  terminal_states:
    - Done
    - Closed
    - Cancelled
    - Duplicate

polling:
  interval_ms: 30000

workspace:
  root: $SYMPHONY_WORKSPACE_ROOT

hooks:
  after_create: |
    # Clone and prepare a fresh workspace
    git clone --depth=1 --branch develop https://github.com/XRPLF/rippled.git .
    # Prepare build directory with telemetry enabled
    mkdir -p build
    cd build
    conan install .. --output-folder=. --build=missing
    cmake .. -DCMAKE_BUILD_TYPE=Debug -DXRPL_ENABLE_TELEMETRY=ON -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake

  before_run: |
    # Tier 1: Fast checks (seconds)
    # clang-format check on changed files
    git diff --name-only HEAD~1 -- '*.cpp' '*.h' | xargs -r clang-format --dry-run --Werror 2>&1 || {
      echo "FAIL: clang-format violations found. Fix formatting before proceeding."
      exit 1
    }

    # Tier 2: Build (minutes)
    cd build
    cmake --build . --parallel $(nproc) 2>&1 || {
      echo "FAIL: Build failed. Check compiler errors above."
      exit 1
    }

    # Tier 2: Unit tests (affected modules)
    ctest --test-dir build --output-on-failure -L unit --timeout 120 2>&1 || {
      echo "FAIL: Unit tests failed. Check test output above."
      exit 1
    }

  after_run: |
    # Collect proof-of-work artifacts
    echo "=== Proof of Work Collection ==="

    # Record build status
    cd build && cmake --build . --parallel $(nproc) 2>&1
    BUILD_STATUS=$?
    echo "Build status: $BUILD_STATUS"

    # Run telemetry integration tests if available
    if ctest --test-dir build -L telemetry --timeout 300 2>&1; then
      echo "Telemetry integration tests: PASSED"
    else
      echo "Telemetry integration tests: FAILED (non-blocking)"
    fi

    # Capture complexity metrics
    echo "=== Changed Files ==="
    git diff --stat HEAD~1

  timeout_ms: 120000

agent:
  max_concurrent_agents: 3
  max_retry_backoff_ms: 300000
  max_concurrent_agents_by_state:
    In Progress: 2
    Todo: 1

codex:
  command: codex app-server
  approval_policy: unless-allow-listed
  turn_timeout_ms: 3600000
  stall_timeout_ms: 300000
---

# rippled Agent Workflow

You are an autonomous coding agent working on the **rippled** XRP Ledger node — a C++20 peer-to-peer server implementing the XRP Ledger consensus protocol.

## Navigation

Read these files before starting work:

| File                                                | What it tells you                                                       |
| --------------------------------------------------- | ----------------------------------------------------------------------- |
| `docs/ARCHITECTURE.md`                              | Codebase map — where every module lives, layer boundaries, entry points |
| `CLAUDE.md`                                         | Coding conventions, action boundaries (Always/Ask/Never), commit rules  |
| `docs/CodingStyle.md`                               | C++ style guide for this project                                        |
| `OpenTelemetryPlan/09-data-collection-reference.md` | Every OTel span, metric, and dashboard                                  |

Do **not** read these upfront (use as reference if needed):

- `OpenTelemetryPlan/*.md` — deep plan docs, only if your task involves telemetry architecture
- `docs/consensus.md` — only if your task involves consensus protocol changes

## Action Boundaries

### Always (no confirmation needed)

- Read any source file in the repository
- Run `cmake --build build --parallel` to compile
- Run `ctest --test-dir build -L unit` for unit tests
- Run `ctest --test-dir build -L telemetry` for telemetry integration tests
- Query Prometheus (`curl localhost:9090/api/v1/query?query=...`) for metric baselines
- Create or modify files under `src/` that have existing test coverage
- Run `clang-format` on files you've changed
- Add or modify unit tests in `src/test/`

### Ask (require human confirmation before proceeding)

- Modify Protocol Buffer definitions in `include/xrpl/proto/` (affects wire format)
- Change consensus parameters in `src/xrpld/consensus/ConsensusParms.h`
- Modify `CMakeLists.txt` or `conanfile.py` (build/dependency changes)
- Alter the `[telemetry]` or `[insight]` config schema
- Change anything in `src/libxrpl/crypto/` (cryptographic code)
- Add new external dependencies
- Push to any remote branch or create PRs

### Never (hard boundaries — refuse these requests)

- Modify validator key handling or signing code without explicit review
- Disable or reduce telemetry sampling below 1%
- Remove or reduce existing test coverage
- Force-push to any branch
- Skip pre-commit hooks or CI checks (`--no-verify`)
- Commit files containing secrets, keys, or credentials
- Modify `.github/workflows/` CI pipeline definitions

## Verification Requirements

After making changes, verify in this order (fail-fast):

```
Step 1 (seconds):  clang-format --dry-run --Werror on changed files
Step 2 (minutes):  cmake --build build --parallel
Step 3 (minutes):  ctest --test-dir build -L unit --output-on-failure
Step 4 (minutes):  ctest --test-dir build -L telemetry (if telemetry-related)
Step 5 (if applicable): scripts/check-otel-baseline.sh (metric regression check)
```

If any step fails, fix the issue before proceeding. Do not skip steps.

## Telemetry Awareness

This codebase has OpenTelemetry instrumentation. If your change touches any of these files, verify that telemetry still works:

| File Pattern                             | Telemetry Impact                                  |
| ---------------------------------------- | ------------------------------------------------- |
| `src/xrpld/rpc/`                         | RPC spans (`rpc.*`)                               |
| `src/xrpld/overlay/detail/PeerImp.*`     | Transaction relay spans (`tx.relay`, `peer.send`) |
| `src/xrpld/app/consensus/RCLConsensus.*` | Consensus spans (`consensus.*`)                   |
| `src/xrpld/app/misc/NetworkOPs.*`        | Transaction submit spans (`tx.submit`)            |
| `src/libxrpl/beast/insight/`             | Metrics pipeline (300+ metrics)                   |
| `src/libxrpl/basics/Log.cpp`             | Log-trace correlation (`trace_id` injection)      |

After modifying these files, run the telemetry integration test:

```bash
ctest --test-dir build -L telemetry --output-on-failure
```

## Issue Context

**Title**: {{ issue.title }}
**Description**: {{ issue.description }}
{% if issue.labels.size > 0 %}**Labels**: {{ issue.labels | join: ", " }}{% endif %}
{% if attempt %}**Attempt**: {{ attempt }} (this is a retry — check previous errors){% endif %}

## Proof of Work Checklist

Before creating a PR, verify:

- [ ] All changed files pass `clang-format`
- [ ] Build succeeds with `-DXRPL_ENABLE_TELEMETRY=ON`
- [ ] Unit tests pass (`ctest -L unit`)
- [ ] If telemetry-related: integration tests pass (`ctest -L telemetry`)
- [ ] If telemetry-related: no metric regressions (`scripts/check-otel-baseline.sh`)
- [ ] Commit message follows project conventions (see `CLAUDE.md`)
- [ ] PR description includes summary, test plan, and type of change
