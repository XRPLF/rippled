# Harness Engineering for rippled: Leveraging OTel as the Foundation for Agent-First Development

> **Date**: 2026-03-09
> **Status**: Investigation Report
> **Context**: OpenAI's [Harness Engineering](https://openai.com/index/harness-engineering/) blog post + rippled OTel initiative (12-PR chain, Phases 1-11)
> **Companion Files**: [ARCHITECTURE.md](ARCHITECTURE.md) | [WORKFLOW.md](WORKFLOW.md)

---

## Executive Summary

OpenAI coined **"Harness Engineering"** to describe the discipline of building infrastructure _around_ AI coding agents — not the agents themselves. Their thesis: **"Give Codex a map, not a 1,000-page instruction manual."** They shipped [Symphony](https://github.com/openai/symphony), an open-source framework that turns issue tracker tickets into autonomous "implementation runs" with proof-of-work requirements (CI pass, PR review, walkthrough) before merging.

The rippled OpenTelemetry initiative — now spanning 12 chained PRs from Phase 1a through Phase 11 planning — is **uniquely positioned** to become the observability backbone of a harness engineering strategy. The OTel work already delivers 3 of the 5 harness engineering principles. This report maps the gap and proposes a concrete adoption path.

```mermaid
flowchart TB
    subgraph today["What We Have Today"]
        direction TB
        OTel["OTel Instrumentation<br/>16 spans, 300+ metrics<br/>8 Grafana dashboards"]
        CLAUDE["CLAUDE.md<br/>Agent instruction file"]
        CI["CI Pipeline<br/>Build + test gates"]
        Plans["Plan Docs<br/>10 architecture docs"]
    end

    subgraph gap["The Gap"]
        direction TB
        ARCH["ARCHITECTURE.md<br/>(codebase map, sample ready)"]
        LINT["Semantic Linting<br/>(agent-friendly errors)"]
        BOUND["Three-Tier Boundaries<br/>(Always/Ask/Never)"]
        VERIFY["Deterministic Verification<br/>(OTel-powered)"]
        WORKFLOW["WORKFLOW.md<br/>(agent orchestration, sample ready)"]
    end

    subgraph future["Agent-First rippled"]
        direction TB
        AUTO["Autonomous<br/>Implementation Runs"]
        PROOF["Proof of Work<br/>(traces + metrics + CI)"]
        SCALE["Parallelized<br/>Agent Development"]
    end

    today --> gap --> future

    style today fill:#1b5e20,stroke:#0d3d14,color:#fff
    style gap fill:#bf360c,stroke:#8c2809,color:#fff
    style future fill:#0d47a1,stroke:#082f6a,color:#fff
    style OTel fill:#1b5e20,stroke:#0d3d14,color:#fff
    style CLAUDE fill:#1b5e20,stroke:#0d3d14,color:#fff
    style CI fill:#1b5e20,stroke:#0d3d14,color:#fff
    style Plans fill:#1b5e20,stroke:#0d3d14,color:#fff
    style ARCH fill:#e65100,stroke:#bf360c,color:#fff
    style LINT fill:#e65100,stroke:#bf360c,color:#fff
    style BOUND fill:#e65100,stroke:#bf360c,color:#fff
    style VERIFY fill:#e65100,stroke:#bf360c,color:#fff
    style WORKFLOW fill:#e65100,stroke:#bf360c,color:#fff
    style AUTO fill:#1565c0,stroke:#0d47a1,color:#fff
    style PROOF fill:#1565c0,stroke:#0d47a1,color:#fff
    style SCALE fill:#1565c0,stroke:#0d47a1,color:#fff
```

---

## Part 1: What Is Harness Engineering?

### 1.1 Origin and Core Concept

OpenAI's engineering team discovered that **prompt engineering alone doesn't scale** for AI coding agents. They tried the "giant AGENTS.md" approach — a massive instruction file with everything an agent needs to know. It failed:

- Too much context crowds out the actual task
- Everything marked "important" means nothing is important
- The file goes stale as the codebase evolves

What actually worked was building **infrastructure** that makes agents effective:

| Failed Approach               | What Worked Instead                       |
| ----------------------------- | ----------------------------------------- |
| Giant instruction files       | Short map pointing to deeper docs         |
| Detailed step-by-step prompts | Strict architecture enforced by linters   |
| Manual agent supervision      | Fast, deterministic feedback loops        |
| One-off agent runs            | Repeatable autonomous implementation runs |

### 1.2 The Five Principles

| #   | Principle                      | Definition                                                                    | rippled Status                                           |
| --- | ------------------------------ | ----------------------------------------------------------------------------- | -------------------------------------------------------- |
| 1   | **Deterministic Verification** | Verify agent output with automated checks — don't trust, verify               | Partial (CI exists, OTel validation planned in Phase 10) |
| 2   | **Semantic Linting**           | Linter messages teach agents how to fix violations in one shot                | Missing                                                  |
| 3   | **Three-Tier Boundaries**      | Every harness defines Always / Ask / Never action categories                  | Partial (CLAUDE.md has some rules)                       |
| 4   | **Fail-Fast Feedback**         | Fast checks first (lint, typecheck), slow checks later (integration)          | Partial (CI runs, but not agent-optimized)               |
| 5   | **Architecture as Map**        | [ARCHITECTURE.md](ARCHITECTURE.md) tells agents _where_ things are, not _why_ | Missing (plan docs explain _why_, not _where_)           |

### 1.3 Symphony: The Orchestration Layer

OpenAI released [Symphony](https://github.com/openai/symphony) — an open-source framework (Elixir/BEAM) that:

1. **Polls an issue tracker** (Linear) for work items
2. **Creates isolated workspaces** per issue (git worktrees)
3. **Spawns coding agents** (Codex) with a repo-defined `WORKFLOW.md` prompt
4. **Requires proof of work** before merging: CI status, PR review feedback, complexity analysis, walkthrough videos
5. **Provides observability** into concurrent agent runs (structured logs, status surface)

The key insight: `WORKFLOW.md` is version-controlled _in the repo_, so the agent's policy evolves with the code.

```mermaid
flowchart TB
    subgraph symphony["Symphony Orchestrator"]
        POLL["Poll Issue Tracker<br/>(Linear/Jira)"]
        DISPATCH["Dispatch to<br/>Isolated Workspace"]
        MONITOR["Monitor Agent<br/>Progress"]
    end

    subgraph agent["Coding Agent Run"]
        READ_WF["Read WORKFLOW.md"]
        IMPLEMENT["Implement<br/>Changes"]
        VALIDATE["Self-Validate<br/>(lint + test + OTel)"]
        PR["Create PR with<br/>Proof of Work"]
    end

    subgraph verify["Proof of Work"]
        CI_PASS["CI Passes"]
        METRICS["No Metric<br/>Regression"]
        TRACES["Traces Show<br/>Correct Behavior"]
        REVIEW["PR Review<br/>Feedback"]
    end

    POLL --> DISPATCH --> READ_WF
    READ_WF --> IMPLEMENT --> VALIDATE --> PR
    PR --> CI_PASS
    PR --> METRICS
    PR --> TRACES
    PR --> REVIEW

    style symphony fill:#4a148c,stroke:#2e0d57,color:#fff
    style agent fill:#1b5e20,stroke:#0d3d14,color:#fff
    style verify fill:#bf360c,stroke:#8c2809,color:#fff
    style POLL fill:#6a1b9a,stroke:#4a148c,color:#fff
    style DISPATCH fill:#6a1b9a,stroke:#4a148c,color:#fff
    style MONITOR fill:#6a1b9a,stroke:#4a148c,color:#fff
    style READ_WF fill:#2e7d32,stroke:#1b5e20,color:#fff
    style IMPLEMENT fill:#2e7d32,stroke:#1b5e20,color:#fff
    style VALIDATE fill:#2e7d32,stroke:#1b5e20,color:#fff
    style PR fill:#2e7d32,stroke:#1b5e20,color:#fff
    style CI_PASS fill:#d84315,stroke:#bf360c,color:#fff
    style METRICS fill:#d84315,stroke:#bf360c,color:#fff
    style TRACES fill:#d84315,stroke:#bf360c,color:#fff
    style REVIEW fill:#d84315,stroke:#bf360c,color:#fff
```

---

## Part 2: OTel as the Observability Backbone for Harness Engineering

### 2.1 The Connection: Observability Enables Agent Autonomy

The most overlooked aspect of harness engineering is **how agents verify their own work**. OpenAI's Symphony requires "proof of work" — but what constitutes proof? For a systems project like rippled, CI pass alone is insufficient. An agent needs to verify:

- Did the change introduce latency regressions? (traces)
- Are consensus rounds still completing within tolerance? (metrics)
- Does the transaction lifecycle still trace end-to-end? (cross-node traces)
- Are error rates stable? (dashboards)

**The OTel initiative is building exactly this verification infrastructure.**

### 2.2 Mapping OTel Phases to Harness Capabilities

| OTel Phase                          | Harness Capability Unlocked                                      |
| ----------------------------------- | ---------------------------------------------------------------- |
| **Phase 1-2**: Core + RPC Tracing   | Agents can verify RPC handler changes don't regress latency      |
| **Phase 3**: Transaction Tracing    | Agents can verify cross-node transaction propagation works       |
| **Phase 4**: Consensus Tracing      | Agents can verify consensus-touching changes don't break timing  |
| **Phase 5**: Dashboards + Runbook   | Agents can reference dashboards as "expected state" baselines    |
| **Phase 6-7**: Metrics Pipeline     | Agents can check 300+ metrics for regressions after changes      |
| **Phase 8**: Log-Trace Correlation  | Agents can trace from an error log line to the full request path |
| **Phase 9**: Metric Gap Fill        | Agents get coverage of NodeStore I/O, cache rates, TxQ depth     |
| **Phase 10**: Synthetic Workload    | **Direct harness engineering**: automated telemetry validation   |
| **Phase 11**: Third-Party Pipelines | Agents can monitor external network health indicators            |

### 2.3 Phase 10 IS Harness Engineering

Phase 10 (Synthetic Workload Generation & Telemetry Validation) is the most directly aligned with harness engineering:

- **5-node validator harness** — isolated test environment for agent runs
- **RPC load generator + tx submitter** — deterministic workload to produce consistent telemetry
- **Automated validation of 16 spans + 300+ metrics + 10 dashboards** — the "proof of work" verification layer
- **Performance benchmarks** — regression detection
- **CI integration** — automated gate

This is exactly OpenAI's Principle #1 (Deterministic Verification) applied to a C++ blockchain node.

### 2.4 OTel-Powered Agent Verification Flow

```mermaid
flowchart TB
    subgraph agent_run["Agent Implementation Run"]
        CHANGE["Agent Makes<br/>Code Change"]
        BUILD["Build + Unit Tests"]
        DEPLOY["Deploy to 5-Node<br/>Test Harness"]
    end

    subgraph otel_verify["OTel Verification Layer (Phase 10)"]
        WORKLOAD["Run Synthetic<br/>Workload"]
        SPANS["Validate 16 Spans<br/>Present + Correct"]
        METRICS["Check 300+ Metrics<br/>No Regressions"]
        DASH["Dashboard Health<br/>All Panels Populated"]
        PERF["Performance Bench<br/>< Tolerance Thresholds"]
    end

    subgraph outcome["Outcome"]
        PASS["Proof of Work:<br/>All Checks Pass"]
        FAIL["Rejection:<br/>Specific Failure<br/>Reported to Agent"]
    end

    CHANGE --> BUILD --> DEPLOY
    DEPLOY --> WORKLOAD
    WORKLOAD --> SPANS
    WORKLOAD --> METRICS
    WORKLOAD --> DASH
    WORKLOAD --> PERF
    SPANS -->|pass| PASS
    METRICS -->|pass| PASS
    DASH -->|pass| PASS
    PERF -->|pass| PASS
    SPANS -->|fail| FAIL
    METRICS -->|fail| FAIL
    DASH -->|fail| FAIL
    PERF -->|fail| FAIL

    style agent_run fill:#1b5e20,stroke:#0d3d14,color:#fff
    style otel_verify fill:#e65100,stroke:#bf360c,color:#fff
    style outcome fill:#0d47a1,stroke:#082f6a,color:#fff
    style CHANGE fill:#2e7d32,stroke:#1b5e20,color:#fff
    style BUILD fill:#2e7d32,stroke:#1b5e20,color:#fff
    style DEPLOY fill:#2e7d32,stroke:#1b5e20,color:#fff
    style WORKLOAD fill:#f57c00,stroke:#e65100,color:#fff
    style SPANS fill:#f57c00,stroke:#e65100,color:#fff
    style METRICS fill:#f57c00,stroke:#e65100,color:#fff
    style DASH fill:#f57c00,stroke:#e65100,color:#fff
    style PERF fill:#f57c00,stroke:#e65100,color:#fff
    style PASS fill:#1565c0,stroke:#0d47a1,color:#fff
    style FAIL fill:#c62828,stroke:#b71c1c,color:#fff
```

---

## Part 3: Building the Surrounding Harness Infrastructure

### 3.1 Gap Analysis: What's Missing

The OTel work provides the **verification layer** (Principle #1). Here's what's needed to complete the harness:

```mermaid
---
config:
    quadrantChart:
        chartWidth: 900
        chartHeight: 900
        pointLabelFontSize: 14
        pointRadius: 5
        titleFontSize: 22
        quadrantLabelFontSize: 16
        xAxisLabelFontSize: 14
        yAxisLabelFontSize: 14
        quadrantInternalBorderStrokeWidth: 2
        titlePadding: 20
---
quadrantChart
    title Harness Engineering Readiness
    x-axis Low Effort --> High Effort
    y-axis Low Impact --> High Impact
    quadrant-1 Quick Wins
    quadrant-2 Strategic Investment
    quadrant-3 Nice to Have
    quadrant-4 Defer

    ARCHITECTURE.md: [0.15, 0.95]
    Three-Tier Boundaries: [0.15, 0.78]
    Semantic Linting Config: [0.40, 0.68]
    WORKFLOW.md for Symphony: [0.65, 0.72]
    OTel Verification Scripts: [0.45, 0.92]
    Agent-Friendly CI Pipeline: [0.35, 0.55]
    Jira-Symphony Integration: [0.80, 0.42]
    Full Symphony Deployment: [0.88, 0.58]
```

### 3.2 Principle #5: Architecture as Map ([ARCHITECTURE.md](ARCHITECTURE.md))

**Current state**: The OTel plan docs (01-architecture-analysis.md) describe rippled's architecture in detail, but they're _plan docs_ — they explain _why_ to instrument, not _where things are_ for an agent navigating the codebase.

**What's needed**: A top-level [`ARCHITECTURE.md`](ARCHITECTURE.md) that serves as the agent's map.

**Sample created**: See [ARCHITECTURE.md](ARCHITECTURE.md) — a working example covering all sections below.

```
ARCHITECTURE.md
├── Layer Diagram (5 layers: RPC → NetworkOPs → Consensus → Ledger → NodeStore)
├── Module Map (directory → purpose, one line each)
├── Key Abstractions (Application, Overlay, RCLConsensus, JobQueue, beast::insight)
├── Entry Points (where execution starts for each operation type)
├── Data Flow (transaction lifecycle, ledger close, peer message handling)
├── Build Targets (libxrpl, xrpld, unit tests)
└── Telemetry Map (which spans/metrics cover which modules — links to 09-data-collection-reference.md)
```

**Key principle**: Keep it under 300 lines. Link to deeper docs rather than inlining detail. Agents need a map, not an encyclopedia.

### 3.3 Principle #3: Three-Tier Boundaries

**Current state**: `CLAUDE.md` has workflow rules but doesn't explicitly categorize actions into Always/Ask/Never.

**Proposed addition to CLAUDE.md**:

```markdown
## Agent Action Boundaries

### Always (autonomous — no confirmation needed)

- Run `cmake --build` and unit tests
- Read any source file
- Create/modify files under `src/` that are covered by existing tests
- Run the OTel integration test suite
- Query Prometheus/Grafana for metric baselines

### Ask (require human confirmation)

- Modify Protocol Buffer definitions (affects wire format)
- Change consensus timing parameters
- Modify CMakeLists.txt or Conan dependencies
- Alter the telemetry configuration schema
- Push to any remote branch

### Never (hard boundaries — agent must refuse)

- Modify cryptographic signing code without review
- Disable or weaken telemetry sampling below 1%
- Remove or reduce test coverage
- Change validator key handling
- Force-push to any branch
```

### 3.4 Principle #2: Semantic Linting

**Current state**: rippled uses clang-format and has CI checks, but linter output isn't optimized for agent consumption.

**What makes linting "semantic"**: The linter error message should teach the agent how to fix the violation in one shot.

**Proposed improvements**:

| Tool         | Current            | Agent-Friendly Improvement                                                              |
| ------------ | ------------------ | --------------------------------------------------------------------------------------- |
| clang-format | Format check in CI | Add `.clang-format` with explicit rationale comments; pre-commit hook                   |
| clang-tidy   | Not integrated     | Add `.clang-tidy` config targeting rippled patterns (RAII, span safety)                 |
| Custom lint  | None               | Script that checks: telemetry macro usage, span attribute completeness, config coverage |
| OTel lint    | None               | Validate span names follow `<component>.<operation>` convention                         |

**Example of a semantic lint rule for OTel**:

```
ERROR: Span "handleRequest" does not follow naming convention.
       Expected: "<component>.<operation>" (e.g., "rpc.handle_request")
       Fix: Rename to "rpc.handle_request" in src/xrpld/rpc/ServerHandler.cpp:142
       Reference: OpenTelemetryPlan/02-design-decisions.md §2.3
```

### 3.5 Principle #4: Fail-Fast Feedback Pipeline

**Current state**: CI runs the full build + test suite. No tiered feedback.

**Proposed agent-optimized CI pipeline**:

```mermaid
flowchart LR
    subgraph tier1["Tier 1: Seconds"]
        LINT["clang-format<br/>+ clang-tidy"]
        SCHEMA["Config schema<br/>validation"]
    end

    subgraph tier2["Tier 2: Minutes"]
        BUILD["cmake build<br/>(TELEMETRY=ON)"]
        UNIT["Unit tests<br/>(telemetry module)"]
    end

    subgraph tier3["Tier 3: Minutes"]
        INTEG["OTel Integration<br/>Test (5-node)"]
        METRICS_CHK["Metric Regression<br/>Check"]
    end

    subgraph tier4["Tier 4: Extended"]
        PERF["Performance<br/>Benchmark"]
        FULL["Full Test<br/>Suite"]
    end

    tier1 -->|pass| tier2 -->|pass| tier3 -->|pass| tier4

    style tier1 fill:#1b5e20,stroke:#0d3d14,color:#fff
    style tier2 fill:#bf360c,stroke:#8c2809,color:#fff
    style tier3 fill:#0d47a1,stroke:#082f6a,color:#fff
    style tier4 fill:#4a148c,stroke:#2e0d57,color:#fff
    style LINT fill:#2e7d32,stroke:#1b5e20,color:#fff
    style SCHEMA fill:#2e7d32,stroke:#1b5e20,color:#fff
    style BUILD fill:#d84315,stroke:#bf360c,color:#fff
    style UNIT fill:#d84315,stroke:#bf360c,color:#fff
    style INTEG fill:#1565c0,stroke:#0d47a1,color:#fff
    style METRICS_CHK fill:#1565c0,stroke:#0d47a1,color:#fff
    style PERF fill:#6a1b9a,stroke:#4a148c,color:#fff
    style FULL fill:#6a1b9a,stroke:#4a148c,color:#fff
```

Key: **fail at Tier 1 → agent gets feedback in seconds**, not after a 30-minute build.

### 3.6 [WORKFLOW.md](WORKFLOW.md): Agent Orchestration Policy

If rippled adopted Symphony (or a similar orchestrator), the [`WORKFLOW.md`](WORKFLOW.md) would define the agent's runtime behavior.

**Sample created**: See [WORKFLOW.md](WORKFLOW.md) — a complete, Symphony-spec-compatible workflow definition including:

- **Front matter**: Tracker config, polling interval, workspace hooks, agent concurrency, Codex settings
- **Hooks**: `after_create` (clone + build setup), `before_run` (tiered lint -> build -> test), `after_run` (proof-of-work collection)
- **Prompt body**: Navigation table ([ARCHITECTURE.md](ARCHITECTURE.md), CLAUDE.md, CodingStyle.md), three-tier action boundaries (Always/Ask/Never), fail-fast verification steps, telemetry-aware file patterns, Liquid template variables for issue context, proof-of-work checklist

The [WORKFLOW.md](WORKFLOW.md) is version-controlled in the repo so agent policy evolves with the code.

---

## Part 4: Adoption Roadmap

### 4.1 Three-Phase Adoption

```mermaid
flowchart TB
    subgraph phase_a["Phase A: Foundation (Now - 2 weeks)"]
        direction TB
        A1["Create ARCHITECTURE.md<br/>(codebase map)"]
        A2["Add Three-Tier Boundaries<br/>to CLAUDE.md"]
        A3["Add pre-commit hooks<br/>(clang-format)"]
        A4["Write OTel baseline<br/>verification script"]
    end

    subgraph phase_b["Phase B: Feedback Loops (Weeks 3-6)"]
        direction TB
        B1["Semantic linting config<br/>(clang-tidy + custom)"]
        B2["Tiered CI pipeline<br/>(fast → slow)"]
        B3["OTel metric regression<br/>check in CI"]
        B4["Phase 10: Synthetic<br/>workload harness"]
    end

    subgraph phase_c["Phase C: Orchestration (Weeks 7-10)"]
        direction TB
        C1["WORKFLOW.md for<br/>agent orchestration"]
        C2["Symphony or custom<br/>orchestrator setup"]
        C3["Jira integration<br/>(RIPD project)"]
        C4["First autonomous<br/>implementation runs"]
    end

    phase_a --> phase_b --> phase_c

    style phase_a fill:#1b5e20,stroke:#0d3d14,color:#fff
    style phase_b fill:#bf360c,stroke:#8c2809,color:#fff
    style phase_c fill:#0d47a1,stroke:#082f6a,color:#fff
    style A1 fill:#2e7d32,stroke:#1b5e20,color:#fff
    style A2 fill:#2e7d32,stroke:#1b5e20,color:#fff
    style A3 fill:#2e7d32,stroke:#1b5e20,color:#fff
    style A4 fill:#2e7d32,stroke:#1b5e20,color:#fff
    style B1 fill:#d84315,stroke:#bf360c,color:#fff
    style B2 fill:#d84315,stroke:#bf360c,color:#fff
    style B3 fill:#d84315,stroke:#bf360c,color:#fff
    style B4 fill:#d84315,stroke:#bf360c,color:#fff
    style C1 fill:#1565c0,stroke:#0d47a1,color:#fff
    style C2 fill:#1565c0,stroke:#0d47a1,color:#fff
    style C3 fill:#1565c0,stroke:#0d47a1,color:#fff
    style C4 fill:#1565c0,stroke:#0d47a1,color:#fff
```

### 4.2 Effort Estimates

| Phase | Task                               | Effort     | Dependencies                       |
| ----- | ---------------------------------- | ---------- | ---------------------------------- |
| **A** | [ARCHITECTURE.md](ARCHITECTURE.md) | 2d         | None                               |
| **A** | CLAUDE.md boundaries update        | 0.5d       | None                               |
| **A** | Pre-commit hooks                   | 1d         | None                               |
| **A** | OTel baseline verification script  | 2d         | OTel Phases 1-6 merged             |
| **B** | Semantic linting config            | 2d         | Phase A complete                   |
| **B** | Tiered CI pipeline                 | 3d         | Phase A complete                   |
| **B** | Metric regression CI check         | 2d         | OTel Phase 7 merged                |
| **B** | Phase 10 synthetic workload        | 10d        | OTel Phase 9 merged                |
| **C** | [WORKFLOW.md](WORKFLOW.md)         | 1d         | Phases A+B complete                |
| **C** | Orchestrator setup                 | 3d         | [WORKFLOW.md](WORKFLOW.md) defined |
| **C** | Jira integration                   | 2d         | Orchestrator running               |
| **C** | First autonomous runs              | 2d         | Everything above                   |
|       | **Total**                          | **~30.5d** |                                    |

### 4.3 What to Do Immediately

These four items require no OTel dependencies and can start today:

1. **Create [`ARCHITECTURE.md`](ARCHITECTURE.md)** — 300-line codebase map covering the 5 layers (RPC, NetworkOPs, Consensus, Ledger, NodeStore), module directory map, entry points, and telemetry coverage map linking to `09-data-collection-reference.md`

2. **Add Three-Tier Boundaries to `CLAUDE.md`** — Explicit Always/Ask/Never categories (see Section 3.3 above)

3. **Add pre-commit hooks** — `clang-format` check + OTel span naming convention check

4. **Write `scripts/check-otel-baseline.sh`** — Script that starts the 5-node Docker stack, runs a synthetic workload, and validates all 16 spans are present with required attributes

---

## Part 5: Unique Advantages for rippled

### 5.1 Why rippled Is Especially Well-Suited

| Advantage                      | Explanation                                                                                        |
| ------------------------------ | -------------------------------------------------------------------------------------------------- |
| **Deterministic consensus**    | Consensus rounds produce predictable telemetry — ideal for regression detection                    |
| **Isolated test networks**     | 5-node validator harness gives agents a safe, isolated sandbox                                     |
| **Rich telemetry baseline**    | 16 spans + 300+ metrics + 8 dashboards = comprehensive "expected state"                            |
| **Protocol Buffer boundaries** | Wire format changes are explicit and lintable — agents can't silently break compatibility          |
| **Existing CLAUDE.md**         | Already have an agent instruction file — just needs boundary tiers                                 |
| **Modular plan docs**          | 10 architecture docs serve as deep reference material for [ARCHITECTURE.md](ARCHITECTURE.md) links |

### 5.2 Risks and Mitigations

| Risk                                               | Likelihood | Impact | Mitigation                                                                                 |
| -------------------------------------------------- | ---------- | ------ | ------------------------------------------------------------------------------------------ |
| Agent modifies consensus-critical code incorrectly | Medium     | High   | Three-tier boundary: consensus changes = "Ask" tier; OTel traces detect timing regressions |
| OTel verification adds CI time                     | Low        | Medium | Tiered pipeline — OTel checks only run after fast checks pass                              |
| Symphony/orchestrator maintenance burden           | Medium     | Medium | Start with manual agent runs using CLAUDE.md; orchestrator is Phase C                      |
| Metric baseline drift                              | Medium     | Low    | Regularly update baselines; use percentage thresholds not absolute values                  |
| C++ build times limit agent feedback speed         | High       | Medium | Incremental builds, ccache, focus on affected module tests first                           |

### 5.3 The Competitive Edge

Most blockchain projects have **no observability infrastructure at all**. The rippled OTel initiative gives the project:

1. **The only blockchain node with comprehensive OpenTelemetry instrumentation** — traces, metrics, logs, all correlated
2. **A deterministic verification layer** that AI agents can use to validate their own changes
3. **A path to autonomous development** where routine engineering tasks (bug fixes, feature additions, test expansion) can be handled by agents with proof-of-work requirements

This is not just observability — it's the foundation for **10x engineering velocity** through agent-first development.

---

## Part 6: OpenClaw and OpenFang — Agent Runtime Integration Opportunities

### 6.1 Project Overview

Two open-source projects in the agent ecosystem offer capabilities that map directly onto the harness engineering strategy:

| Project                                                 | Description                                                                                                                      | Language   | Stars | Key Insight                                                                                   |
| ------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------- | ---------- | ----- | --------------------------------------------------------------------------------------------- |
| **[OpenClaw](https://github.com/openclaw/openclaw)**    | Personal AI assistant platform with gateway, multi-channel inbox, skills system, and browser control                             | TypeScript | 296K  | Skills platform + multi-agent routing = harness orchestration layer                           |
| **[OpenFang](https://github.com/RightNow-AI/openfang)** | Agent Operating System — kernel-based architecture with autonomous "Hands," WASM sandbox, Merkle audit trail, 140+ API endpoints | Rust       | 13K   | Rust kernel + security-first design + built-in observability = production-grade agent harness |

### 6.2 OpenClaw: Skills-Based Agent Orchestration

OpenClaw's architecture is a **gateway-centric control plane** where agents connect via WebSocket, receive work, and execute through a skills system. Relevant capabilities for rippled:

**Skills Platform**

OpenClaw's managed skills system (ClawHub marketplace) maps to how rippled could package agent capabilities:

| OpenClaw Concept                  | rippled Harness Equivalent                                                                 |
| --------------------------------- | ------------------------------------------------------------------------------------------ |
| **Skill** (bundled capability)    | OTel verification script, lint checker, metric regression detector                         |
| **ClawHub** (skill marketplace)   | Internal skill registry for rippled-specific agent capabilities                            |
| **Workspace skills** (repo-local) | `scripts/` directory with harness verification tools                                       |
| **Agent routing** (per-channel)   | Route different issue types to specialized agents (RPC agent, consensus agent, test agent) |

**Multi-Agent Routing**

OpenClaw can route inbound messages to isolated agents with separate workspaces and sessions. For rippled:

- **RPC Layer Agent** — handles `src/xrpld/rpc/` changes, runs RPC-specific OTel span validation
- **Consensus Agent** — handles `src/xrpld/consensus/` changes, runs timing-sensitive verification, always in "Ask" tier
- **Test Agent** — handles test expansion, runs the full suite, validates coverage

**Session-to-Session Coordination**

OpenClaw's `sessions_send` tool enables agent-to-agent communication. A supervisor agent could:

1. Receive a Jira ticket via channel adapter
2. Analyze scope, spawn a specialist agent
3. Monitor progress, collect proof of work
4. Report results back via the channel

**Practical Fit**: OpenClaw is a **medium-term option (Phase C)** as an alternative to Symphony for agent orchestration. Its TypeScript stack is more approachable than Symphony's Elixir/BEAM, and the skills platform provides a natural packaging mechanism for rippled-specific harness tools.

### 6.3 OpenFang: Rust-Native Agent OS

OpenFang is architecturally the closest match to rippled's needs — a Rust-based kernel with security-first design, built for autonomous agents.

**Architecture Alignment**

```mermaid
flowchart TB
    subgraph openfang["OpenFang Kernel"]
        direction TB
        KERNEL["openfang-kernel<br/>Orchestration, RBAC,<br/>scheduling, metering"]
        RUNTIME["openfang-runtime<br/>Agent loop, 53 tools,<br/>WASM sandbox, MCP"]
        MEMORY["openfang-memory<br/>SQLite + vector,<br/>session compaction"]
        API["openfang-api<br/>140+ REST/WS/SSE,<br/>dashboard"]
    end

    subgraph harness["rippled Harness Layer"]
        direction TB
        HAND_OTEL["OTel Verifier Hand<br/>(custom)"]
        HAND_BUILD["Build + Test Hand<br/>(custom)"]
        HAND_METRIC["Metric Regression<br/>Hand (custom)"]
        HAND_REVIEW["Code Review<br/>Hand (custom)"]
    end

    subgraph rippled_infra["rippled Infrastructure"]
        direction TB
        OTEL["OTel Stack<br/>Collector + Tempo +<br/>Grafana"]
        CI_SYS["CI Pipeline<br/>GitHub Actions"]
        HARNESS_5["5-Node Test<br/>Harness"]
        JIRA["Jira<br/>RIPD Project"]
    end

    KERNEL --> RUNTIME
    RUNTIME --> MEMORY
    RUNTIME --> API

    HAND_OTEL --> OTEL
    HAND_BUILD --> CI_SYS
    HAND_METRIC --> OTEL
    HAND_REVIEW --> HARNESS_5

    KERNEL --> HAND_OTEL
    KERNEL --> HAND_BUILD
    KERNEL --> HAND_METRIC
    KERNEL --> HAND_REVIEW

    JIRA -.->|"channel adapter"| API

    style openfang fill:#4a148c,stroke:#2e0d57,color:#fff
    style harness fill:#bf360c,stroke:#8c2809,color:#fff
    style rippled_infra fill:#1b5e20,stroke:#0d3d14,color:#fff
    style KERNEL fill:#6a1b9a,stroke:#4a148c,color:#fff
    style RUNTIME fill:#6a1b9a,stroke:#4a148c,color:#fff
    style MEMORY fill:#6a1b9a,stroke:#4a148c,color:#fff
    style API fill:#6a1b9a,stroke:#4a148c,color:#fff
    style HAND_OTEL fill:#d84315,stroke:#bf360c,color:#fff
    style HAND_BUILD fill:#d84315,stroke:#bf360c,color:#fff
    style HAND_METRIC fill:#d84315,stroke:#bf360c,color:#fff
    style HAND_REVIEW fill:#d84315,stroke:#bf360c,color:#fff
    style OTEL fill:#2e7d32,stroke:#1b5e20,color:#fff
    style CI_SYS fill:#2e7d32,stroke:#1b5e20,color:#fff
    style HARNESS_5 fill:#2e7d32,stroke:#1b5e20,color:#fff
    style JIRA fill:#2e7d32,stroke:#1b5e20,color:#fff
```

**Key OpenFang Features for rippled Harness Engineering**

| OpenFang Feature                                         | Harness Engineering Value                                                                                                    |
| -------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------- |
| **Hands** (autonomous capability packages)               | Package OTel verification, build validation, and metric regression detection as custom Hands that run on schedules           |
| **HAND.toml** (manifest with tools, metrics, guardrails) | Declaratively define what each harness agent can access, what metrics it reports, what actions need approval                 |
| **WASM Dual-Metered Sandbox**                            | Run agent-generated code in isolation — critical for a financial system like XRPL                                            |
| **Merkle Hash-Chain Audit Trail**                        | Cryptographic proof of every agent action — required for regulatory compliance in blockchain infrastructure                  |
| **16 Security Systems**                                  | Defense-in-depth (SSRF protection, secret zeroization, taint tracking) matches rippled's security requirements               |
| **MCP + A2A Protocol Support**                           | Native Model Context Protocol support means agents can use MCP servers to access rippled build tools, OTel queries, and Jira |
| **Capability Gates (RBAC)**                              | Map directly to Three-Tier Boundaries — declare what tools each agent role can use                                           |
| **Budget Tracking + Metering**                           | Track LLM API costs per agent per task — important for sustainable autonomous development                                    |
| **40 Channel Adapters**                                  | Connect to Jira, Slack, Discord for inbound work items and outbound status reports                                           |
| **Loop Guard**                                           | SHA256-based detection prevents agents from getting stuck in retry loops on failing builds                                   |

**Custom Hands for rippled**

The "Hands" concept (autonomous capability packages defined by `HAND.toml` + system prompt + `SKILL.md`) maps perfectly to harness engineering tasks:

```toml
# HAND.toml — OTel Verification Hand (conceptual)
[hand]
name = "otel-verifier"
description = "Validates OTel telemetry after code changes"
schedule = "on_pr"

[tools]
required = ["bash", "curl", "browser"]

[settings]
harness_nodes = 5
workload_duration_sec = 60
span_count_expected = 16
metric_regression_threshold_pct = 5.0

[guardrails]
require_approval = ["deploy_to_testnet", "modify_consensus_params"]

[dashboard]
metrics = ["spans_validated", "metrics_checked", "regressions_found"]
```

**Why OpenFang Over Symphony for rippled**

| Factor                 | Symphony (Elixir)                | OpenFang (Rust)                                                         |
| ---------------------- | -------------------------------- | ----------------------------------------------------------------------- |
| **Language alignment** | Elixir — foreign to rippled team | Rust — closer to C++ mindset, memory-safe systems language              |
| **Security model**     | Basic workspace isolation        | 16-layer defense-in-depth (WASM sandbox, taint tracking, audit trail)   |
| **Autonomous agents**  | Spawns external Codex            | Built-in autonomous Hands that run 24/7                                 |
| **Observability**      | Structured logs                  | Dashboard + 140+ API endpoints for monitoring agent runs                |
| **Audit trail**        | Git history                      | Merkle hash-chain — tamper-evident, suitable for regulated environments |
| **Install**            | Elixir runtime + Docker          | Single 32MB binary                                                      |
| **Agent memory**       | None built-in                    | SQLite + vector embeddings, session compaction                          |

### 6.4 Integration Strategy: OpenFang as the Primary Harness Runtime

**Recommended approach**: Use OpenFang as the agent operating system for rippled's harness engineering strategy, replacing Symphony in the Phase C orchestration layer.

```mermaid
flowchart TB
    subgraph phase_a_new["Phase A: Foundation (unchanged)"]
        direction TB
        A1_N["ARCHITECTURE.md"]
        A2_N["Three-Tier Boundaries"]
        A3_N["Pre-commit hooks"]
        A4_N["OTel baseline script"]
    end

    subgraph phase_b_new["Phase B: Feedback Loops (unchanged)"]
        direction TB
        B1_N["Semantic linting"]
        B2_N["Tiered CI pipeline"]
        B3_N["Metric regression check"]
        B4_N["Phase 10: Synthetic workload"]
    end

    subgraph phase_c_new["Phase C: Agent Runtime (updated)"]
        direction TB
        C1_N["Deploy OpenFang<br/>alongside rippled infra"]
        C2_N["Build custom Hands:<br/>otel-verifier, build-runner,<br/>metric-checker, code-reviewer"]
        C3_N["Connect Jira via<br/>channel adapter"]
        C4_N["Write HAND.toml manifests<br/>with RBAC matching<br/>Three-Tier Boundaries"]
        C5_N["First autonomous<br/>implementation runs"]
    end

    subgraph phase_d_new["Phase D: Advanced (new)"]
        direction TB
        D1_N["OpenClaw skills for<br/>developer-facing interface"]
        D2_N["Agent-to-agent coordination<br/>via A2A protocol"]
        D3_N["Merkle audit trail<br/>for compliance reporting"]
        D4_N["FangHub marketplace<br/>for rippled-specific Hands"]
    end

    phase_a_new --> phase_b_new --> phase_c_new --> phase_d_new

    style phase_a_new fill:#1b5e20,stroke:#0d3d14,color:#fff
    style phase_b_new fill:#bf360c,stroke:#8c2809,color:#fff
    style phase_c_new fill:#0d47a1,stroke:#082f6a,color:#fff
    style phase_d_new fill:#4a148c,stroke:#2e0d57,color:#fff
    style A1_N fill:#2e7d32,stroke:#1b5e20,color:#fff
    style A2_N fill:#2e7d32,stroke:#1b5e20,color:#fff
    style A3_N fill:#2e7d32,stroke:#1b5e20,color:#fff
    style A4_N fill:#2e7d32,stroke:#1b5e20,color:#fff
    style B1_N fill:#d84315,stroke:#bf360c,color:#fff
    style B2_N fill:#d84315,stroke:#bf360c,color:#fff
    style B3_N fill:#d84315,stroke:#bf360c,color:#fff
    style B4_N fill:#d84315,stroke:#bf360c,color:#fff
    style C1_N fill:#1565c0,stroke:#0d47a1,color:#fff
    style C2_N fill:#1565c0,stroke:#0d47a1,color:#fff
    style C3_N fill:#1565c0,stroke:#0d47a1,color:#fff
    style C4_N fill:#1565c0,stroke:#0d47a1,color:#fff
    style C5_N fill:#1565c0,stroke:#0d47a1,color:#fff
    style D1_N fill:#6a1b9a,stroke:#4a148c,color:#fff
    style D2_N fill:#6a1b9a,stroke:#4a148c,color:#fff
    style D3_N fill:#6a1b9a,stroke:#4a148c,color:#fff
    style D4_N fill:#6a1b9a,stroke:#4a148c,color:#fff
```

### 6.5 Comparative Summary

| Capability              | Symphony                 | OpenClaw                           | OpenFang                                  | Recommendation                                          |
| ----------------------- | ------------------------ | ---------------------------------- | ----------------------------------------- | ------------------------------------------------------- |
| **Agent Orchestration** | Polling + dispatch       | Gateway + routing                  | Kernel + scheduler                        | OpenFang for autonomous, OpenClaw for interactive       |
| **Security**            | Basic isolation          | DM pairing, allowlists             | 16-layer defense-in-depth                 | OpenFang — critical for financial infrastructure        |
| **Observability**       | Structured logs          | Logging                            | Dashboard + 140 API endpoints             | OpenFang — aligns with OTel investment                  |
| **Proof of Work**       | CI status, PR review     | N/A                                | Merkle hash-chain audit trail             | OpenFang — cryptographic proof                          |
| **Developer UX**        | Elixir CLI               | Multi-channel (Slack, Discord)     | CLI + Tauri desktop + 40 channels         | OpenClaw for notification layer, OpenFang for execution |
| **rippled Fit**         | Good (designed for this) | Medium (general-purpose assistant) | Strong (Rust, security-first, autonomous) | **OpenFang primary, OpenClaw supplementary**            |

### 6.6 Recommended Architecture: Dual-Layer Setup

The optimal configuration uses **both** projects in complementary roles:

- **OpenFang** = **execution layer** — runs autonomous Hands for build verification, OTel validation, metric regression detection, and code review. Handles the "harness" in harness engineering.
- **OpenClaw** = **communication layer** — developer-facing interface via Slack/Discord. Developers chat with OpenClaw to ask about agent status, trigger manual runs, review proof-of-work summaries. OpenClaw routes to OpenFang's API for execution.

This mirrors OpenAI's own separation: Symphony handles orchestration (like OpenFang), while the developer interacts through a surface (like OpenClaw) to review and approve work.

---

## Part 7: MCP Integrations — Connecting Bug Reporting, Bounty, and CI Platforms

### 7.1 The MCP Bridge

The Model Context Protocol (MCP) is the glue that connects autonomous agents to external platforms. For rippled's harness engineering strategy, three MCP servers form the critical integration layer between agents and the project's existing workflows:

| MCP Server                                                                    | Provider             | Stars | Capabilities                                                                                                                     |
| ----------------------------------------------------------------------------- | -------------------- | ----- | -------------------------------------------------------------------------------------------------------------------------------- |
| **[GitHub MCP Server](https://github.com/github/github-mcp-server)**          | GitHub (official)    | 27K   | 18 toolsets: repos, issues, PRs, actions, code_security, secret_protection, security_advisories, notifications, labels, and more |
| **[Atlassian MCP Server](https://github.com/atlassian/atlassian-mcp-server)** | Atlassian (official) | 400+  | Jira (search/create/update issues via JQL), Confluence (pages, search via CQL), Compass (components), OAuth 2.1 + API token auth |
| **[mcp-atlassian](https://github.com/sooperset/mcp-atlassian)**               | Community            | 4.5K  | Jira + Confluence with broader client compatibility (Claude Code, VS Code, Cursor)                                               |

### 7.2 The Existing Bug Reporting Pipeline

rippled already has a well-defined vulnerability reporting structure (from [SECURITY.md](https://github.com/XRPLF/rippled/blob/develop/SECURITY.md)):

```mermaid
flowchart TB
    subgraph inbound["Bug Discovery Channels"]
        direction TB
        BOUNTY["Bug Bounty Program<br/>(bugs@ripple.com, PGP encrypted)<br/>Scope: rippled, xrpl.js, xrpl-py, xrpl4j"]
        GH_ISSUES["GitHub Issues<br/>(XRPLF/rippled)<br/>Labels: Bug, Triaged"]
        INTERNAL["Internal Discovery<br/>(dev team, CI failures,<br/>OTel anomaly detection)"]
    end

    subgraph triage["Triage Process"]
        direction TB
        ASSESS["Two independent evaluators<br/>assess the report"]
        PLAN["Formulate fix plan"]
        FIX["Develop, test, release fix"]
        ANNOUNCE["Public announcement"]
    end

    subgraph jira_track["Jira Tracking"]
        direction TB
        EPIC["Epic: RIPD-5060<br/>(OTel initiative)"]
        STORIES["Stories per phase<br/>(e.g. RIPD-5187)"]
        BUGS["Bug tickets<br/>(RIPD project)"]
    end

    BOUNTY --> ASSESS
    GH_ISSUES --> ASSESS
    INTERNAL --> ASSESS
    ASSESS --> PLAN --> FIX --> ANNOUNCE

    GH_ISSUES -.->|"sync"| BUGS
    BUGS --> STORIES
    STORIES --> EPIC

    style inbound fill:#c62828,stroke:#b71c1c,color:#fff
    style triage fill:#bf360c,stroke:#8c2809,color:#fff
    style jira_track fill:#0d47a1,stroke:#082f6a,color:#fff
    style BOUNTY fill:#d32f2f,stroke:#c62828,color:#fff
    style GH_ISSUES fill:#d32f2f,stroke:#c62828,color:#fff
    style INTERNAL fill:#d32f2f,stroke:#c62828,color:#fff
    style ASSESS fill:#d84315,stroke:#bf360c,color:#fff
    style PLAN fill:#d84315,stroke:#bf360c,color:#fff
    style FIX fill:#d84315,stroke:#bf360c,color:#fff
    style ANNOUNCE fill:#d84315,stroke:#bf360c,color:#fff
    style EPIC fill:#1565c0,stroke:#0d47a1,color:#fff
    style STORIES fill:#1565c0,stroke:#0d47a1,color:#fff
    style BUGS fill:#1565c0,stroke:#0d47a1,color:#fff
```

**Key characteristics of the current pipeline:**

- **Bug bounty** is email-based (PGP-encrypted to `bugs@ripple.com`) — not on a platform like Immunefi or HackerOne
- **Scope** covers rippled, xrpl.js, xrpl-py, xrpl4j — security issues only (funds, privacy, network operation)
- **Triage** requires two independent evaluators, private reporter communication, and a coordinated disclosure timeline
- **GitHub Issues** track public bugs with `Bug` and `Triaged` labels
- **Jira (RIPD project)** tracks internal engineering work, with epics (RIPD-5060 for OTel) and stories per phase

### 7.3 GitHub MCP Server — CI, PRs, and Security Integration

The [GitHub MCP Server](https://github.com/github/github-mcp-server) (official, Go-based) gives agents direct access to the rippled repository through 18 toolsets. The harness-relevant toolsets:

| Toolset                   | Harness Engineering Use                                                                                             |
| ------------------------- | ------------------------------------------------------------------------------------------------------------------- |
| **`actions`**             | Monitor CI workflow runs, detect build failures, analyze flaky tests — feeds into Fail-Fast Feedback (Principle #4) |
| **`issues`**              | Read bug reports, extract reproduction steps, link to related PRs — automates triage intake                         |
| **`pull_requests`**       | Create PRs with proof-of-work evidence, read review comments, update PR descriptions — core autonomous workflow     |
| **`code_security`**       | Access Code Scanning alerts (CodeQL) — agents can detect and fix security findings autonomously                     |
| **`secret_protection`**   | Check for exposed secrets — critical for a financial system; agent can flag and remediate immediately               |
| **`security_advisories`** | Read/create security advisories — connects to bug bounty reporting flow                                             |
| **`repos`**               | Browse code, search files, analyze commits — supports codebase exploration during implementation runs               |
| **`labels`**              | Apply triage labels (`Bug`, `Triaged`, `Security`) — automates issue categorization                                 |

**Agent workflow with GitHub MCP:**

```mermaid
flowchart LR
    subgraph trigger["Trigger"]
        ISSUE["New GitHub Issue<br/>(labeled 'Bug')"]
        CI_FAIL["CI Failure<br/>(Actions workflow)"]
        SEC_ALERT["Code Scanning<br/>Alert"]
    end

    subgraph agent_work["Agent via GitHub MCP"]
        READ["Read issue/alert<br/>details"]
        ANALYZE["Analyze code +<br/>reproduction steps"]
        IMPLEMENT["Implement fix<br/>in worktree"]
        PR_CREATE["Create PR with<br/>proof-of-work"]
    end

    subgraph verify_gh["Verification"]
        CI_CHECK["Monitor Actions<br/>workflow run"]
        REVIEW["Request review +<br/>address comments"]
        LABEL["Apply resolution<br/>labels"]
    end

    ISSUE --> READ
    CI_FAIL --> READ
    SEC_ALERT --> READ
    READ --> ANALYZE --> IMPLEMENT --> PR_CREATE
    PR_CREATE --> CI_CHECK --> REVIEW --> LABEL

    style trigger fill:#c62828,stroke:#b71c1c,color:#fff
    style agent_work fill:#1b5e20,stroke:#0d3d14,color:#fff
    style verify_gh fill:#0d47a1,stroke:#082f6a,color:#fff
    style ISSUE fill:#d32f2f,stroke:#c62828,color:#fff
    style CI_FAIL fill:#d32f2f,stroke:#c62828,color:#fff
    style SEC_ALERT fill:#d32f2f,stroke:#c62828,color:#fff
    style READ fill:#2e7d32,stroke:#1b5e20,color:#fff
    style ANALYZE fill:#2e7d32,stroke:#1b5e20,color:#fff
    style IMPLEMENT fill:#2e7d32,stroke:#1b5e20,color:#fff
    style PR_CREATE fill:#2e7d32,stroke:#1b5e20,color:#fff
    style CI_CHECK fill:#1565c0,stroke:#0d47a1,color:#fff
    style REVIEW fill:#1565c0,stroke:#0d47a1,color:#fff
    style LABEL fill:#1565c0,stroke:#0d47a1,color:#fff
```

### 7.4 Jira MCP Server — Work Tracking and Sprint Integration

Two options for Jira integration:

| Server                                                                                   | Best For                                                                |
| ---------------------------------------------------------------------------------------- | ----------------------------------------------------------------------- |
| **[Atlassian MCP Server](https://github.com/atlassian/atlassian-mcp-server)** (official) | Production — OAuth 2.1, Atlassian-hosted, respects all Jira permissions |
| **[mcp-atlassian](https://github.com/sooperset/mcp-atlassian)** (community, 4.5K stars)  | Development — broader client support, self-hosted, faster iteration     |

**Jira MCP enables agents to:**

- **Read RIPD tickets** — pull acceptance criteria, linked PRs, priority, and sprint assignment via JQL queries
- **Create bug tickets** — when an agent discovers a regression during OTel validation, auto-create a RIPD bug ticket with telemetry evidence (span diff, metric deviation, dashboard screenshot URL)
- **Update ticket status** — transition tickets through the workflow (Open -> In Progress -> In Review -> Done) as the agent progresses
- **Link PRs to tickets** — automatically add PR links to Jira tickets (never the reverse, per project rules)
- **Search for context** — JQL queries like `project = RIPD AND labels = otel AND status != Done` to find related work

**Recommended AGENTS.md/CLAUDE.md configuration for Jira MCP:**

```markdown
## Atlassian Rovo MCP

When connected to atlassian-rovo-mcp:

- **MUST** use Jira project key = RIPD
- **MUST** use cloudId for the Ripple Atlassian instance
- **MUST** use `maxResults: 10` for all JQL search operations
- **MUST** link PRs TO Jira tickets, never Jira tickets INTO PRs
- **MUST NOT** create or modify Security-labeled tickets without human approval
```

### 7.5 Bug Bounty Pipeline — Agent-Assisted Triage

The bug bounty program's email-based intake is inherently manual, but agents can accelerate the downstream process once a report enters the system:

```mermaid
flowchart TB
    subgraph intake["Intake (Manual)"]
        EMAIL["Encrypted email to<br/>bugs@ripple.com"]
        HUMAN_TRIAGE["Two evaluators<br/>assess report"]
    end

    subgraph agent_assist["Agent-Assisted (via MCP)"]
        JIRA_CREATE["Create RIPD security ticket<br/>(Jira MCP)"]
        REPRO["Attempt reproduction<br/>on test harness"]
        OTEL_CHECK["Check OTel telemetry for<br/>anomalous patterns matching<br/>the reported vulnerability"]
        FIX_BRANCH["Create fix branch +<br/>implement patch"]
        VALIDATE["Run OTel verification:<br/>spans intact, no regression,<br/>vulnerability mitigated"]
        PR_SUBMIT["Create PR with<br/>proof-of-work<br/>(GitHub MCP)"]
    end

    subgraph release["Release (Manual)"]
        REVIEW_SEC["Security review of fix"]
        RELEASE["Coordinated release"]
        ADVISORY["Public advisory<br/>(GitHub MCP: security_advisories)"]
    end

    EMAIL --> HUMAN_TRIAGE
    HUMAN_TRIAGE -->|"approved"| JIRA_CREATE
    JIRA_CREATE --> REPRO --> OTEL_CHECK --> FIX_BRANCH --> VALIDATE --> PR_SUBMIT
    PR_SUBMIT --> REVIEW_SEC --> RELEASE --> ADVISORY

    style intake fill:#c62828,stroke:#b71c1c,color:#fff
    style agent_assist fill:#1b5e20,stroke:#0d3d14,color:#fff
    style release fill:#4a148c,stroke:#2e0d57,color:#fff
    style EMAIL fill:#d32f2f,stroke:#c62828,color:#fff
    style HUMAN_TRIAGE fill:#d32f2f,stroke:#c62828,color:#fff
    style JIRA_CREATE fill:#2e7d32,stroke:#1b5e20,color:#fff
    style REPRO fill:#2e7d32,stroke:#1b5e20,color:#fff
    style OTEL_CHECK fill:#2e7d32,stroke:#1b5e20,color:#fff
    style FIX_BRANCH fill:#2e7d32,stroke:#1b5e20,color:#fff
    style VALIDATE fill:#2e7d32,stroke:#1b5e20,color:#fff
    style PR_SUBMIT fill:#2e7d32,stroke:#1b5e20,color:#fff
    style REVIEW_SEC fill:#6a1b9a,stroke:#4a148c,color:#fff
    style RELEASE fill:#6a1b9a,stroke:#4a148c,color:#fff
    style ADVISORY fill:#6a1b9a,stroke:#4a148c,color:#fff
```

**Key boundaries for security work:**

| Action                                        | Tier       | Rationale                            |
| --------------------------------------------- | ---------- | ------------------------------------ |
| Read bounty ticket details from Jira          | **Ask**    | Security-sensitive content           |
| Attempt reproduction on isolated test harness | **Always** | Sandboxed, no production risk        |
| Check OTel telemetry for matching anomalies   | **Always** | Read-only query                      |
| Create fix branch and implement patch         | **Always** | Isolated worktree                    |
| Run OTel verification suite                   | **Always** | Deterministic, sandboxed             |
| Create PR for security fix                    | **Ask**    | Visibility to others                 |
| Publish security advisory                     | **Never**  | Must be human-coordinated disclosure |
| Modify cryptographic code                     | **Never**  | Per existing Three-Tier Boundaries   |

### 7.6 Proactive Bug Detection via OTel + MCP

The most powerful integration is agents that **find bugs before they're reported**, using OTel telemetry as a continuous health signal:

| Detection Pattern                | OTel Signal                                     | Agent Action (via MCP)                                                                                                              |
| -------------------------------- | ----------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------- |
| Latency spike on `rpc.submit`    | Span duration > p99 threshold                   | Create GitHub Issue (GitHub MCP), create RIPD ticket (Jira MCP), investigate root cause                                             |
| Consensus round timeout          | `consensus.round` span exceeds tolerance        | Alert via Slack (OpenClaw), create high-priority Jira ticket, run diagnostic on test harness                                        |
| Transaction relay failure        | `tx.relay` span missing child `peer.send` spans | Investigate overlay connectivity, create bug report with trace evidence                                                             |
| Metric regression after PR merge | Prometheus metric deviation > 5%                | Comment on merged PR (GitHub MCP), create regression ticket (Jira MCP), attempt automated revert                                    |
| Code scanning alert              | GitHub CodeQL finding                           | Read alert details (GitHub MCP), assess severity, create fix PR if low-risk, escalate if high-risk                                  |
| Secret exposure                  | GitHub secret scanning alert                    | Immediately flag (GitHub MCP), create urgent Jira ticket, agent must NOT attempt to remediate secrets autonomously (**Never** tier) |

### 7.7 MCP Configuration for the Harness

**Recommended MCP server configuration for Claude Code / agent runtimes:**

```json
{
  "mcpServers": {
    "github": {
      "type": "http",
      "url": "https://api.githubcopilot.com/mcp/",
      "toolsets": [
        "repos",
        "issues",
        "pull_requests",
        "actions",
        "code_security",
        "secret_protection",
        "security_advisories",
        "labels",
        "notifications"
      ]
    },
    "atlassian": {
      "type": "http",
      "url": "https://mcp.atlassian.com/v1/mcp",
      "note": "OAuth 2.1 — scoped to RIPD project"
    }
  }
}
```

**For OpenFang integration**, these MCP servers connect natively via OpenFang's MCP support, enabling Hands to:

- Poll Jira for new RIPD tickets matching `type = Bug AND status = Open`
- Read GitHub Issues labeled `Bug` for reproduction steps
- Create PRs with proof-of-work after verification passes
- Monitor GitHub Actions for CI status on agent-created PRs

### 7.8 End-to-End: From Bug Report to Verified Fix

Putting it all together — the complete harness-engineered bug lifecycle:

```mermaid
flowchart TB
    subgraph sources["Bug Sources"]
        direction LR
        S1["Bug Bounty<br/>(email)"]
        S2["GitHub Issue<br/>(public)"]
        S3["OTel Anomaly<br/>(automated)"]
        S4["CI Failure<br/>(GitHub Actions)"]
    end

    subgraph mcp_layer["MCP Integration Layer"]
        direction LR
        JIRA_MCP["Jira MCP<br/>(create/read tickets)"]
        GH_MCP["GitHub MCP<br/>(issues, PRs, CI, security)"]
    end

    subgraph agent_runtime["Agent Runtime (OpenFang)"]
        direction TB
        TRIAGE_HAND["Triage Hand<br/>(classify, prioritize)"]
        FIX_HAND["Fix Hand<br/>(implement, test)"]
        VERIFY_HAND["OTel Verifier Hand<br/>(validate telemetry)"]
    end

    subgraph proof["Proof of Work"]
        direction LR
        P1["CI passes"]
        P2["16 spans valid"]
        P3["Metrics stable"]
        P4["Audit trail<br/>(Merkle chain)"]
    end

    S1 --> JIRA_MCP
    S2 --> GH_MCP
    S3 --> GH_MCP
    S4 --> GH_MCP

    JIRA_MCP --> TRIAGE_HAND
    GH_MCP --> TRIAGE_HAND
    TRIAGE_HAND --> FIX_HAND --> VERIFY_HAND

    VERIFY_HAND --> P1
    VERIFY_HAND --> P2
    VERIFY_HAND --> P3
    VERIFY_HAND --> P4

    P1 -->|"PR created via<br/>GitHub MCP"| GH_MCP
    P4 -->|"ticket updated via<br/>Jira MCP"| JIRA_MCP

    style sources fill:#c62828,stroke:#b71c1c,color:#fff
    style mcp_layer fill:#e65100,stroke:#bf360c,color:#fff
    style agent_runtime fill:#1b5e20,stroke:#0d3d14,color:#fff
    style proof fill:#0d47a1,stroke:#082f6a,color:#fff
    style S1 fill:#d32f2f,stroke:#c62828,color:#fff
    style S2 fill:#d32f2f,stroke:#c62828,color:#fff
    style S3 fill:#d32f2f,stroke:#c62828,color:#fff
    style S4 fill:#d32f2f,stroke:#c62828,color:#fff
    style JIRA_MCP fill:#f57c00,stroke:#e65100,color:#fff
    style GH_MCP fill:#f57c00,stroke:#e65100,color:#fff
    style TRIAGE_HAND fill:#2e7d32,stroke:#1b5e20,color:#fff
    style FIX_HAND fill:#2e7d32,stroke:#1b5e20,color:#fff
    style VERIFY_HAND fill:#2e7d32,stroke:#1b5e20,color:#fff
    style P1 fill:#1565c0,stroke:#0d47a1,color:#fff
    style P2 fill:#1565c0,stroke:#0d47a1,color:#fff
    style P3 fill:#1565c0,stroke:#0d47a1,color:#fff
    style P4 fill:#1565c0,stroke:#0d47a1,color:#fff
```

---

## Appendix A: Key Resources

| Resource                        | URL                                                                                                      |
| ------------------------------- | -------------------------------------------------------------------------------------------------------- |
| OpenAI Harness Engineering Blog | https://openai.com/index/harness-engineering/                                                            |
| Symphony Framework              | https://github.com/openai/symphony                                                                       |
| Symphony Spec                   | https://github.com/openai/symphony/blob/main/SPEC.md                                                     |
| Agentic Harness Bootstrap       | https://github.com/synthnoosh/agentic-harness-bootstrap                                                  |
| Awesome Agent Harness           | https://github.com/AutoJunjie/awesome-agent-harness                                                      |
| OpenClaw (Agent Assistant)      | https://github.com/openclaw/openclaw                                                                     |
| OpenFang (Agent OS)             | https://github.com/RightNow-AI/openfang                                                                  |
| GitHub MCP Server (official)    | https://github.com/github/github-mcp-server                                                              |
| Atlassian MCP Server (official) | https://github.com/atlassian/atlassian-mcp-server                                                        |
| mcp-atlassian (community)       | https://github.com/sooperset/mcp-atlassian                                                               |
| rippled SECURITY.md             | https://github.com/XRPLF/rippled/blob/develop/SECURITY.md                                                |
| rippled OTel PR Chain           | #6436 -> #6437 -> #6438 -> #6424 -> #6425 -> #6426 -> #6427 -> #6433 -> #6439 -> #6493 -> #6494 -> #6513 |
| rippled OTel Plan Docs          | `OpenTelemetryPlan/` directory (10 documents)                                                            |

## Appendix B: Harness Engineering Ecosystem

The broader ecosystem emerging around harness engineering:

| Category               | Tools                                                                                  |
| ---------------------- | -------------------------------------------------------------------------------------- |
| **Agent OS/Runtime**   | **OpenFang** (Rust, autonomous Hands), **OpenClaw** (TS, skills + gateway)             |
| **Orchestrators**      | Symphony (OpenAI), Vibe Kanban, Emdash, Warp, VibeHQ                                   |
| **Task Runners**       | Symphony, Baton, Axon, GitHub Copilot Coding Agent                                     |
| **Coding Agents**      | Claude Code, Codex, OpenCode, Gemini CLI, Kiro, Amp, Cursor                            |
| **Spec/Requirements**  | Kiro IDE, OpenSpec, agents.md, AGENTS.md                                               |
| **Protocols**          | MCP (Model Context Protocol), ACP (Agent Communication Protocol), A2A (Agent-to-Agent) |
| **Harness Frameworks** | Deep Agents, Gambit, Harness Kit, Bridle                                               |

## Appendix C: Glossary

| Term                           | Definition                                                                                       |
| ------------------------------ | ------------------------------------------------------------------------------------------------ |
| **Harness Engineering**        | Building infrastructure around AI coding agents to enable reliable autonomous development        |
| **Proof of Work**              | Evidence that an agent's changes are correct: CI pass, metric stability, trace validity          |
| **Three-Tier Boundaries**      | Categorizing agent actions into Always (autonomous), Ask (confirm), Never (forbidden)            |
| **[WORKFLOW.md](WORKFLOW.md)** | Version-controlled file defining agent orchestration policy, prompt template, and runtime config |
| **Implementation Run**         | A complete autonomous cycle: read issue -> implement -> verify -> PR                             |
| **Semantic Linting**           | Linter errors that include enough context for an agent to fix the violation in one attempt       |
| **Architecture Map**           | A concise document (`ARCHITECTURE.md`) telling agents where things are in the codebase           |
| **MCP**                        | Model Context Protocol — standard for connecting AI agents to external tools and data sources    |
| **MCP Server**                 | A service exposing tools/resources via MCP that agents can invoke (e.g., GitHub MCP, Jira MCP)   |
| **Bug Bounty**                 | Program rewarding external researchers for responsibly disclosing security vulnerabilities       |
| **Hand**                       | OpenFang's autonomous capability package — bundles tools, prompts, guardrails, and schedules     |
