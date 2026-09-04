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

This test verifies RPC and transaction spans in standalone mode, plus the
consensus spans that a simulated round still produces. The proposal, voting
and peer-facing consensus spans do not fire — see the expected-spans table at
the end of this test for which do and which do not.

### Step 1: Start the observability stack

```bash
docker compose -f docker/telemetry/docker-compose.yml up -d
```

Wait for services to be ready:

```bash
# otel-collector health
curl -sf http://localhost:13133/ && echo "collector ready"

# Tempo readiness
curl -sf http://localhost:3200/ready >/dev/null && echo "tempo ready"
```

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

Wait 5 seconds for the batch export, then see the "Verification Queries" section below. Its span loop is a superset of what standalone mode produces, so compare its output against the "Expected spans (standalone mode)" table above rather than running a second, narrower set of queries here.

Or open Grafana Explore with Tempo datasource: http://localhost:3000

### Step 6: Teardown

```bash
# Kill xrpld (Ctrl+C or)
kill $(pgrep -f 'xrpld.*xrpld-telemetry')

# Stop observability stack
docker compose -f docker/telemetry/docker-compose.yml down

# Clean xrpld data
rm -rf docker/telemetry/data/
```

### Expected spans (standalone mode)

| Span Name                                                                                                  | Expected | Notes                                             |
| ---------------------------------------------------------------------------------------------------------- | -------- | ------------------------------------------------- |
| `rpc.http_request`                                                                                         | Yes      | Every HTTP RPC call                               |
| `rpc.process`                                                                                              | Yes      | Every RPC processing                              |
| `rpc.command.server_info`                                                                                  | Yes      | server_info RPC                                   |
| `rpc.command.server_state`                                                                                 | Yes      | server_state RPC                                  |
| `rpc.command.ledger`                                                                                       | Yes      | ledger RPC                                        |
| `rpc.command.submit`                                                                                       | Yes      | submit RPC                                        |
| `rpc.command.ledger_accept`                                                                                | Yes      | ledger_accept RPC                                 |
| `tx.process`                                                                                               | Yes      | Transaction submission                            |
| `tx.receive`                                                                                               | No       | No peers in standalone                            |
| `consensus.round`, `.phase.open`, `.ledger_close`, `.accept`, `.accept.apply`                              | Yes      | `ledger_accept` drives a simulated round          |
| `consensus.establish`, `.update_positions`, `.check`, `.proposal.*`, `.validation.receive`, `.mode_change` | No       | `simulate` jumps straight to `Accepted`; no peers |

---

## Test 2: 6-Node Consensus Network (Full Verification)

This test verifies ALL span categories including consensus and peer
transaction relay, using a 6-node validator network.

### Automated

Run the integration test script:

```bash
bash docker/telemetry/integration-test.sh
```

It checks prerequisites, clears the previous run, brings up the observability stack, generates six validator key pairs and their node configs, starts the nodes, waits for consensus and then for a validated ledger, exercises RPC and submits a transaction, verifies traces in Tempo and both the spanmetrics and the StatsD-derived metrics in Prometheus, then prints a summary and leaves the stack running.

The script announces each step as it runs, so read its `Step N:` headers for the authoritative sequence — they are not restated here, because a numbered copy of them drifts as soon as a step is added.

Its Tempo checks cover the RPC, transaction, consensus, ledger and peer span categories from a fixed list, which is narrower than the loop in the "Verification Queries" section below.

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
rm -rf docker/telemetry/data/
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
traces_endpoint=http://localhost:4318/v1/traces
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
}' | jq .result.engine_result
```

Expected result: `"tesSUCCESS"`, the same as Test 1 Step 4.

Wait 15 seconds for consensus and batch export.

#### Step 8: Verify in Tempo

See the "Verification Queries" section below.

---

## Expected Span Catalog

A smoke-test subset: the spans a short local run reliably produces, and how to trigger each. This is not the full catalog — the authoritative span list, with the attributes each span carries, is [docs/telemetry-runbook.md § Span Reference](../../docs/telemetry-runbook.md#span-reference).

Attributes are deliberately not repeated here. Keeping a second copy is how this table came to list attribute keys that no longer exist anywhere in the code.

| Span Name                   | Source File       | How to Trigger            |
| --------------------------- | ----------------- | ------------------------- |
| `rpc.http_request`          | ServerHandler.cpp | Any HTTP RPC call         |
| `rpc.ws_upgrade`            | ServerHandler.cpp | WebSocket upgrade         |
| `rpc.ws_message`            | ServerHandler.cpp | WebSocket RPC message     |
| `rpc.process`               | ServerHandler.cpp | RPC processing            |
| `rpc.command.<name>`        | RPCHandler.cpp    | Any RPC command           |
| `tx.process`                | NetworkOPs.cpp    | Submit transaction        |
| `tx.receive`                | PeerImp.cpp       | Peer relays transaction   |
| `consensus.proposal.send`   | RCLConsensus.cpp  | Consensus proposing phase |
| `consensus.ledger_close`    | RCLConsensus.cpp  | Ledger close event        |
| `consensus.accept`          | RCLConsensus.cpp  | Ledger accepted           |
| `consensus.validation.send` | RCLConsensus.cpp  | Validation sent           |
| `consensus.accept.apply`    | RCLConsensus.cpp  | Ledger apply + close time |
| `tx.apply`                  | BuildLedger.cpp   | Ledger close (tx set)     |
| `ledger.build`              | BuildLedger.cpp   | Ledger build              |
| `ledger.validate`           | LedgerMaster.cpp  | Ledger validated          |
| `ledger.store`              | LedgerMaster.cpp  | Ledger stored             |
| `peer.proposal.receive`     | PeerImp.cpp       | Peer sends proposal       |
| `peer.validation.receive`   | PeerImp.cpp       | Peer sends validation     |

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

# Span call counts (from the spanmetrics connector). The span_ prefix is the
# connector's `namespace: "span"` in otel-collector-config.yaml; drop that
# setting and these become traces_span_metrics_*.
curl -s "$PROM/api/v1/query?query=span_calls_total" |
    jq '.data.result[] | {span: .metric.span_name, count: .value[1]}'

# Latency histogram
curl -s "$PROM/api/v1/query?query=span_duration_milliseconds_count" |
    jq '.data.result[] | {span: .metric.span_name, count: .value[1]}'

# RPC calls by command
curl -s "$PROM/api/v1/query?query=span_calls_total{span_name=~\"rpc.command.*\"}" |
    jq '.data.result[] | {command: .metric.command, count: .value[1]}'

# Deployment-tier labels present on metrics (set by the collector's
# resource/tier processor and promoted via resource_to_telemetry_conversion).
# Expect deployment_environment and xrpl_network_type on each series.
curl -s "$PROM/api/v1/query?query=span_calls_total" |
    jq '.data.result[0].metric | {deployment_environment, xrpl_network_type, service_name}'
```

### Grafana

Open http://localhost:3000 (anonymous admin access enabled).

Pre-configured dashboards:

- **RPC Performance**: Request rates, latency percentiles by command, top commands, WebSocket rate
- **Transaction Overview**: Transaction processing rates, apply duration, peer relay, failed tx rate
- **Consensus Health**: Consensus round duration, proposer counts, mode tracking, accept heatmap
- **Ledger Operations**: Build/validate/store rates and durations, TX apply metrics
- **Peer Network**: Proposal/validation receive rates, trusted vs untrusted breakdown (requires `trace_peer=1`)

Pre-configured datasources:

- **Tempo**: Trace data at `http://tempo:3200`
- **Prometheus**: Metrics at `http://prometheus:9090`
- **Loki**: Log data at `http://loki:3100` (via Grafana Explore)

---

## Test 3: Log-Trace Correlation

xrpld injects `trace_id` and `span_id` into its log output when
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
curl -s "http://localhost:3200/api/traces/$TRACE_ID" | jq '.data | length'
```

Expected result: `1` (the trace exists in Tempo).

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
3. Check that otel-collector port 4318 is accessible:
   ```bash
   curl -sf http://localhost:4318 && echo "reachable"
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

### No trace_id in log output

1. Verify xrpld was built with `telemetry=ON` (`-Dtelemetry=ON` in CMake)
2. Verify `enabled=1` in the `[telemetry]` config section
3. Log lines only contain trace context when emitted inside an active span.
   Background logs (startup, periodic tasks outside spans) will not have
   `trace_id`/`span_id`.
4. Ensure the trace category is enabled (e.g., `trace_rpc=1` for RPC logs)

### No logs in Loki

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

### Grafana trace-log links not working

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
2. Check that the metrics pipeline is configured:
   ```yaml
   service:
     pipelines:
       metrics:
         receivers: [spanmetrics]
         exporters: [prometheus]
   ```
3. Verify Prometheus can reach collector:
   ```bash
   curl -s http://localhost:9090/api/v1/targets | jq '.data.activeTargets'
   ```
