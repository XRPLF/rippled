# rippled OpenTelemetry Alerting Runbook

rippled exports its internal metrics and provisions Grafana alert rules on the
health-critical ones. This document explains each alert, its likely cause, and
how to point alerts at a real receiver.

The rules are provisioned from
`grafana/provisioning/alerting/` and load automatically when the Grafana
container in [docker-compose.yml](docker-compose.yml) starts — no manual setup
in the Grafana UI. Rules appear in Grafana under **Alerting → Alert rules**,
folder **xrpld**.

---

## Alert catalogue

All rules evaluate every minute against the Prometheus datasource, over a
5-minute window, and group by `exported_instance` so each node alerts on its
own. Alerts fire only after the condition holds for the `for` dwell time.

| Alert                   | Severity | Fires when                                      | For |
| ----------------------- | -------- | ----------------------------------------------- | --- |
| `LedgerHistoryMismatch` | critical | `rate(xrpld_ledger_history_mismatch_total)` > 0 | 5m  |
| `LedgerCloseStalled`    | critical | `rate(xrpld_ledgers_closed_total)` ≈ 0          | 3m  |
| `ValidationsMissed`     | warning  | `rate(xrpld_validation_missed_total)` > 0       | 5m  |
| `ValidationsNotChecked` | warning  | `rate(xrpld_validations_checked_total)` ≈ 0     | 5m  |
| `JobQueueTxOverflow`    | warning  | `rate(xrpld_jq_trans_overflow_total)` > 0       | 5m  |
| `JobQueueLatencyHigh`   | warning  | p99 `xrpld_job_queued_duration_us` > 1s         | 5m  |

### Consensus / ledger health

**LedgerHistoryMismatch** — The node closed a ledger whose history diverges
from the validated network chain. Likely causes: corrupted local state, a bug,
or a node that fell out of sync and rebuilt incorrectly. Investigate the node's
ledger acquisition logs; a healthy node never mismatches.

**LedgerCloseStalled** — No ledgers closed for 3 minutes. A healthy node closes
one every ~3-5s. Likely causes: lost peer connectivity, consensus stall, or the
process is hung. This rule also fires on _NoData_ — if the series disappears the
node is likely down. Check peer count and process health first.

### Validator health

**ValidationsMissed** — This validator's validations are not agreeing with the
validated ledger. Sustained misses risk removal from UNLs. Check clock sync,
peer connectivity, and whether the node is keeping up with ledger close.

**ValidationsNotChecked** — The node has stopped checking incoming validations
from peers. Likely causes: overlay/peer disconnection or a stalled validation
pipeline. Fires on NoData as well.

### Job queue / resource health

**JobQueueTxOverflow** — The transaction job queue is full and transactions are
being dropped. The node is shedding load it cannot process. Check CPU, the
`JobQueueLatencyHigh` alert, and offered load.

**JobQueueLatencyHigh** — p99 queue wait exceeds 1 second, i.e. jobs back up
before running. The node is saturated. Correlate with CPU and the Job Queue
dashboard.

---

## Tuning thresholds

Thresholds live in
[grafana/provisioning/alerting/rules.yaml](grafana/provisioning/alerting/rules.yaml)
as the `params` array of each rule's `C` (threshold) node. Common tunables:

- **`JobQueueLatencyHigh`** — `params: [1000000]` is 1 000 000 µs (1s). Lower
  it for latency-sensitive deployments.
- **`LedgerCloseStalled` / `ValidationsNotChecked`** — use `lt` with a tiny
  epsilon (`0.001`) rather than `0`, so floating-point rate noise near zero
  does not suppress the alert.

Edit the file and restart the Grafana container to reload:

```bash
docker compose -f docker/telemetry/docker-compose.yml restart grafana
```

---

## Sending alerts somewhere real

The provisioned contact point `xrpld-default`
([contactpoints.yaml](grafana/provisioning/alerting/contactpoints.yaml)) ships
as a **local-dev webhook** to a placeholder URL — alerts fire but go nowhere
until you change it.

**Option A — repoint the webhook.** Replace the `url` under the `webhook`
receiver with a real endpoint (PagerDuty Events API, Opsgenie, a custom sink).

**Option B — add a Slack receiver.** Add a second receiver to the same contact
point:

```yaml
- uid: xrpld-slack
  type: slack
  settings:
    url: https://hooks.slack.com/services/XXX/YYY/ZZZ
    title: "{{ .CommonLabels.alertname }} on {{ .CommonLabels.exported_instance }}"
```

**Option C — email.** Requires Grafana SMTP configured via `GF_SMTP_*`
environment variables on the Grafana service in `docker-compose.yml`, then an
`email` receiver with `addresses`.

Routing is a single flat policy in
[policies.yaml](grafana/provisioning/alerting/policies.yaml): all alerts →
`xrpld-default`, grouped by `alertname` + `exported_instance`. To route
critical alerts to a different receiver, add child routes matching
`severity = critical`.

---

## Verifying provisioning loaded

After the stack is up:

```bash
# All six rules present?
curl -s http://localhost:3000/api/v1/provisioning/alert-rules | jq '.[].title'

# Contact point present?
curl -s http://localhost:3000/api/v1/provisioning/contact-points | jq '.[].name'
```

Grafana logs a provisioning error and skips the file if the YAML is malformed:

```bash
docker compose -f docker/telemetry/docker-compose.yml logs grafana | grep -i alerting
```
