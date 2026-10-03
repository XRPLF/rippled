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

Close the ledger to drive a simulated consensus round — that round is what
produces the `consensus.*` spans. It is not required for `submit` itself:
standalone puts the node in `OperatingMode::FULL` at startup
(`NetworkOPsImp::setStandAlone()`), and the one validated-ledger-age gate on
the submit path is skipped when `config.standalone()` is set
(`checkTxJsonFields()` in `src/xrpld/rpc/detail/TransactionSign.cpp`).

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
      "Destination": "rN7n7otQDd6FczFgLdSqtcsAUxDkw6fzRH",
      "Amount": "10000000"
    }
  }]
}' | jq .result.engine_result
```

Expected result: `"tesSUCCESS"`.

The destination does not have to exist yet. 10 XRP is exactly the default base
reserve (`FeeSetup::accountReserve` in `src/xrpld/core/Config.h`), so the
payment creates and funds the account. `integration-test.sh` does not hardcode
a destination at all — it calls `wallet_propose` and uses the `account_id` that
comes back.

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
pkill -f 'xrpld --conf docker/telemetry/xrpld-telemetry\.cfg'

# Stop observability stack
docker compose -f docker/telemetry/docker-compose.yml down

# Clean xrpld data
rm -rf docker/telemetry/data/
```

The pattern is anchored on the whole `--conf <path>` argument with the `.`
escaped, so it matches this node and not another xrpld run or an editor whose
command line happens to name the same file. `pkill` is also a no-op when
nothing matches, where `kill $(pgrep ...)` errors out with no arguments.

### Expected spans (standalone mode)

| Span Name                                                                     | Expected | Notes                                      |
| ----------------------------------------------------------------------------- | -------- | ------------------------------------------ |
| `rpc.http_request`                                                            | Yes      | Every HTTP RPC call                        |
| `rpc.process`                                                                 | Yes      | Every RPC processing                       |
| `rpc.command.server_info`                                                     | Yes      | server_info RPC                            |
| `rpc.command.server_state`                                                    | Yes      | server_state RPC                           |
| `rpc.command.ledger`                                                          | Yes      | ledger RPC                                 |
| `rpc.command.submit`                                                          | Yes      | submit RPC                                 |
| `rpc.command.ledger_accept`                                                   | Yes      | ledger_accept RPC                          |
| `rpc.ws_upgrade`, `rpc.ws_message`                                            | No       | Need a WebSocket client                    |
| `tx.process`                                                                  | Yes      | Transaction submission                     |
| `tx.preflight`, `tx.preclaim`, `tx.transactor`                                | Yes      | Apply stages of the Payment                |
| `tx.apply`                                                                    | Yes      | Ledger build applies the tx set            |
| `tx.receive`                                                                  | No       | No peers in standalone                     |
| `txq.enqueue`, `txq.apply_direct`                                             | Yes      | `TxQ::apply` on the submit path            |
| `txq.accept`, `txq.cleanup`                                                   | Yes      | Run on every ledger close                  |
| `txq.accept_tx`, `txq.batch_clear`                                            | No       | Nothing is ever queued here                |
| `ledger.build`, `ledger.store`                                                | Yes      | `buildLCL` builds, then stores             |
| `ledger.validate`                                                             | No       | `checkAccept` is unreachable in standalone |
| `consensus.round`, `.phase.open`, `.ledger_close`, `.accept`, `.accept.apply` | Yes      | `ledger_accept` drives a simulated round   |
| `consensus.mode_change`                                                       | Yes      | Fires once per round start                 |
| `consensus.establish`, `.update_positions`, `.check`                          | No       | `phaseEstablish()` never runs              |
| `consensus.proposal.send`, `.validation.send`                                 | No       | The config carries no validator key        |
| `consensus.proposal.receive`, `.validation.receive`                           | No       | No peers                                   |
| `peer.proposal.receive`, `peer.validation.receive`                            | No       | No peers                                   |
| `pathfind.*`                                                                  | No       | No path request, no path subscription      |
| `grpc.*`                                                                      | No       | No `[port_grpc]` in the config             |

Four of the "No" rows have a reason worth spelling out.

- `ledger.validate` belongs to `LedgerMaster::checkAccept`, and standalone never
  reaches it: `consensusBuilt` returns early when standalone, and `switchLCL`
  takes its standalone branch instead of calling `checkAccept`. That
  `getNeededValidations()` returns 0 in standalone is therefore not enough on its
  own.
- `consensus.establish`, `.update_positions` and `.check` are started from
  `phaseEstablish()`. `simulate` does call `closeLedger({})` — which is exactly
  why `.phase.open` and `.ledger_close` do fire — and then sets the phase to
  `Accepted` itself, so `phaseEstablish()` is never entered.
- `.proposal.send` and `.validation.send` are absent for a different reason
  again: `xrpld-telemetry.cfg` carries no `validation_seed` or
  `validator_token`, so `preStartRound` leaves `validating_` false. The node
  observes rather than proposes, and `validate()` — the owner of
  `.validation.send` — is never called.
- `pathfind.update_all` is emitted only while at least one path subscription is
  active, and this test makes no `path_find` or `ripple_path_find` call.

`.mode_change` is in the "Yes" rows because it does not depend on the mode
actually changing. `startRoundInternal` calls `mode_.set()`, `MonitoredMode::set`
calls `onModeChange` with no equality test, and `onModeChange` creates the span
before the `before != after` check — that check guards only the censorship-detector
reset.

One `consensus.round` span reaches Tempo, not two. `roundSpan_` is reset only at
the top of the next `startRoundTracing()`, so after the two `ledger_accept` calls
the first round's span has ended and been exported while the second is still open.
Only ended spans are exported.

---

## Test 2: 6-Node Consensus Network (Full Verification)

This test verifies ALL span categories including consensus and peer
transaction relay, using a 6-node validator network.

### Automated

Run the integration test script:

```bash
bash docker/telemetry/integration-test.sh
```

It checks prerequisites, clears the previous run, brings up the observability stack, generates six validator key pairs and their node configs, starts the nodes, waits for consensus and then for a validated ledger, exercises RPC and submits a transaction, verifies traces in Tempo and both the span_metrics and the StatsD-derived metrics in Prometheus, then prints a summary and leaves the stack running.

The authoritative sequence is the 14 `# Step N:` banner comments in the script source, so read the file rather than the console — none of the script's 48 runtime `log` lines print a step number. The sequence is not restated here, because a numbered copy of it drifts as soon as a step is added.

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
{one "127.0.0.1 <port>" line for each port in 51235-51240 except this node's
own 51234 + node_number — a node must not list itself as a fixed peer, so
each config carries five lines, not six}

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

[insight]
server=statsd
address=127.0.0.1:8125
prefix={the same prefix the [insight] block in integration-test.sh sets — it
becomes the Prometheus metric-name prefix, so any other value renames every
beast::insight metric}

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
      "Destination": "rN7n7otQDd6FczFgLdSqtcsAUxDkw6fzRH",
      "Amount": "10000000"
    }
  }]
}' | jq .result.engine_result
```

Expected result: `"tesSUCCESS"`, the same as Test 1 Step 4.

Wait 15 seconds for the consensus round and the trace batch export. Prometheus
needs longer: `integration-test.sh` waits a further 20 s before its span_metrics
queries and another 20 s before its StatsD queries, so 35 s and 55 s after the
submit. Querying the metrics block at 15 s returns no series, which looks like a
broken pipeline and is not one.

#### Step 8: Verify in Tempo and Prometheus

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

Run `RUN_START=$(date +%s)` **before** starting xrpld (Test 1 Step 2, Test 2
Step 5), in the same shell you will run the block below in. Tempo keeps blocks
for `block_retention` (`tempo.yaml`, 1h) on a named volume, so a search with no
time bound is answered by the previous run's traces.

```bash
TEMPO="http://localhost:3200"

# Refuse to run unbounded rather than report a previous run's traces.
: "${RUN_START:?record RUN_START=\$(date +%s) before starting xrpld}"

# List all services
curl -s "$TEMPO/api/v2/search/tag/resource.service.name/values" | jq '.tagValues[].value'

# Count traces per span name. Test 1 produces a subset of this list — read it
# against the "Expected spans (standalone mode)" table above, not as pass/fail.
#
# -G is required: it moves the urlencoded parameters into the query string.
# Without it curl POSTs them as a request body, Tempo answers 200 and ignores
# the query, and every span name comes back non-zero. start/end bound the
# search to this run; the end margin covers spans exported while the query is
# in flight.
for op in "rpc.http_request" "rpc.process" \
    "rpc.command.server_info" "rpc.command.server_state" "rpc.command.ledger" \
    "rpc.command.submit" "rpc.command.ledger_accept" \
    "tx.process" "tx.receive" "tx.apply" \
    "tx.preflight" "tx.preclaim" "tx.transactor" \
    "txq.enqueue" "txq.apply_direct" "txq.accept" "txq.cleanup" \
    "consensus.round" "consensus.phase.open" "consensus.ledger_close" \
    "consensus.establish" "consensus.update_positions" "consensus.check" \
    "consensus.accept" "consensus.accept.apply" \
    "consensus.proposal.send" "consensus.validation.send" \
    "consensus.mode_change" \
    "consensus.proposal.receive" "consensus.validation.receive" \
    "ledger.build" "ledger.validate" "ledger.store" \
    "peer.proposal.receive" "peer.validation.receive"; do
    count=$(curl -sfG "$TEMPO/api/search" \
        --data-urlencode "q={resource.service.name=\"xrpld\" && name=\"$op\"}" \
        --data-urlencode "start=$RUN_START" \
        --data-urlencode "end=$(($(date +%s) + 60))" \
        --data-urlencode "limit=5" |
        jq '.traces | length')
    printf "%-35s %s traces\n" "$op" "$count"
done
```

Eight more span families exist but need a trigger neither test performs, so they
are counted separately — a zero here is the expected answer, not a failure.
`rpc.ws_*` need a WebSocket client, the `pathfind.*` family needs a `path_find`
or `ripple_path_find` call, and the two `txq` names need a transaction sitting in
the queue.

```bash
for op in "rpc.ws_upgrade" "rpc.ws_message" \
    "pathfind.request" "pathfind.compute" "pathfind.discover" "pathfind.update_all" \
    "txq.accept_tx" "txq.batch_clear"; do
    count=$(curl -sfG "$TEMPO/api/search" \
        --data-urlencode "q={resource.service.name=\"xrpld\" && name=\"$op\"}" \
        --data-urlencode "start=$RUN_START" \
        --data-urlencode "end=$(($(date +%s) + 60))" \
        --data-urlencode "limit=5" |
        jq '.traces | length')
    printf "%-35s %s traces\n" "$op" "$count"
done
```

The remaining family is `grpc.<method>`, whose span name is the gRPC method, so
it has no fixed string to query and needs a `[port_grpc]` stanza neither test
configures.

### Prometheus API

Base URL: `http://localhost:9090`

```bash
PROM="http://localhost:9090"

# Span call counts (from span_metrics connector)
curl -s "$PROM/api/v1/query?query=traces_span_metrics_calls_total" |
    jq '.data.result[] | {span: .metric.span_name, count: .value[1]}'

# Latency histogram
curl -s "$PROM/api/v1/query?query=traces_span_metrics_duration_milliseconds_count" |
    jq '.data.result[] | {span: .metric.span_name, count: .value[1]}'

# RPC calls by command
curl -s "$PROM/api/v1/query?query=traces_span_metrics_calls_total{span_name=~\"rpc.command.*\"}" |
    jq '.data.result[] | {command: .metric.command, count: .value[1]}'

# Deployment-tier labels present on metrics (set by the collector's
# resource/tier processor and promoted via resource_to_telemetry_conversion).
# Expect deployment_environment and xrpl_network_type on each series.
curl -s "$PROM/api/v1/query?query=traces_span_metrics_calls_total" |
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
2. Verify `[ips_fixed]` lists the 5 other peer ports, and not the node's own
3. Verify `validators.txt` has all 6 public keys
4. Check node debug logs: `tail -50 /tmp/xrpld-integration/node1/debug.log`
5. Ensure `[peer_private]` is set to `1`. In `src/libxrpl/peerfinder/Config.cpp`
   it sets both `autoConnect = !standalone && !peerPrivate` and
   `wantIncoming = (!config.peerPrivate) && (port != 0)`, so it stops the node
   reaching out to the public network **and** stops it accepting inbound peers.
   The nodes here find each other through `[ips_fixed]`, which is unaffected.

### Transaction not processing

1. Verify genesis account exists:
   ```bash
   curl -s http://localhost:5005 \
       -d '{"method":"account_info","params":[{"account":"rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"}]}' |
       jq .result.account_data.Balance
   ```
2. Check submit response for error codes
3. In standalone mode, remember to call `ledger_accept` after submitting

### Spanmetrics not appearing in Prometheus

1. Verify otel-collector config has `span_metrics` connector
2. Check that the metrics pipeline is configured:
   ```yaml
   service:
     pipelines:
       metrics:
         receivers: [span_metrics]
         exporters: [prometheus]
   ```
3. Verify Prometheus can reach collector:
   ```bash
   curl -s http://localhost:9090/api/v1/targets | jq '.data.activeTargets'
   ```
