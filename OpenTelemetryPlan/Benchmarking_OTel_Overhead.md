<!-- cspell:ignore ondemand otelcol -->

# Benchmarking OpenTelemetry Overhead

How to empirically measure the runtime cost of rippled's OpenTelemetry
instrumentation, using the `ripple/perf-iac` performance pipeline.

> **Tracking:** [RIPD-7155](https://ripplelabs.atlassian.net/browse/RIPD-7155)
> (under epic RIPD-5060).

---

## What is measured

A perf-iac **Performance Comparison** run builds and deploys two rippled
clusters on dedicated EC2, drives identical JMeter payment load at both, and
profiles both:

| Side          | rippled build                                             | runtime cfg                                      | collector                              | profiling |
| ------------- | --------------------------------------------------------- | ------------------------------------------------ | -------------------------------------- | --------- |
| **on-demand** | telemetry compiled in (phase-10 default `telemetry=True`) | `[telemetry] enabled=1`, OTLP → `127.0.0.1:4318` | node-local sidecar (receive + discard) | on        |
| **baseline**  | telemetry compiled out (`telemetry=False`)                | none                                             | none                                   | on        |

**Overhead = the delta between the two sides** — the rippled-process eBPF
profile difference (CPU spent in span creation / attribute extraction on the
hot path) plus the JMeter TPS / latency delta. The OTel trace data itself is
discarded; only the _cost_ of producing it is measured.

### Why a local discard-collector

rippled's OTLP exporter runs on a background thread. If the endpoint is dead,
that thread burns CPU on failed-export retries — and because the exporter is
_inside_ the rippled process, that retry CPU lands in the rippled profile and
inflates the apparent overhead. A node-local collector that accepts and
discards (nop exporter) lets the export succeed instantly, keeping the profile
clean. It is CPU-capped (50%) and, being a separate process, is excluded from
the rippled-process profile regardless.

---

## Prerequisites (one-time)

Two branches carry the benchmark setup:

| Branch                              | Repo              | Purpose                                                                  |
| ----------------------------------- | ----------------- | ------------------------------------------------------------------------ |
| `pratik/otel-phase11-telemetry-off` | `XRPLF/rippled`   | baseline binary — `conanfile.py` `default_options.telemetry = False`     |
| `pratik/otel-benchmarking-test`     | `ripple/perf-iac` | adds the per-side `telemetry` config key + `otel_collector` Ansible role |

Both must be pushed. The perf-iac branch is the one to **run the workflow
from** (see below) so the telemetry plumbing is present.

---

## Triggering a run (manual — recommended)

Run the comparison from the perf-iac branch via `gh`, or via the Actions UI
with **Use workflow from = `pratik/otel-benchmarking-test`**.

```bash
gh workflow run perf-internal.yml -R ripple/perf-iac \
    --ref pratik/otel-benchmarking-test \
    -f work-item=RIPD-7155 \
    -f testname_base=otel_overhead_phase10 \
    -f ondemand_performance_config='{"repo":"xrplf/rippled","ref":"pratik/otel-phase10-workload-validation","telemetry":"on","test_tpm":"60000","test_duration":"600","profiling":"true"}' \
    -f baseline_performance_config='{"repo":"xrplf/rippled","ref":"pratik/otel-phase11-telemetry-off","telemetry":"off","profiling":"true"}'
```

| Field           | Meaning                                                         |
| --------------- | --------------------------------------------------------------- |
| `--ref`         | **must** be the perf-iac branch with the telemetry changes      |
| `work-item`     | real Jira key, ≤32 chars (names the dynamic env)                |
| `telemetry`     | per-side: `on` for on-demand, `off` for baseline — never merges |
| `test_tpm`      | aggregate throughput per **minute** (`60000` ≈ 1000 TPS)        |
| `test_duration` | seconds (`600` = 10 min)                                        |
| `profiling`     | `true` on **both** sides — this is the measurement              |

Shared keys (`test_tpm`, `test_duration`) inherit baseline ← on-demand, so set
them once. Omitting `ssh-public-key` auto-destroys the env after the run.

### Reading results

- Report URL appears in the **Performance Testing** job log.
- Slack notice to `#ripplex-performance-rippled-ci`.
- Compare the two sides' rippled-process profiles + the TPS/latency table.

---

## Triggering from rippled CI (optional — needs a cross-org token)

It is possible to add a `workflow_dispatch` job in `XRPLF/rippled` that shells
out to dispatch the perf-iac run, so the benchmark can be kicked off from the
rippled repo. **This is not wired up yet** because of a cross-org auth
requirement, documented here so DevOps can decide.

### The blocker

- rippled lives in **`XRPLF`**; perf-iac lives in **`ripple`** (different orgs).
- A workflow's default `GITHUB_TOKEN` is scoped to its own repo and **cannot**
  dispatch a workflow in another org.
- A **PAT or GitHub App token** with `actions: write` on `ripple/perf-iac`
  must be stored as a secret (e.g. `PERF_IAC_DISPATCH_TOKEN`) in
  `XRPLF/rippled`. Provisioning that token is an org-admin decision.

### Sketch (once the token exists)

A `.github/workflows/otel-benchmark-trigger.yml` in rippled, `workflow_dispatch`
with inputs for the two refs / TPM / duration, whose single step dispatches
perf-iac:

```text
steps:
  - dispatch perf-internal.yml on ripple/perf-iac
    using gh CLI (or actions/github-script) authenticated with
    secrets.PERF_IAC_DISPATCH_TOKEN, passing the same -f inputs as the
    manual command above.
```

Notes / caveats:

- This only _kicks off_ the perf-iac run; the actual provisioning, build,
  load, and profiling still execute **in** perf-iac under its own OIDC role
  and repo `vars` — so no rippled-side AWS access is needed, only the
  dispatch token.
- Results still surface in the perf-iac Actions run + Slack, not in rippled CI.
  If rippled-side visibility is wanted, the trigger job can poll the dispatched
  run and echo its conclusion/report URL into the rippled job summary.

---

## Lessons learned

- **A parent-directory `.gitignore` (`tasks/`) silently excluded the Ansible
  role's `tasks/main.yml`.** The role committed without its tasks file and ran
  as a no-op — the collector never installed, leaving the OTLP endpoint dead.
  Always verify what is _tracked_ (`git ls-files <role>/`) after committing a
  new role, not just what exists locally; run `ansible-playbook --syntax-check`
  on the pushed tree.
- **Matrix legs run `max-parallel: 1`** — on-demand and baseline run
  sequentially on one dynamic env (good for comparability; doubles wall-clock).
- Validate the role mechanics locally (syntax-check, render templates); the
  full integration (real AMI apt install, 5-node provisioning, load) only
  exercises in the pipeline — so a short `test_duration` smoke run is the
  cheapest way to shake out integration bugs before a long measurement run.
