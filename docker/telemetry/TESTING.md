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
# otel-collector health
curl -sf http://localhost:13133/ && echo "collector ready"

# Tempo readiness
curl -sf http://localhost:3200/ready > /dev/null && echo "tempo ready"
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
  -d '{"method":"ledger","params":[{"ledger_index":"current"}]}' \
  | jq .result.ledger_current_index
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
  --data-urlencode 'q={resource.service.name="xrpld" && name="rpc.request"}' \
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
| `rpc.request`               | Yes      | Every HTTP RPC call           |
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
exporter=otlp_http
sampling_ratio=1.0
batch_size=512
batch_delay_ms=2000
max_queue_size=2048
trace_rpc=1
trace_transactions=1
trace_consensus=1
trace_peer=0
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
  echo $! > /tmp/xrpld-integration/node$i/xrpld.pid
done
```

#### Step 6: Wait for consensus

Poll each node until `server_state` = `"proposing"`:

```bash
for port in 5005 5006 5007 5008 5009 5010; do
  while true; do
    state=$(curl -s http://localhost:$port \
      -d '{"method":"server_info"}' \
      | jq -r '.result.info.server_state')
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

All 16 production span names instrumented across Phases 2-5:

| Span Name                   | Source File           | Phase | Key Attributes                                                                           | How to Trigger            |
| --------------------------- | --------------------- | ----- | ---------------------------------------------------------------------------------------- | ------------------------- |
| `rpc.request`               | ServerHandler.cpp:271 | 2     | --                                                                                       | Any HTTP RPC call         |
| `rpc.process`               | ServerHandler.cpp:573 | 2     | --                                                                                       | Any HTTP RPC call         |
| `rpc.ws_message`            | ServerHandler.cpp:384 | 2     | --                                                                                       | WebSocket RPC message     |
| `rpc.command.<name>`        | RPCHandler.cpp:161    | 2     | `xrpl.rpc.command`, `xrpl.rpc.version`, `xrpl.rpc.role`                                  | Any RPC command           |
| `tx.process`                | NetworkOPs.cpp:1227   | 3     | `xrpl.tx.hash`, `xrpl.tx.local`, `xrpl.tx.path`                                          | Submit transaction        |
| `tx.receive`                | PeerImp.cpp:1273      | 3     | `xrpl.peer.id`                                                                           | Peer relays transaction   |
| `consensus.proposal.send`   | RCLConsensus.cpp:177  | 4     | `xrpl.consensus.round`                                                                   | Consensus proposing phase |
| `consensus.ledger_close`    | RCLConsensus.cpp:282  | 4     | `xrpl.consensus.ledger.seq`, `xrpl.consensus.mode`                                       | Ledger close event        |
| `consensus.accept`          | RCLConsensus.cpp:395  | 4     | `xrpl.consensus.proposers`, `xrpl.consensus.round_time_ms`                               | Ledger accepted           |
| `consensus.validation.send` | RCLConsensus.cpp:753  | 4     | `xrpl.consensus.ledger.seq`, `xrpl.consensus.proposing`                                  | Validation sent           |
| `consensus.accept.apply`    | RCLConsensus.cpp:453  | 4     | `xrpl.consensus.close_time`, `close_time_correct`, `close_resolution_ms`, `state`        | Ledger apply + close time |
| `tx.apply`                  | BuildLedger.cpp:88    | 5     | `xrpl.ledger.tx_count`, `xrpl.ledger.tx_failed`                                          | Ledger close (tx set)     |
| `ledger.build`              | BuildLedger.cpp:31    | 5     | `xrpl.ledger.seq`, `xrpl.ledger.close_time`, `close_time_correct`, `close_resolution_ms` | Ledger build              |
| `ledger.validate`           | LedgerMaster.cpp:915  | 5     | `xrpl.ledger.seq`, `xrpl.ledger.validations`                                             | Ledger validated          |
| `ledger.store`              | LedgerMaster.cpp:409  | 5     | `xrpl.ledger.seq`                                                                        | Ledger stored             |
| `peer.proposal.receive`     | PeerImp.cpp:1667      | 5     | `xrpl.peer.id`, `xrpl.peer.proposal.trusted`                                             | Peer sends proposal       |
| `peer.validation.receive`   | PeerImp.cpp:2264      | 5     | `xrpl.peer.id`, `xrpl.peer.validation.trusted`                                           | Peer sends validation     |

---

## Verification Queries

### Tempo API

Base URL: `http://localhost:3200`

```bash
TEMPO="http://localhost:3200"

# List all services
curl -s "$TEMPO/api/v2/search/tag/resource.service.name/values" | jq '.tagValues[].value'

# Query traces by operation
for op in "rpc.request" "rpc.process" \
          "rpc.command.server_info" "rpc.command.server_state" "rpc.command.ledger" \
          "tx.process" "tx.receive" "tx.apply" \
          "consensus.proposal.send" "consensus.ledger_close" \
          "consensus.accept" "consensus.accept.apply" \
          "consensus.validation.send" \
          "ledger.build" "ledger.validate" "ledger.store" \
          "peer.proposal.receive" "peer.validation.receive"; do
  count=$(curl -s "$TEMPO/api/search" \
    --data-urlencode "q={resource.service.name=\"xrpld\" && name=\"$op\"}" \
    --data-urlencode "limit=5" \
    | jq '.traces | length')
  printf "%-35s %s traces\n" "$op" "$count"
done
```

### Prometheus API

Base URL: `http://localhost:9090`

```bash
PROM="http://localhost:9090"

# Span call counts (from spanmetrics connector)
curl -s "$PROM/api/v1/query?query=traces_span_metrics_calls_total" \
  | jq '.data.result[] | {span: .metric.span_name, count: .value[1]}'

# Latency histogram
curl -s "$PROM/api/v1/query?query=traces_span_metrics_duration_milliseconds_count" \
  | jq '.data.result[] | {span: .metric.span_name, count: .value[1]}'

# RPC calls by command
curl -s "$PROM/api/v1/query?query=traces_span_metrics_calls_total{span_name=~\"rpc.command.*\"}" \
  | jq '.data.result[] | {command: .metric["xrpl.rpc.command"], count: .value[1]}'
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

## Test 3: Log-Trace Correlation (Phase 8)

Phase 8 injects `trace_id` and `span_id` into rippled's log output when
a log line is emitted within an active OTel span. This test verifies the
end-to-end log-trace correlation pipeline.

### Step 1: Verify trace_id in log output

After running Test 1 or Test 2 (which generate RPC spans), check the
rippled debug.log for trace context:

```bash
grep 'trace_id=[a-f0-9]\{32\} span_id=[a-f0-9]\{16\}' /path/to/debug.log
```

Expected: log lines with `trace_id=<32hex> span_id=<16hex>` between the
severity code and the message. Example:

```
2024-01-15T10:30:45.123Z RPCHandler:NFO trace_id=abc123def456789012345678abcdef01 span_id=0123456789abcdef Calling server_info
```

Lines emitted outside of an active span (background tasks, startup) will
NOT have trace context — this is expected.

### Step 2: Cross-check trace_id in Jaeger

Extract a `trace_id` from the log and verify it exists in Jaeger:

```bash
TRACE_ID=$(grep -o 'trace_id=[a-f0-9]\{32\}' /path/to/debug.log | head -1 | cut -d= -f2)
echo "Checking trace: $TRACE_ID"
curl -s "http://localhost:16686/api/traces/$TRACE_ID" | jq '.data | length'
```

Expected result: `1` (the trace exists in Jaeger).

### Step 3: Verify Loki log ingestion

The OTel Collector's filelog receiver tails rippled's debug.log and
exports parsed entries to Loki. Verify Loki has received entries:

```bash
# Query Loki for any rippled logs
curl -sG "http://localhost:3100/loki/api/v1/query" \
  --data-urlencode 'query={job="rippled"}' \
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
2. Query: `{job="rippled"} |= "trace_id="`
3. In the log results, click the **TraceID** derived field link
4. Verify it navigates to the full trace in Tempo

### Expected results

| Check                          | Expected                                 |
| ------------------------------ | ---------------------------------------- |
| `trace_id=` in debug.log       | Present in log lines within active spans |
| `span_id=` in debug.log        | Present alongside trace_id               |
| Logs without active span       | No trace_id/span_id fields               |
| trace_id in Jaeger             | Matches a valid trace                    |
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
     -d '{"method":"account_info","params":[{"account":"rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"}]}' \
     | jq .result.account_data.Balance
   ```
2. Check submit response for error codes
3. In standalone mode, remember to call `ledger_accept` after submitting

### No trace_id in log output (Phase 8)

1. Verify rippled was built with `telemetry=ON` (`-Dtelemetry=ON` in CMake)
2. Verify `enabled=1` in the `[telemetry]` config section
3. Log lines only contain trace context when emitted inside an active span.
   Background logs (startup, periodic tasks outside spans) will not have
   `trace_id`/`span_id`.
4. Ensure the trace category is enabled (e.g., `trace_rpc=1` for RPC logs)

### No logs in Loki (Phase 8)

1. Verify the log file mount in docker-compose.yml:
   ```yaml
   volumes:
     - /tmp/xrpld-integration:/var/log/rippled:ro
   ```
2. Check OTel Collector logs for filelog receiver errors:
   ```bash
   docker compose -f docker/telemetry/docker-compose.yml logs otel-collector | grep -i "filelog\|loki\|error"
   ```
3. Verify Loki is running:
   ```bash
   curl -s http://localhost:3100/ready
   ```
4. Verify the filelog receiver glob pattern matches your log files:
   The default pattern is `/var/log/rippled/*/debug.log`

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
