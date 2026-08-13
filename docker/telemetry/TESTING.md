# OpenTelemetry Integration Testing Guide

This document describes how to verify the xrpld OpenTelemetry telemetry
pipeline end-to-end, from span generation through the observability stack
(otel-collector, Tempo, Prometheus, Grafana).

---

## Prerequisites

### Build xrpld with telemetry

```bash
conan install . --build=missing -o telemetry=True
cmake --preset default -Dtelemetry=ON
cmake --build --preset default --target xrpld
```

The binary is at `.build/xrpld`.

### Required tools

- **Docker** with `docker compose` (v2)
- **curl**
- **jq** (JSON processor)

### Verify binary

```bash
.build/xrpld --version
```

---

## Test 1: Single-Node Standalone (Quick Verification)

This test verifies RPC and transaction spans in standalone mode. Consensus
spans will not fire because standalone mode does not run consensus.

### Step 1: Start the observability stack

```bash
docker compose -f docker/telemetry/docker-compose.yml up -d
```

Wait for services to be ready:

```bash
# otel-collector readiness: any HTTP response on the OTLP/HTTP port means the
# receiver is listening. Do NOT use `curl -sf` here — a GET of / returns 404,
# which -f treats as failure even when the collector is healthy.
[ "$(curl -so /dev/null -w '%{http_code}' http://localhost:4318/)" != "000" ] &&
    echo "collector ready"

# Tempo readiness
curl -sf http://localhost:3200/ready >/dev/null && echo "tempo ready"
```

> The collector's `health_check` extension listens on **13133**, but
> `docker-compose.yml` publishes only 4317, 4318 and 8889 — so 13133 is not
> reachable from the host with the base stack. It is published only by the
> Phase-10 workload stack (`docker-compose.workload.yaml`).

### Step 2: Start xrpld in standalone mode

```bash
.build/xrpld --conf docker/telemetry/xrpld-telemetry.cfg -a --start
```

Wait a few seconds for the node to initialize.

### Step 3: Exercise RPC spans

```bash
# server_info
curl -s http://localhost:5005 \
    -d '{"method":"server_info"}' | jq .result.info.server_state

# server_state
curl -s http://localhost:5005 \
    -d '{"method":"server_state"}' | jq .result.state.server_state

# ledger
curl -s http://localhost:5005 \
    -d '{"method":"ledger","params":[{"ledger_index":"current"}]}' |
    jq .result.ledger_current_index
```

### Step 4: Submit a transaction

Close the ledger first (required in standalone mode):

```bash
curl -s http://localhost:5005 -d '{"method":"ledger_accept"}'
```

Submit a Payment from the genesis account:

```bash
curl -s http://localhost:5005 -d '{
  "method": "submit",
  "params": [{
    "secret": "snoPBrXtMeMyMHUVTgbuqAfg1SUTb",
    "tx_json": {
      "TransactionType": "Payment",
      "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
      "Destination": "rPMh7Pi9ct699iZUTWzJaUMR1o42VEfGqF",
      "Amount": "10000000"
    }
  }]
}' | jq .result.engine_result
```

Expected result: `"tesSUCCESS"`.

Close the ledger again to finalize:

```bash
curl -s http://localhost:5005 -d '{"method":"ledger_accept"}'
```

### Step 5: Verify traces in Tempo

Wait 5 seconds for the batch export, then:

```bash
TEMPO="http://localhost:3200"

# Check xrpld service is registered
curl -s "$TEMPO/api/v2/search/tag/resource.service.name/values" | jq '.tagValues[].value'

# Check RPC spans
curl -s "$TEMPO/api/search" \
    --data-urlencode 'q={resource.service.name="xrpld" && name="rpc.http_request"}' \
    --data-urlencode 'limit=5' | jq '.traces | length'

curl -s "$TEMPO/api/search" \
    --data-urlencode 'q={resource.service.name="xrpld" && name="rpc.process"}' \
    --data-urlencode 'limit=5' | jq '.traces | length'

curl -s "$TEMPO/api/search" \
    --data-urlencode 'q={resource.service.name="xrpld" && name="rpc.command.server_info"}' \
    --data-urlencode 'limit=5' | jq '.traces | length'

# Check transaction spans
curl -s "$TEMPO/api/search" \
    --data-urlencode 'q={resource.service.name="xrpld" && name="tx.process"}' \
    --data-urlencode 'limit=5' | jq '.traces | length'
```

Or open Grafana Explore with Tempo datasource: http://localhost:3000

### Step 6: Teardown

```bash
# Kill xrpld (Ctrl+C or)
kill $(pgrep -f 'xrpld.*xrpld-telemetry')

# Stop observability stack
docker compose -f docker/telemetry/docker-compose.yml down

# Clean xrpld data
rm -rf data/
```

### Expected spans (standalone mode)

| Span Name                   | Expected | Notes                         |
| --------------------------- | -------- | ----------------------------- |
| `rpc.http_request`          | Yes      | Every HTTP RPC call           |
| `rpc.process`               | Yes      | Every RPC processing          |
| `rpc.command.server_info`   | Yes      | server_info RPC               |
| `rpc.command.server_state`  | Yes      | server_state RPC              |
| `rpc.command.ledger`        | Yes      | ledger RPC                    |
| `rpc.command.submit`        | Yes      | submit RPC                    |
| `rpc.command.ledger_accept` | Yes      | ledger_accept RPC             |
| `tx.process`                | Yes      | Transaction submission        |
| `tx.receive`                | No       | No peers in standalone        |
| `consensus.*`               | No       | Consensus disabled standalone |

---

## Test 2: 6-Node Consensus Network (Full Verification)

This test verifies ALL span categories including consensus and peer
transaction relay, using a 6-node validator network.

### Automated

Run the integration test script:

```bash
bash docker/telemetry/integration-test.sh
```

The script will:

1. Start the observability stack
2. Generate 6 validator key pairs
3. Create config files for each node
4. Start all 6 nodes
5. Wait for consensus ("proposing" state)
6. Exercise RPC, submit transactions
7. Verify all span categories in Tempo
8. Verify spanmetrics in Prometheus
9. Print results and leave the stack running

### Manual

If you prefer to run the steps manually:

#### Step 1: Start observability stack

```bash
docker compose -f docker/telemetry/docker-compose.yml up -d
```

#### Step 2: Generate validator keys

Start a temporary standalone xrpld:

```bash
.build/xrpld --conf docker/telemetry/xrpld-telemetry.cfg -a --start &
TEMP_PID=$!
sleep 5
```

Generate 6 key pairs:

```bash
for i in $(seq 1 6); do
    curl -s http://localhost:5005 \
        -d '{"method":"validation_create"}' | jq '.result'
done
```

Record the `validation_seed` and `validation_public_key` for each.
Kill the temporary node:

```bash
kill $TEMP_PID
rm -rf data/
```

#### Step 3: Create node configs

For each node (1-6), create a config file. Template:

```ini
[server]
port_rpc
port_peer

[port_rpc]
port = {5004 + node_number}
ip = 127.0.0.1
admin = 127.0.0.1
protocol = http

[port_peer]
port = {51234 + node_number}
ip = 0.0.0.0
protocol = peer

[node_db]
type=NuDB
path=/tmp/xrpld-integration/node{N}/nudb
online_delete=256

[database_path]
/tmp/xrpld-integration/node{N}/db

[debug_logfile]
/tmp/xrpld-integration/node{N}/debug.log

[validation_seed]
{seed from step 2}

[validators_file]
/tmp/xrpld-integration/validators.txt

[ips_fixed]
127.0.0.1 51235
127.0.0.1 51236
127.0.0.1 51237
127.0.0.1 51238
127.0.0.1 51239
127.0.0.1 51240

[peer_private]
1

[telemetry]
enabled=1
endpoint=http://localhost:4318/v1/traces
batch_size=512
batch_delay_ms=2000
max_queue_size=2048
trace_rpc=1
trace_transactions=1
trace_consensus=1
trace_peer=1
trace_ledger=1

[rpc_startup]
{ "command": "log_level", "severity": "warning" }

[ssl_verify]
0
```

#### Step 4: Create validators.txt

```ini
[validators]
{public_key_1}
{public_key_2}
{public_key_3}
{public_key_4}
{public_key_5}
{public_key_6}
```

#### Step 5: Start all 6 nodes

```bash
for i in $(seq 1 6); do
    .build/xrpld --conf /tmp/xrpld-integration/node$i/xrpld.cfg --start &
    echo $! >/tmp/xrpld-integration/node$i/xrpld.pid
done
```

#### Step 6: Wait for consensus

Poll each node until `server_state` = `"proposing"`:

```bash
for port in 5005 5006 5007 5008 5009 5010; do
    while true; do
        state=$(curl -s http://localhost:$port \
            -d '{"method":"server_info"}' |
            jq -r '.result.info.server_state')
        echo "Port $port: $state"
        [ "$state" = "proposing" ] && break
        sleep 5
    done
done
```

#### Step 7: Exercise RPC and submit transaction

```bash
# RPC calls
curl -s http://localhost:5005 -d '{"method":"server_info"}'
curl -s http://localhost:5005 -d '{"method":"server_state"}'
curl -s http://localhost:5005 -d '{"method":"ledger","params":[{"ledger_index":"current"}]}'

# Submit transaction
curl -s http://localhost:5005 -d '{
  "method": "submit",
  "params": [{
    "secret": "snoPBrXtMeMyMHUVTgbuqAfg1SUTb",
    "tx_json": {
      "TransactionType": "Payment",
      "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
      "Destination": "rPMh7Pi9ct699iZUTWzJaUMR1o42VEfGqF",
      "Amount": "10000000"
    }
  }]
}'
```

Wait 15 seconds for consensus and batch export.

#### Step 8: Verify in Tempo

See the "Verification Queries" section below.

---

## Expected Span Catalog

What follows is a **trigger** catalogue, not an attribute reference: one row per
span-name family, saying which config toggle gates it and what you have to do to
make it appear. It covers all 41 span-name families the code emits, in eight
subsystem groups — RPC (5), gRPC (1), Transaction (6), TxQ (6), Consensus (13),
Ledger (4), Peer (2), PathFind (4).

For each span's **attributes** — span name, source file, full attribute set and
description, per subsystem — see
[`docs/telemetry-runbook.md`](../../docs/telemetry-runbook.md) **§ Span
Reference**; its **§ Protocol Span Flow** gives the parent/child shape of a trace
and calls out where telemetry parenting deliberately differs from the protocol
flow. Both are kept in step with the code, so they are the reference to trust.
One hole worth knowing: the runbook's Span Reference tables have no row for
`grpc.<MethodName>` (it appears only in Protocol Span Flow). Its attributes are
`method`, `grpc_role` and `grpc_status`, emitted from `GRPCServer.cpp` with the
key constants in `src/xrpld/app/main/GrpcSpanNames.h`.

If you find an older inline span inventory in this file or elsewhere, do not
trust it — the copy that used to live here had drifted badly (18 rows under a
"16 spans" heading, whole families missing, and pre-rename dotted `xrpl.*`
attribute keys the code no longer emits). The code and the runbook are the source
of truth.

### Span → How to Trigger

"Test" is the section of this file that exercises the family. `T1` = Test 1
(standalone), `T2` = Test 2 (6-node network).

| Span family (count)                                                                                                                                                                                                                              | Config toggle        | How to trigger                                                                                                                                                                                                                    | Test    |
| ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------- |
| **RPC** (5 total, 3 here): `rpc.http_request`, `rpc.process`, `rpc.command.<name>`                                                                                                                                                               | `trace_rpc=1`        | Any HTTP JSON-RPC call: `curl -s http://localhost:5005 -d '{"method":"server_info"}'`. `rpc.command.<name>` is one family — the command name is part of the span name.                                                            | T1      |
| **RPC** (cont.): `rpc.ws_message`, `rpc.ws_upgrade`                                                                                                                                                                                              | `trace_rpc=1`        | Needs a WebSocket client against `[port_ws_public]` (**6005**) or `[port_ws_admin_local]` (6006). `rpc.ws_upgrade` covers the handshake — force a failure to see its error path. `curl` alone will not do it.                     | —       |
| **gRPC** (1): `grpc.<MethodName>`                                                                                                                                                                                                                | `trace_rpc=1`        | Call a gRPC method (`GetLedger`, `GetLedgerData`, …). **Requires a `[port_grpc]` stanza — the shipped `xrpld-telemetry*.cfg` files define none**, so add one first.                                                               | —       |
| **Transaction** (6 total, 4 here): `tx.process`, `tx.preflight`, `tx.preclaim`, `tx.transactor`                                                                                                                                                  | `trace_transactions` | Submit any transaction (T1 Step 4). The three apply-stage spans share the tx's deterministic trace id; the `stage` attribute says where a failing tx stopped.                                                                     | T1      |
| **Transaction** (cont.): `tx.receive`                                                                                                                                                                                                            | `trace_transactions` | A **peer** relays a transaction. Never appears in standalone — submit on one node of the cluster and look on another.                                                                                                             | T2      |
| **Transaction** (cont.): `tx.apply`                                                                                                                                                                                                              | `trace_transactions` | Ledger close with a non-empty transaction set: submit, then `ledger_accept` (T1) or wait for consensus (T2).                                                                                                                      | T1 / T2 |
| **TxQ** (6): `txq.enqueue`, `txq.apply_direct`, `txq.batch_clear`, `txq.accept`, `txq.accept_tx`, `txq.cleanup`                                                                                                                                  | `trace_transactions` | `txq.enqueue`/`apply_direct` on every submission; `txq.accept`/`accept_tx`/`cleanup` on every ledger close. To force real queueing, submit faster than ledgers close or with a fee below the required fee level.                  | T1      |
| **Consensus** (13): `consensus.round`, `.phase.open`, `.establish`, `.update_positions`, `.check`, `.proposal.send`, `.ledger_close`, `.accept`, `.accept.apply`, `.validation.send`, `.mode_change`, `.proposal.receive`, `.validation.receive` | `trace_consensus=1`  | Requires real consensus — **standalone emits none of these**. Bring up T2 and wait for nodes to reach `proposing`; one `consensus.round` per close. `.mode_change` needs an actual mode transition (stop/start a node).           | T2      |
| **Ledger** (4 total, 3 here): `ledger.build`, `ledger.validate`, `ledger.store`                                                                                                                                                                  | `trace_ledger=1`     | Any ledger close: `ledger_accept` in standalone, or consensus in T2.                                                                                                                                                              | T1 / T2 |
| **Ledger** (cont.): `ledger.acquire`                                                                                                                                                                                                             | `trace_ledger=1`     | Node fetches a **missing** ledger from peers. Start a node with no history against a running cluster, or restart one node after the others have advanced.                                                                         | T2      |
| **Peer** (2): `peer.proposal.receive`, `peer.validation.receive`                                                                                                                                                                                 | `trace_peer=1`       | Inbound consensus messages from peers; fresh trace roots. T2 only, and high volume.                                                                                                                                               | T2      |
| **PathFind** (4): `pathfind.request`, `pathfind.compute`, `pathfind.discover`, `pathfind.update_all`                                                                                                                                             | `trace_rpc=1`        | `curl -s http://localhost:5005 -d '{"method":"ripple_path_find","params":[{"source_account":"…","destination_account":"…","destination_amount":"100"}]}'`. `pathfind.update_all` fires on ledger close while a request is active. | T1      |

Notes that matter when a span you expect is missing:

- **Toggles are per-subsystem and all default to on** (`trace_rpc`,
  `trace_transactions`, `trace_consensus`, `trace_peer`, `trace_ledger`), but
  `[telemetry] enabled` defaults to **0** — nothing is emitted until it is `1`.
- **`consensus.*` and `peer.*` cannot be produced in standalone mode.** If Test 1
  shows none, that is correct behaviour, not a regression — see "Expected spans
  (standalone mode)" above.
- **`rpc.ws_*` and `grpc.*` need a client and a port the quick tests do not
  use.** Absence in T1/T2 is expected.
- Trace ids are deterministic for transactions (`txID[0:16]`) and consensus
  rounds (`prevLedgerHash[0:16]`), so you can compute the id you expect rather
  than searching for it.

---

## Verification Queries

### Tempo API

Base URL: `http://localhost:3200`

```bash
TEMPO="http://localhost:3200"

# List all services
curl -s "$TEMPO/api/v2/search/tag/resource.service.name/values" | jq '.tagValues[].value'

# Query traces by operation
for op in "rpc.http_request" "rpc.ws_upgrade" "rpc.ws_message" "rpc.process" \
    "rpc.command.server_info" "rpc.command.server_state" "rpc.command.ledger" \
    "tx.process" "tx.receive" "tx.apply" \
    "consensus.proposal.send" "consensus.ledger_close" \
    "consensus.accept" "consensus.accept.apply" \
    "consensus.validation.send" \
    "ledger.build" "ledger.validate" "ledger.store" \
    "peer.proposal.receive" "peer.validation.receive"; do
    count=$(curl -s "$TEMPO/api/search" \
        --data-urlencode "q={resource.service.name=\"xrpld\" && name=\"$op\"}" \
        --data-urlencode "limit=5" |
        jq '.traces | length')
    printf "%-35s %s traces\n" "$op" "$count"
done
```

### Prometheus API

Base URL: `http://localhost:9090`

```bash
PROM="http://localhost:9090"

# Span call counts (from spanmetrics connector)
curl -s "$PROM/api/v1/query?query=span_calls_total" |
    jq '.data.result[] | {span: .metric.span_name, count: .value[1]}'

# Latency histogram
curl -s "$PROM/api/v1/query?query=span_duration_milliseconds_count" |
    jq '.data.result[] | {span: .metric.span_name, count: .value[1]}'

# RPC calls by command
curl -s "$PROM/api/v1/query?query=span_calls_total{span_name=~\"rpc.command.*\"}" |
    jq '.data.result[] | {command: .metric["command"], count: .value[1]}'

# Deployment-tier labels present on metrics (set by the collector's
# resource/tier processor and promoted via resource_to_telemetry_conversion).
# Expect deployment_environment and xrpl_network_type on each series.
curl -s "$PROM/api/v1/query?query=span_calls_total" |
    jq '.data.result[0].metric | {deployment_environment, xrpl_network_type, service_name}'
```

### Grafana

Open http://localhost:3000 (anonymous admin access enabled).

Pre-configured dashboards: every `.json` under
`docker/telemetry/grafana/dashboards/` is provisioned into the `xrpld` folder —
`provisioning/dashboards/dashboards.yaml` points the file provider at
`/var/lib/grafana/dashboards`, which `docker-compose.yml` bind-mounts from that
directory. Adding a file there is all that is needed; there is no per-dashboard
registration.

For what each dashboard covers, see
[`docs/telemetry-runbook.md`](../../docs/telemetry-runbook.md) **§ Grafana
Dashboards** — the per-dashboard reference. Listing them here would be a second
copy that rots (this section previously named 5 of the 15 provisioned).

Pre-configured datasources:

- **Tempo**: Trace data at `http://tempo:3200`
- **Prometheus**: Metrics at `http://prometheus:9090`
- **Loki**: Log data at `http://loki:3100` (via Grafana Explore)

---

## Exporting to Grafana Cloud

Instead of (or alongside) the local backends, the collector can forward
traces, metrics, and logs to a hosted **Grafana Cloud** stack. This is a
runtime choice layered on top of the base stack — xrpld and the base
`docker-compose.yml` are unchanged.

### Step 1: Get Grafana Cloud OTLP credentials

From **Grafana Cloud → Connections → OpenTelemetry (OTLP)**, note the OTLP
gateway endpoint (ends in `/otlp`), the numeric instance id, and an
access-policy token with `metrics:write`, `traces:write`, and `logs:write`.

### Step 2: Fill in the env file

```bash
cp docker/telemetry/.env.grafanacloud.example docker/telemetry/.env.grafanacloud
# edit .env.grafanacloud:
#   GRAFANA_CLOUD_OTLP_ENDPOINT=https://otlp-gateway-<zone>.grafana.net/otlp
#   GRAFANA_CLOUD_INSTANCE_ID=<instance id>
#   GRAFANA_CLOUD_API_TOKEN=<token>
```

`.env.grafanacloud` is gitignored — never commit real tokens.

### Step 3: Start the stack with cloud export enabled

```bash
docker compose -f docker/telemetry/docker-compose.yml \
    -f docker/telemetry/docker-compose.grafanacloud.yaml up -d
```

The override swaps the collector onto `otel-collector-config.grafanacloud.yaml`.
It keeps the local Tempo/Prometheus/Loki exporters and adds an
`otlphttp/grafanacloud` exporter, but it is **not** the base config plus one
exporter — it restructures the pipelines. Bring the stack up with just the base
file to return to local-only.

Differences that change what you will see:

|                     | Base (`otel-collector-config.yaml`) | Cloud override                                                                                      |
| ------------------- | ----------------------------------- | --------------------------------------------------------------------------------------------------- |
| Pipelines           | 3: `traces`, `metrics`, `logs`      | 5: `traces/metrics`, `traces/store`, `metrics/local`, `metrics/cloud`, `logs`                       |
| Trace sampling      | none — 100% of spans reach Tempo    | `tail_sampling` keeps **0.5%** (one `probabilistic` policy, `decision_wait: 10s`) on `traces/store` |
| `debug` exporter    | present on `traces`                 | dropped                                                                                             |
| `attributes/hash`   | present on `traces`                 | **omitted**                                                                                         |
| Cloud metric labels | n/a                                 | `transform/cloudlabels` on `metrics/cloud` only                                                     |

Consequences worth knowing before you debug against the cloud stack:

- **Traces are sampled, span metrics are not.** Sampling sits only on
  `traces/store` (the pipeline feeding Tempo _and_ Grafana Cloud). The
  `spanmetrics` connector is fed by the separate, unsampled `traces/metrics`
  pipeline, so `span_*` rates stay exact while only ~1 trace in 200 is
  retrievable by trace ID. A trace you can see in a metric may not exist in
  Tempo.
- **Pathfinding account hashing does not happen on the cloud export.** The base
  config's `attributes/hash` processor hashes `pathfind_source_account` and
  `pathfind_dest_account`. It is absent from every cloud pipeline, so those two
  attributes leave for Grafana Cloud (and, on that config, for Tempo) with their
  raw account values.

### Step 4: Verify data reaches Grafana Cloud

After exercising RPC/transaction workflows (Tests 1 or 2), open your Grafana
Cloud instance and confirm:

- **Traces**: Explore → hosted Tempo datasource → search `{resource.service.name="xrpld"}`
- **Metrics**: Explore → hosted Prometheus/Mimir → query `span_calls_total`
- **Logs**: Explore → hosted Loki → query `{service_name="xrpld"}` (requires `warning`+ file logging). **Not `{job="xrpld"}`** — see the note under Test 3 Step 3.

If nothing appears, check the collector logs for auth/export errors:

```bash
docker compose -f docker/telemetry/docker-compose.yml \
    -f docker/telemetry/docker-compose.grafanacloud.yaml \
    logs otel-collector | grep -iE 'grafanacloud|401|403|export'
```

A `401`/`403` means the instance id or token is wrong; a connection error
means the endpoint URL is wrong or missing the `/otlp` path.

---

## Test 3: Log-Trace Correlation (Phase 8)

Phase 8 injects `trace_id` and `span_id` into xrpld's log output when
a log line is emitted within an active OTel span. This test verifies the
end-to-end log-trace correlation pipeline.

### Step 1: Verify trace_id in log output

After running Test 1 or Test 2 (which generate RPC spans), check the
xrpld debug.log for trace context:

```bash
grep 'trace_id=[a-f0-9]\{32\} span_id=[a-f0-9]\{16\}' /path/to/debug.log
```

Expected: log lines with `trace_id=<32hex> span_id=<16hex>` between the
severity code and the message. Example:

```
2024-Jan-15 10:30:45.123456 UTC RPCHandler:NFO trace_id=abc123def456789012345678abcdef01 span_id=0123456789abcdef Calling server_info
```

Lines emitted outside of an active span (background tasks, startup) will
NOT have trace context — this is expected.

### Step 2: Cross-check trace_id in Tempo

Extract a `trace_id` from the log and verify it exists in Tempo:

```bash
TRACE_ID=$(grep -o 'trace_id=[a-f0-9]\{32\}' /path/to/debug.log | head -1 | cut -d= -f2)
echo "Checking trace: $TRACE_ID"
curl -s "http://localhost:3200/api/traces/$TRACE_ID" | jq '.batches | length'
```

Expected result: `> 0` (the trace exists in Tempo).

### Step 3: Verify Loki log ingestion

The OTel Collector's filelog receiver tails xrpld's debug.log and
exports parsed entries to Loki. Verify Loki has received entries:

```bash
# Query Loki for any xrpld logs
curl -sG "http://localhost:3100/loki/api/v1/query" \
    --data-urlencode 'query={service_name="xrpld"}' \
    --data-urlencode 'limit=5' | jq '.data.result | length'
```

Expected: > 0 results.

> **Use `service_name`, not `job`.** The collector's `resource/logs` processor
> applies an `upsert` to **both** `service.name=xrpld` and `job=xrpld`
> (`otel-collector-config.yaml:57-70`), and its comment says the `job` attribute
> is there so operators can paste `{job="xrpld"}`. That does not work: on OTLP
> ingest Loki promotes only an allow-listed set of resource attributes to indexed
> stream labels (`service.name` → `service_name`, plus `service.namespace`,
> `service.instance.id`, `deployment.environment`, `k8s.*`, `cloud.*`), and `job`
> is not on the list. This repo mounts no Loki config override — the `loki`
> service runs the image's built-in `/etc/loki/local-config.yaml`
> (`docker-compose.yml:75`) — so `job` lands in **structured metadata**, which
> cannot be a stream selector. `{job="xrpld"}` therefore returns **zero results
> with no error**, which reads exactly like "logs are not being ingested". If
> this query is empty, check `{service_name="xrpld"}` before debugging the
> pipeline. All 38 Loki queries in the shipped dashboards select on
> `service_name`; none uses `job`.

### Step 4: Verify Grafana Tempo-to-Loki correlation

1. Open Grafana at http://localhost:3000
2. Navigate to **Explore** -> select **Tempo** datasource
3. Search for a trace (e.g., operation `rpc.command.server_info`)
4. Click **"Logs for this trace"** in the trace detail view
5. Verify that Loki log lines appear, filtered by the trace's `trace_id`

### Step 5: Verify Grafana Loki-to-Tempo correlation

1. In Grafana **Explore**, select **Loki** datasource
2. Query: `{service_name="xrpld"} |= "trace_id="`
3. In the log results, click the **TraceID** derived field link
4. Verify it navigates to the full trace in Tempo

### Expected results

| Check                          | Expected                                 |
| ------------------------------ | ---------------------------------------- |
| `trace_id=` in debug.log       | Present in log lines within active spans |
| `span_id=` in debug.log        | Present alongside trace_id               |
| Logs without active span       | No trace_id/span_id fields               |
| trace_id in Tempo              | Matches a valid trace                    |
| Loki log ingestion             | Logs visible via LogQL                   |
| Tempo -> Loki "Logs for trace" | Shows correlated log lines               |
| Loki -> Tempo TraceID link     | Navigates to correct trace               |

---

## Troubleshooting

### No traces in Tempo

1. Check otel-collector logs:
   ```bash
   docker compose -f docker/telemetry/docker-compose.yml logs otel-collector
   ```
2. Verify xrpld telemetry config has `enabled=1` and correct endpoint
3. Check that otel-collector port 4318 is accessible (`-f` would fail on the
   receiver's 404 for `GET /`, so test for any HTTP status instead):
   ```bash
   curl -so /dev/null -w '%{http_code}\n' http://localhost:4318/
   ```
4. Increase `batch_delay_ms` or decrease `batch_size` in xrpld config

### Nodes not reaching "proposing" state

1. Check that all peer ports (51235-51240) are not in use:
   ```bash
   for p in 51235 51236 51237 51238 51239 51240; do
       ss -tlnp | grep ":$p " && echo "port $p in use"
   done
   ```
2. Verify `[ips_fixed]` lists all 6 peer ports
3. Verify `validators.txt` has all 6 public keys
4. Check node debug logs: `tail -50 /tmp/xrpld-integration/node1/debug.log`
5. Ensure `[peer_private]` is set to `1` (prevents reaching out to public network)

### Transaction not processing

1. Verify genesis account exists:
   ```bash
   curl -s http://localhost:5005 \
       -d '{"method":"account_info","params":[{"account":"rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"}]}' |
       jq .result.account_data.Balance
   ```
2. Check submit response for error codes
3. In standalone mode, remember to call `ledger_accept` after submitting

### No trace_id in log output (Phase 8)

1. Verify xrpld was built with `telemetry=ON` (`-Dtelemetry=ON` in CMake)
2. Verify `enabled=1` in the `[telemetry]` config section
3. Log lines only contain trace context when emitted inside an active span.
   Background logs (startup, periodic tasks outside spans) will not have
   `trace_id`/`span_id`.
4. Ensure the trace category is enabled (e.g., `trace_rpc=1` for RPC logs)

### No logs in Loki (Phase 8)

1. Verify the log file mount in docker-compose.yml:
   ```yaml
   volumes:
     - ${XRPLD_LOG_DIR:-./data/logs}:/var/log/xrpld:ro
   ```
   The mount source defaults to the repo-relative `docker/telemetry/data/logs`
   (where the telemetry configs write). Override `XRPLD_LOG_DIR` to tail logs
   from another root.
2. Check OTel Collector logs for filelog receiver errors:
   ```bash
   docker compose -f docker/telemetry/docker-compose.yml logs otel-collector | grep -i "filelog\|loki\|error"
   ```
3. Verify Loki is running:
   ```bash
   curl -s http://localhost:3100/ready
   ```
4. Verify the filelog receiver glob pattern matches your log files:
   The default pattern is `/var/log/xrpld/*/debug.log`

### Grafana trace-log links not working (Phase 8)

1. Verify `tracesToLogs` is configured in the Tempo datasource provisioning
   (`docker/telemetry/grafana/provisioning/datasources/tempo.yaml`)
2. Verify `derivedFields` is configured in the Loki datasource provisioning
   (`docker/telemetry/grafana/provisioning/datasources/loki.yaml`)
3. Restart Grafana after changing provisioning files:
   ```bash
   docker compose -f docker/telemetry/docker-compose.yml restart grafana
   ```

### Spanmetrics not appearing in Prometheus

1. Verify otel-collector config has `spanmetrics` connector
2. Check that the metrics pipeline matches `otel-collector-config.yaml`
   verbatim:
   ```yaml
   service:
     pipelines:
       metrics:
         receivers: [otlp, spanmetrics]
         processors: [resource/tier, resource/stripsdk, batch]
         exporters: [prometheus]
   ```
   Both receivers are required. `spanmetrics` carries the span-derived
   `span_*` series; `otlp` carries the node's native `beast::insight` /
   MetricsRegistry metrics, which arrive on the same OTLP port. Dropping
   `otlp` silently removes every native metric while the `span_*` ones keep
   working — so the dashboards only half-break.
3. Verify Prometheus can reach collector:
   ```bash
   curl -s http://localhost:9090/api/v1/targets | jq '.data.activeTargets'
   ```
