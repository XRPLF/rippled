# Phase-9 Alerting — Design

**Date:** 2026-07-06
**Branch:** `pratik/otel-phase9-metric-gap-fill` (PR #6513, Jira RIPD-5187)
**Status:** Approved

## Purpose

Phase 9 exports ~68 internal rippled metrics and ships Grafana dashboards for
them. This adds the missing operator-facing piece: **provisioned Grafana alert
rules** that fire on the health-critical metrics phase 9 introduces. The
phase-9 task list (line 311) and Jira story RIPD-5187 both already list
"alerting rules" as a phase-9 deliverable, so this closes that gap.

Scope is deliberately narrow — the three subsystems whose failure is
node-fatal: **consensus/ledger health, validator health, job queue**. RPC/API
health is explicitly out of scope.

## Why phase 9 (not phase 11)

Every metric these alerts fire on is _born_ in phase 9
(`xrpld_ledger_history_mismatch_total`, `xrpld_ledgers_closed_total`,
`xrpld_validation_missed_total`, `xrpld_validations_checked_total`,
`xrpld_jq_trans_overflow_total`, `xrpld_job_queued_duration_us_bucket`). Alerts
belong with the metrics they watch, and this is where the dependency lives.

## Delivery

Provisioned YAML, version-controlled — matching the existing datasource /
dashboard provisioning pattern. No docker-compose change: the Grafana service
already mounts `./grafana/provisioning:/etc/grafana/provisioning:ro`, and
Grafana auto-loads `provisioning/alerting/*.yaml`.

New files under `docker/telemetry/grafana/provisioning/alerting/`:

| File                 | Purpose                                                                                                             |
| -------------------- | ------------------------------------------------------------------------------------------------------------------- |
| `contactpoints.yaml` | One contact point `xrpld-default` (webhook to a documented placeholder; comments show how to swap for Slack/email). |
| `policies.yaml`      | Default notification policy: route all alerts → `xrpld-default`, grouped by `alertname` + `exported_instance`.      |
| `rules.yaml`         | 6 alert rules across 3 groups (below).                                                                              |

Plus the Alerting section of `docs/telemetry-runbook.md` — operator runbook:
what each alert means, likely causes, and how to point the contact point at a
real receiver.

## Alert rules

All rules target Prometheus datasource `uid: prometheus`. Each rule uses the
Grafana rule shape: query (A) → reduce (B, last value) → threshold (C). All
`rate()`/`histogram_quantile()` expressions aggregate with
`sum by (exported_instance)` (or `+ le`) so **each node alerts independently**.
Alert rules run headless, so they cannot use the dashboards' `$node` template
variables — they match all series and group by `exported_instance` instead.

| Group     | Alert                 | Expression (5m window)                                                                                    | Fires                 | `for` | severity |
| --------- | --------------------- | --------------------------------------------------------------------------------------------------------- | --------------------- | ----- | -------- |
| Consensus | LedgerHistoryMismatch | `sum by (exported_instance)(rate(xrpld_ledger_history_mismatch_total[5m]))`                               | `> 0`                 | 5m    | critical |
| Consensus | LedgerCloseStalled    | `sum by (exported_instance)(rate(xrpld_ledgers_closed_total[5m]))`                                        | `< 0.001` (≈0)        | 3m    | critical |
| Validator | ValidationsMissed     | `sum by (exported_instance)(rate(xrpld_validation_missed_total[5m]))`                                     | `> 0`                 | 5m    | warning  |
| Validator | ValidationsNotChecked | `sum by (exported_instance)(rate(xrpld_validations_checked_total[5m]))`                                   | `< 0.001` (≈0)        | 5m    | warning  |
| Job queue | JobQueueTxOverflow    | `sum by (exported_instance)(rate(xrpld_jq_trans_overflow_total[5m]))`                                     | `> 0`                 | 5m    | warning  |
| Job queue | JobQueueLatencyHigh   | `histogram_quantile(0.99, sum by (le, exported_instance)(rate(xrpld_job_queued_duration_us_bucket[5m])))` | `> 1000000` (µs = 1s) | 5m    | warning  |

Each rule carries labels `severity` and `category` (consensus/validator/jobqueue)
and annotations `summary` + `description` (with `{{ $labels.exported_instance }}`
and `{{ $values.B.Value }}` interpolation).

### Threshold rationale

- **LedgerCloseStalled `< 0.001` for 3m**: healthy nodes close a ledger every
  ~3-5s; a 5m rate decaying to ~0 means the node is stuck. The epsilon (not
  exact `0`) avoids float rate-noise suppressing the alert.
- **JobQueueLatencyHigh 1s p99**: a default starting point, easy to tune — jobs
  queued >1s at p99 indicate the node is saturated.
- Others are `> 0` on error/miss counters: any sustained nonzero rate is
  actionable.

## Non-goals / YAGNI

- No per-alert silencing schedules, no mute timings.
- No RPC/API, overlay, or fee-market alerts (dashboards cover those visually).
- Single contact point — multi-receiver routing is left to the operator.

## Verification

1. `yamllint` (or `python -c yaml.safe_load`) on all three YAML files.
2. `docker compose -f docker/telemetry/docker-compose.yml config -q` still parses.
3. Optional live check: start stack, `GET /api/v1/provisioning/alert-rules`
   returns the 6 rules; Grafana logs show no provisioning errors.
4. Code-review pass (subagent) against phase conventions before commit.

## Branch / PR restructure (independent, same session)

- Rename `pratik/otel-phase11-benchmark` → `pratik/perf-test-otel-on`
  (0 unique commits — equals phase-10 tip; the telemetry-IN baseline).
- Rename `pratik/otel-phase11-telemetry-off` → `pratik/perf-test-otel-off`
  (carries the "compile telemetry OUT" build change + benchmark runbook).
- Close PR #7433, delete stale `origin/pratik/otel-phase11-telemetry-off`.
- Push both; open one perf PR `perf-test-otel-off → perf-test-otel-on`
  (diff = the ON-vs-OFF delta; `-on` needs no PR of its own).
